#pragma once

#include <filesystem>
#include <functional>


enum class DiffType { Added, Removed, Modified, Identical };

struct FileDiff {
  std::filesystem::path relativePath;
  DiffType type;
};

struct DiffOptions {
  bool useHash = true;
  bool ignoreHidden = false;
  bool includeIdentical = false;
  std::vector<std::string> ignorePatterns;
};

DiffType diffFile(
  const std::filesystem::path& a,
  const std::filesystem::path& b,
  const DiffOptions& options);



enum class EditType { Equal, Insert, Delete, Replace };

struct Line {
  int number;        // 1-based
  std::string text;  // without trailing newline
};

struct Hunk {
  EditType type;
  std::vector<Line> linesA;  // empty for Insert
  std::vector<Line> linesB;  // empty for Delete, populated for Replace
};

using DiffResult = std::vector<Hunk>;

std::vector<Line> loadLines(const std::filesystem::path& p);
DiffResult myersDiff(const std::vector<Line>& a, const std::vector<Line>& b);
void printUnifiedDiff(
  const DiffResult& diff, 
  const std::filesystem::path& pathA, 
  const std::filesystem::path& pathB, 
  int context = 3);