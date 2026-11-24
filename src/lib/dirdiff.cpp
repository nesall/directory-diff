#include "directory-diff/dirdiff.h"
#include <algorithm>
#include <map>
#include <atomic>
#include <regex>
#include <string>
#include <thread>
#include <utility>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

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
  void scanEntries(const fs::path& root, const DiffOptions& options, const IgnoreMatcher& ignore,
    std::stop_token stop, DirectoryDiffProgress* progress, OnEntry onEntry) {
    if (stop.stop_requested()) return;
    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied), end;
    for (; it != end && !stop.stop_requested(); ++it) {
      if (progress) ++progress->scanned;
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

namespace {
  using Entries = std::map<fs::path, DiffEntryKind>;

#ifdef _WIN32
  struct CaseInsensitivePathLess {
    bool operator()(const fs::path& a, const fs::path& b) const
    {
      return CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE) == CSTR_LESS_THAN;
    }
  };

  void pairCaseAliases(const fs::path& leftRoot, const fs::path& rightRoot,
    std::vector<FileDiff>& diffs, Entries& unpairedRight, std::stop_token stop)
  {
    struct Candidates {
      std::vector<FileDiff*> left;
      std::vector<Entries::iterator> right;
    };
    std::map<fs::path, Candidates, CaseInsensitivePathLess> candidates;
    for (auto& diff : diffs) {
      if (stop.stop_requested()) return;
      if (diff.rightKind == DiffEntryKind::Missing)
        candidates[diff.relativePath].left.push_back(&diff);
    }
    for (auto right = unpairedRight.begin(); right != unpairedRight.end(); ++right) {
      if (stop.stop_requested()) return;
      candidates[right->first].right.push_back(right);
    }

    for (auto& [path, group] : candidates) {
      if (stop.stop_requested()) return;
      // Exact matches have priority. Ambiguous aliases stay separate.
      if (group.left.size() != 1 || group.right.size() != 1) continue;
      auto& left = *group.left.front();
      const auto right = group.right.front();
      std::error_code ignored;
      if (!fs::equivalent(leftRoot / left.relativePath, leftRoot / right->first, ignored) &&
          !fs::equivalent(rightRoot / left.relativePath, rightRoot / right->first, ignored))
        continue;
      left.rightRelativePath = right->first;
      left.rightKind = right->second;
      unpairedRight.erase(right);
    }
  }
#endif
}

size_t diffDirectories(
  const fs::path& dirA,
  const fs::path& dirB,
  const DiffOptions& options,
  DiffCallback onDiffFound)
{
  return *diffDirectories(dirA, dirB, options, std::move(onDiffFound), {});
}

