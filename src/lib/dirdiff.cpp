#include "directory-diff/dirdiff.h"
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
      auto s = part.string();
      if (!s.empty() && s[0] == '.') return true;
    }
    return false;
  }

  bool matchesAnyPattern(const fs::path& relPath, const std::vector<std::string>& patterns) {
    if (patterns.empty()) return false;
    std::string full = relPath.string();
    std::string filename = relPath.filename().string();

    for (const auto& pat : patterns) {
      // Convert simple wildcard pattern ('*') to regex. Escape other regex metacharacters.
      std::string rx;
      for (char c : pat) {
        switch (c) {
        case '*': rx += ".*"; break;
        case '.': case '\\': case '+': case '?': case '^': case '$': case '(': case ')':
        case '[': case ']': case '{': case '}': case '|': case '/': rx += '\\'; rx += c; break;
        default: rx += c; break;
        }
      }
      try {
        if (pat.starts_with("*.") && !pat.ends_with("*")) {
          if (2 < pat.length()) {
            std::string ext = pat.substr(1); // extension with dot
            if (ext.size() <= filename.size() && 0 == filename.compare(filename.size() - ext.size(), ext.size(), ext)) {
              return true;
            }
          }
        } else {
          std::regex re(rx, std::regex::ECMAScript | std::regex::icase);
          if (std::regex_search(full, re) || std::regex_search(filename, re)) {
            return true;
          }
        }
      } catch (...) {
        // If regex fails, skip this pattern.
        continue;
      }
    }
    return false;
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

  // Scan dirA
  for (const auto& entry : fs::recursive_directory_iterator(dirA, fs::directory_options::skip_permission_denied)) {
    if (!entry.is_regular_file()) continue;
    fs::path relativePath;
    try {
      relativePath = fs::relative(entry.path(), dirA);
    } catch (const fs::filesystem_error&) {
      continue; // Skip files that can't be made relative
    }

    if (options.ignoreHidden && isHiddenPath(relativePath)) continue;
    if (matchesAnyPattern(relativePath, options.ignorePatterns)) continue;

    fileMap[relativePath] = DiffType::Removed; // Assume removed until we find it in dirB
  }

  // Scan dirB
  for (const auto& entry : fs::recursive_directory_iterator(dirB, fs::directory_options::skip_permission_denied)) {
    if (!entry.is_regular_file()) continue;
    fs::path relativePath;
    try {
      relativePath = fs::relative(entry.path(), dirB);
    } catch (const fs::filesystem_error&) {
      continue; // Skip files that can't be made relative
    }

    if (options.ignoreHidden && isHiddenPath(relativePath)) continue;
    if (matchesAnyPattern(relativePath, options.ignorePatterns)) continue;

    auto it = fileMap.find(relativePath);
    if (it != fileMap.end()) {
      const auto pathA = dirA / relativePath;
      const auto pathB = entry.path();
      try {
        bool sameSize = fs::file_size(pathA) == fs::file_size(pathB);
        if (!sameSize) {
          it->second = DiffType::Modified;
          continue;
        }
      } catch (const fs::filesystem_error&) {
        it->second = DiffType::Modified; // If we can't get size, treat as modified
        continue;
      }
      work.push_back({ relativePath, pathA, pathB });
    } else {
      fileMap[relativePath] = DiffType::Added; // New file in dirB
    }
  }

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
            //bool identical = false;
            //if (options.useHash) {
            //  auto hashA = hashFileContents(job.pathA);
            //  auto hashB = hashFileContents(job.pathB);
            //  identical = (hashA && hashB && *hashA == *hashB);
            //  if (identical) {
            //    nofIdentical++;
            //  }
            //} else {
            //  // Treating as modified for simplicity.
            //  identical = false;
            //}
            //results[i] = identical ? DiffType::Identical : DiffType::Modified;
            auto df = diffFile(job.pathA, job.pathB, options);
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
    if (diffType != DiffType::Identical) {
      onDiffFound({ relativePath, diffType });
    }
  }

  return nofIdentical;
}
