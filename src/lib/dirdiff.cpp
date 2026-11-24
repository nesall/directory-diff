#include "directory-diff/dirdiff.h"
#include <algorithm>
#include <map>
#include <atomic>
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

  template<typename OnEntry>
  void scanEntries(const fs::path& root, const DiffOptions& options, const IgnoreMatcher& ignore, OnEntry onEntry) {
    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied), end;
    for (; it != end; ++it) {
      const auto& entry = *it;
      const auto relativePath = entry.path().lexically_relative(root);
      const bool directory = entry.is_directory();
      if ((options.ignoreHidden && isHiddenPath(relativePath)) || ignore.matches(relativePath, directory)) {
        if (directory) it.disable_recursion_pending();
        continue;
      }

      if (directory) {
        if (options.includeDirectories)
          onEntry(relativePath, DiffEntryKind::Directory);
      } else if (entry.is_regular_file()) {
        onEntry(relativePath, DiffEntryKind::File);
      }
    }
  }
} // anonymous namespace

size_t diffDirectories(
  const fs::path& dirA,
  const fs::path& dirB,
  const DiffOptions& options,
  DiffCallback onDiffFound)
{
  struct ComparedEntry {
    DiffEntryKind left = DiffEntryKind::Missing;
    DiffEntryKind right = DiffEntryKind::Missing;
    DiffType result = DiffType::Identical;
  };

  std::map<fs::path, ComparedEntry> entries;
  const IgnoreMatcher ignore(options.ignorePatterns);
  scanEntries(dirA, options, ignore, [&](const fs::path& path, DiffEntryKind kind) {
    entries[path].left = kind;
  });
  scanEntries(dirB, options, ignore, [&](const fs::path& path, DiffEntryKind kind) {
    entries[path].right = kind;
  });

  using Entry = decltype(entries)::value_type;
  std::vector<Entry*> pairs;
  for (auto& entry : entries) {
    auto& diff = entry.second;
    if (diff.left == DiffEntryKind::Missing)
      diff.result = DiffType::Added;
    else if (diff.right == DiffEntryKind::Missing)
      diff.result = DiffType::Removed;
    else if (diff.left != diff.right)
      diff.result = DiffType::Modified;
    else if (diff.left == DiffEntryKind::File)
      pairs.push_back(&entry);
  }

  if (!pairs.empty()) {
    const auto numThreads = std::min<size_t>(std::max(1u, std::thread::hardware_concurrency()), pairs.size());
    std::atomic<size_t> next = 0;
    std::vector<std::jthread> threads;
    for (size_t t = 0; t < numThreads; ++t) {
      threads.emplace_back([&] {
        while (true) {
          const auto index = next.fetch_add(1);
          if (index >= pairs.size()) break;
          auto& [path, diff] = *pairs[index];
          try {
            diff.result = diffFile(dirA / path, dirB / path, options);
          } catch (const fs::filesystem_error&) {
            // A file may be removed or become unreadable during comparison.
            diff.result = DiffType::Modified;
          }
        }
      });
    }
  }

  size_t identicalFiles = 0;
  for (const auto& [path, diff] : entries) {
    if (diff.result == DiffType::Identical && diff.left == DiffEntryKind::File)
      ++identicalFiles;
    if (options.includeIdentical || diff.result != DiffType::Identical)
      onDiffFound({ path, diff.result, diff.left, diff.right });
  }
  return identicalFiles;
}