std::optional<size_t> diffDirectories(
  const fs::path& dirA,
  const fs::path& dirB,
  const DiffOptions& options,
  DiffCallback onDiffFound,
  std::stop_token stop,
  DirectoryDiffProgress* progress)
{
  Entries left, right;
  const IgnoreMatcher ignore(options.ignorePatterns);
  scanEntries(dirA, options, ignore, stop, progress,
    [&](const fs::path& path, DiffEntryKind kind) { left.emplace(path, kind); });
  scanEntries(dirB, options, ignore, stop, progress,
    [&](const fs::path& path, DiffEntryKind kind) { right.emplace(path, kind); });
  if (stop.stop_requested()) return std::nullopt;
  if (progress) progress->phase = DirectoryDiffPhase::Matching;

  std::vector<FileDiff> entries;
  entries.reserve(left.size() + right.size());
  for (const auto& [path, kind] : left) {
    if (stop.stop_requested()) return std::nullopt;
    const auto match = right.find(path);
    entries.push_back({ path, DiffType::Removed, kind,
      match == right.end() ? DiffEntryKind::Missing : match->second });
    if (match != right.end())
      right.erase(match);
  }

  std::map<fs::path, fs::path> rightDirectoryAliases;
#ifdef _WIN32
  pairCaseAliases(dirA, dirB, entries, right, stop);
  for (const auto& entry : entries) {
    if (stop.stop_requested()) return std::nullopt;
    if (entry.rightKind == DiffEntryKind::Directory && !entry.rightRelativePath.empty())
      rightDirectoryAliases.emplace(entry.rightRelativePath, entry.relativePath);
  }
#endif

  for (const auto& [rightPath, kind] : right) {
    if (stop.stop_requested()) return std::nullopt;
    auto logicalPath = rightPath;
    for (auto parent = rightPath.parent_path(); !rightDirectoryAliases.empty() && !parent.empty(); parent = parent.parent_path()) {
      if (const auto alias = rightDirectoryAliases.find(parent); alias != rightDirectoryAliases.end()) {
        logicalPath = alias->second / rightPath.lexically_relative(parent);
        break;
      }
    }
    if (logicalPath != rightPath) {
      const auto originalLeftEnd = entries.begin() + left.size();
      const auto match = std::lower_bound(entries.begin(), originalLeftEnd, logicalPath,
        [](const FileDiff& entry, const fs::path& path) { return entry.relativePath < path; });
      if (match != originalLeftEnd && match->relativePath == logicalPath &&
          match->rightKind == DiffEntryKind::Missing) {
        match->rightKind = kind;
        match->rightRelativePath = rightPath;
        if (kind == DiffEntryKind::Directory)
          rightDirectoryAliases.emplace(rightPath, logicalPath);
        continue;
      }
    }
    entries.push_back({ logicalPath, DiffType::Added, DiffEntryKind::Missing, kind,
      logicalPath == rightPath ? fs::path{} : rightPath });
  }

  std::vector<FileDiff*> work;
  for (auto& entry : entries) {
    if (stop.stop_requested()) return std::nullopt;
    if (entry.leftKind == DiffEntryKind::Missing)
      entry.type = DiffType::Added;
    else if (entry.rightKind == DiffEntryKind::Missing)
      entry.type = DiffType::Removed;
    else if (entry.leftKind != entry.rightKind)
      entry.type = DiffType::Modified;
    else
      entry.type = DiffType::Identical;

    if ((entry.leftKind == DiffEntryKind::File && entry.rightKind == DiffEntryKind::File) ||
        (options.captureFileFingerprints &&
          (entry.leftKind == DiffEntryKind::File || entry.rightKind == DiffEntryKind::File)))
      work.push_back(&entry);
  }

  if (!work.empty()) {
    if (progress) {
      progress->filesToCompare = work.size();
      progress->phase = DirectoryDiffPhase::Comparing;
    }
    const auto numThreads = std::min<size_t>(std::max(1u, std::thread::hardware_concurrency()), work.size());
    std::atomic<size_t> next = 0;
    std::vector<std::jthread> threads;
    for (size_t t = 0; t < numThreads; ++t) {
      threads.emplace_back([&] {
        while (!stop.stop_requested()) {
          const auto index = next.fetch_add(1);
          if (index >= work.size()) break;
          auto& entry = *work[index];
          const auto pathA = dirA / entry.relativePath;
          const auto pathB = dirB / (entry.rightRelativePath.empty() ? entry.relativePath : entry.rightRelativePath);
          const bool bothFiles = entry.leftKind == DiffEntryKind::File && entry.rightKind == DiffEntryKind::File;
          if (options.captureFileFingerprints) {
            if (entry.leftKind == DiffEntryKind::File)
              entry.leftFingerprint = fingerprintFile(pathA, stop);
            if (entry.rightKind == DiffEntryKind::File)
              entry.rightFingerprint = fingerprintFile(pathB, stop);
            if (bothFiles && (!options.useHash || !entry.leftFingerprint ||
                entry.leftFingerprint != entry.rightFingerprint))
              entry.type = DiffType::Modified;
          } else {
            try {
              entry.type = diffFile(pathA, pathB, options, stop);
            } catch (const fs::filesystem_error&) {
              entry.type = DiffType::Modified;
            }
          }
          if (progress && !stop.stop_requested()) ++progress->compared;
        }
      });
    }
  }

  if (stop.stop_requested()) return std::nullopt;
  if (progress) progress->phase = DirectoryDiffPhase::Finalizing;
  std::sort(entries.begin(), entries.end(), [](const FileDiff& a, const FileDiff& b) {
    return a.relativePath < b.relativePath;
  });
  size_t identicalFiles = 0;
  for (const auto& entry : entries) {
    if (stop.stop_requested()) return std::nullopt;
    if (entry.type == DiffType::Identical && entry.leftKind == DiffEntryKind::File)
      ++identicalFiles;
    if (options.includeIdentical || entry.type != DiffType::Identical)
      onDiffFound(entry);
  }
  if (stop.stop_requested()) return std::nullopt;
  return identicalFiles;
}
