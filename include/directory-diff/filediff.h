#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>


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
enum class LineEnding { None, LF, CRLF, CR };

struct Line {
  int number;        // 1-based
  std::string text;  // without trailing newline
  LineEnding ending = LineEnding::LF;
};

struct TextFile {
  std::vector<Line> lines;
  bool utf8Bom = false;
};

struct Hunk {
  EditType type;
  std::vector<Line> linesA;  // empty for Insert
  std::vector<Line> linesB;  // empty for Delete, populated for Replace
};

using DiffResult = std::vector<Hunk>;

TextFile loadTextFile(const std::filesystem::path& path);
std::string textFileBytes(const TextFile& file);
std::vector<Line> loadLines(const std::filesystem::path& p);
DiffResult myersDiff(const std::vector<Line>& a, const std::vector<Line>& b);
void printUnifiedDiff(
  const DiffResult& diff, 
  const std::filesystem::path& pathA, 
  const std::filesystem::path& pathB, 
  int context = 3);
