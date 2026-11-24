#include "directory-diff/dirdiff.h"
#include <algorithm>
#include <map>
#include <fstream>
#include <sstream>
#include <regex>
#include <string>
#include <thread>

namespace fs = std::filesystem;


namespace {

  bool isHiddenPath(const fs::path& p) {
    for (const auto& part : p) {
      if (part.native().starts_with(static_cast<fs::path::value_type>('.'))) return true;
    }
    return false;
  }

  std::regex wildcardRegex(const std::string& pattern) {
    std::string expression;
    for (char c : pattern) {
      switch (c) {
      case '*': expression += ".*"; break;
      case '.': case '\\': case '+': case '?': case '^': case '$': case '(': case ')':
      case '[': case ']': case '{': case '}': case '|': expression += '\\'; expression += c; break;
      default: expression += c; break;
      }
    }
    return std::regex(expression, std::regex::ECMAScript | std::regex::icase);
  }

  class IgnoreMatcher {
  public:
    explicit IgnoreMatcher(const std::vector<std::string>& patterns) {
      for (auto pattern : patterns) {
#ifdef _WIN32
        std::replace(pattern.begin(), pattern.end(), '\\', '/');
#endif
        const bool directory = pattern.ends_with('/');
        if (directory) pattern.pop_back();
        if (pattern.empty()) continue;

        auto& rules = directory ? directories_ : files_;
        if (!directory && pattern.size() > 2 && pattern.starts_with("*.") && pattern.find('*', 1) == std::string::npos) {
          rules.push_back({ {}, pattern.substr(1), false });
          continue;
        }
        try {
          rules.push_back({ wildcardRegex(pattern), {}, pattern.find('/') != std::string::npos });
        } catch (const std::regex_error&) {
          continue;
        }
      }
    }

    bool hasDirectoryRules() const { return !directories_.empty(); }

    bool matches(const fs::path& relativePath, bool directory) const {
      const auto& rules = directory ? directories_ : files_;
      if (rules.empty()) return false;
      const auto utf8 = relativePath.generic_u8string();
      const std::string full(utf8.begin(), utf8.end());
      const std::string name = full.substr(full.find_last_of('/') + 1);

      for (const auto& rule : rules) {
        try {
          if (!rule.suffix.empty()) {
            if (name.ends_with(rule.suffix)) return true;
          } else if (directory) {
            if (std::regex_match(rule.relativePath ? full : name, rule.regex)) return true;
          } else if (std::regex_search(full, rule.regex) || std::regex_search(name, rule.regex)) {
            return true;
          }
        } catch (const std::regex_error&) {
          continue;
        }
      }
      return false;
    }

  private:
    struct Rule {
      std::regex regex;
      std::string suffix;
      bool relativePath;
    };

    std::vector<Rule> files_;
    std::vector<Rule> directories_;
  };

  template<typename OnFile>
  void scanFiles(const fs::path& root, const DiffOptions& options, const IgnoreMatcher& ignore, OnFile onFile) {
    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied), end;
    for (; it != end; ++it) {
      const auto& entry = *it;
      if (!entry.is_regular_file()) {
        if (ignore.hasDirectoryRules() && entry.is_directory() && ignore.matches(entry.path().lexically_relative(root), true))
          it.disable_recursion_pending();
        continue;
      }

      fs::path relativePath;
      try {
        relativePath = fs::relative(entry.path(), root);
      } catch (const fs::filesystem_error&) {
        continue;
      }

      if (options.ignoreHidden && isHiddenPath(relativePath)) continue;
      if (ignore.matches(relativePath, false)) continue;
      onFile(relativePath, entry.path());
    }
  }

  struct FilePair {
    fs::path relative;
    fs::path pathA;
    fs::path pathB;
  };

} // anonymous namespace


// Example output format:
// +added_file.txt
// -removed_file.txt
// ~modified_file.cpp
size_t diffDirectories(
  const fs::path& dirA,
  const fs::path& dirB,
  const DiffOptions& options,
  DiffCallback onDiffFound)
{
  std::vector<FilePair> work;
  std::map<fs::path, DiffType> fileMap;
  const IgnoreMatcher ignore(options.ignorePatterns);

  // Scan dirA
  scanFiles(dirA, options, ignore, [&](const fs::path& relativePath, const fs::path&) {
    fileMap[relativePath] = DiffType::Removed; // Assume removed until we find it in dirB
  });

  // Scan dirB
  scanFiles(dirB, options, ignore, [&](const fs::path& relativePath, const fs::path& pathB) {
    auto it = fileMap.find(relativePath);
    if (it != fileMap.end()) {
      const auto pathA = dirA / relativePath;
      try {
        bool sameSize = fs::file_size(pathA) == fs::file_size(pathB);
        if (!sameSize) {
          it->second = DiffType::Modified;
          return;
        }
      } catch (const fs::filesystem_error&) {
        it->second = DiffType::Modified; // If we can't get size, treat as modified
        return;
      }
      work.push_back({ relativePath, pathA, pathB });
    } else {
      fileMap[relativePath] = DiffType::Added; // New file in dirB
    }
  });

  std::atomic<size_t> nofIdentical = 0;

  if (!work.empty()) {

    const unsigned numThreads = std::min(
      std::max(1u, std::thread::hardware_concurrency()),
      static_cast<unsigned>(work.size()));

    std::atomic<size_t> index = 0;
    std::vector<DiffType> results(work.size());
    std::vector<std::thread> threads;

    for (unsigned t = 0; t < numThreads; ++t) {
      threads.emplace_back([&]()
        {
          while (true) {
            size_t i = index.fetch_add(1);
            if (i >= work.size()) break;
            const auto& job = work[i];
            auto df = diffFile(job.pathA, job.pathB, options);
            if (df == DiffType::Identical) {
              ++nofIdentical;
             }
            results[i] = df;
          }
        });
    }
    for (auto& t : threads) t.join();

    for (size_t i = 0; i < work.size(); ++i) {
      fileMap[work[i].relative] = results[i];
    }

  }

  // Report differences
  for (const auto& [relativePath, diffType] : fileMap) {
    if (options.includeIdentical || diffType != DiffType::Identical) {
      onDiffFound({ relativePath, diffType });
    }
  }

  return nofIdentical;
}
