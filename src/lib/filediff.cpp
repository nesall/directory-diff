#include "directory-diff/filediff.h"
#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>
#include <optional>
#include <span>
#include <stdexcept>
#include <sstream>
#include <directory-diff/3rdparty/fmt/core.h>
#include <directory-diff/3rdparty/fmt/std.h>
#include <directory-diff/3rdparty/utf8.h>

#define XXH_STATIC_LINKING_ONLY /* access advanced declarations */
#define XXH_IMPLEMENTATION      /* access definitions */
#include "directory-diff/3rdparty/xxhash.h"

namespace fs = std::filesystem;


namespace {

#if 0
  std::size_t hashFileContents(const fs::path& p) {
    std::ifstream ifs(p, std::ios::binary);
    if (!ifs) return 0;
    constexpr size_t bufSize = 64 * 1024;
    std::string buffer;
    buffer.resize(bufSize);
    std::size_t hash = 1469598103934665603ull; // FNV-1a offset
    while (true) {
      ifs.read(buffer.data(), static_cast<std::streamsize>(bufSize));
      std::streamsize n = ifs.gcount();
      if (n <= 0) break;

      const unsigned char* data = reinterpret_cast<const unsigned char*>(buffer.data());

      for (std::streamsize i = 0; i < n; ++i) {
        hash ^= data[i];
        hash *= 1099511628211ull;
      }
    }
    return hash;
  }
#else
  std::optional<uint64_t> hashFileContents(const fs::path& p, size_t fileSize) {
    std::ifstream ifs(p, std::ios::binary);
    if (!ifs) return std::nullopt;

    auto size = fileSize == 0 ? fs::file_size(p) : fileSize;

    // small/medium files -> one-shot
    if (size <= 1 * 1024 * 1024) { // 1MB threshold (tune this)
      std::vector<char> data(size);
      if (!ifs.read(data.data(), size)) return std::nullopt;
      return XXH3_64bits(data.data(), size);
    }

    // large files -> streaming
    XXH3_state_t* state = XXH3_createState();
    XXH3_64bits_reset(state);

    constexpr size_t bufSize = 64 * 1024;
    char buffer[bufSize];

    while (true) {
      ifs.read(buffer, bufSize);
      std::streamsize n = ifs.gcount();
      if (n <= 0) break;
      XXH3_64bits_update(state, buffer, static_cast<size_t>(n));
    }

    uint64_t hash = XXH3_64bits_digest(state);
    XXH3_freeState(state);

    return hash;
  }
#endif

} // anonymous namespace

DiffType diffFile(const fs::path& a, const fs::path& b, const DiffOptions& options)
{
  bool ba = fs::exists(a);
  bool bb = fs::exists(b);
  if (ba && !bb) return DiffType::Removed;
  if (!ba && bb) return DiffType::Added;
  if (!ba && !bb) return DiffType::Identical; // Both missing, treat as identical

  auto sizeA = fs::file_size(a);
  auto sizeB = fs::file_size(b);
  if (sizeA != sizeB) return DiffType::Modified;

  bool identical = false;
  if (options.useHash) {
    auto hashA = hashFileContents(a, sizeA);
    auto hashB = hashFileContents(b, sizeB);
    identical = (hashA && hashB && *hashA == *hashB);
  } else {
    // Treating as modified for simplicity.
    identical = false;
  }
  return identical ? DiffType::Identical : DiffType::Modified;
}


TextFile loadTextFile(const fs::path& path)
{
  std::error_code error;
  if (!fs::is_regular_file(path, error)) {
    throw fs::filesystem_error("Cannot open text file", path,
      error ? error : std::make_error_code(std::errc::invalid_argument));
  }

  std::ifstream input(path, std::ios::binary);
  if (!input)
    throw fs::filesystem_error("Cannot open text file", path, std::make_error_code(std::errc::io_error));

  std::string bytes;
  std::array<char, 64 * 1024> buffer;
  while (input.read(buffer.data(), buffer.size()) || input.gcount() > 0)
    bytes.append(buffer.data(), static_cast<size_t>(input.gcount()));
  if (input.bad() || !input.eof())
    throw fs::filesystem_error("Cannot read text file", path, std::make_error_code(std::errc::io_error));
  if (bytes.find('\0') != std::string::npos || !utf8::is_valid(bytes.begin(), bytes.end()))
    throw std::runtime_error("Expected a UTF-8 text file.");

  TextFile file;
  file.utf8Bom = bytes.starts_with("\xEF\xBB\xBF");
  size_t start = file.utf8Bom ? 3 : 0;
  for (int number = 1; start < bytes.size(); ++number) {
    const auto end = bytes.find_first_of("\r\n", start);
    if (end == std::string::npos) {
      file.lines.push_back({ number, bytes.substr(start), LineEnding::None });
      break;
    }
    const bool crlf = bytes[end] == '\r' && end + 1 < bytes.size() && bytes[end + 1] == '\n';
    const LineEnding ending = crlf ? LineEnding::CRLF : bytes[end] == '\r' ? LineEnding::CR : LineEnding::LF;
    file.lines.push_back({ number, bytes.substr(start, end - start), ending });
    start = end + (crlf ? 2 : 1);
  }
  return file;
}

std::string textFileBytes(const TextFile& file)
{
  std::string bytes = file.utf8Bom ? "\xEF\xBB\xBF" : "";
  for (auto it = file.lines.begin(); it != file.lines.end(); ++it) {
    const auto& line = *it;
    bytes += line.text;
    switch (line.ending) {
    case LineEnding::None: break;
    case LineEnding::LF: bytes += '\n'; break;
    case LineEnding::CRLF: bytes += "\r\n"; break;
    case LineEnding::CR:
      bytes += '\r';
      // An inserted empty LF line must stay separate from the preceding CR.
      if (const auto next = std::next(it); next != file.lines.end() && next->text.empty() && next->ending == LineEnding::LF)
        bytes += '\n';
      break;
    }
  }
  return bytes;
}

std::vector<Line> loadLines(const fs::path& path)
{
  return loadTextFile(path).lines;
}

namespace {
  bool sameLine(const Line& a, const Line& b)
  {
    return a.text == b.text && (a.ending == LineEnding::None) == (b.ending == LineEnding::None);
  }

  // The frontier at depth d stores diagonals -d, -d+2, ..., d.
  bool cameFromInsert(const std::vector<int>& previous, int depth, int diagonal)
  {
    return diagonal == -depth ||
      (diagonal != depth && previous[(diagonal + depth - 2) / 2] < previous[(diagonal + depth) / 2]);
  }

  std::vector<EditType> backtrack(const std::vector<std::vector<int>>& trace, int x, int y)
  {
    std::vector<EditType> edits;
    for (int depth = static_cast<int>(trace.size()) - 1; depth > 0; --depth) {
      const auto& previous = trace[depth - 1];
      const int diagonal = x - y;
      const bool inserted = cameFromInsert(previous, depth, diagonal);
      const int previousDiagonal = diagonal + (inserted ? 1 : -1);
      const int previousX = previous[(previousDiagonal + depth - 1) / 2];
      const int previousY = previousX - previousDiagonal;
      while (x > previousX && y > previousY) {
        edits.push_back(EditType::Equal);
        --x;
        --y;
      }
      edits.push_back(inserted ? EditType::Insert : EditType::Delete);
      x = previousX;
      y = previousY;
    }
    edits.insert(edits.end(), x, EditType::Equal);
    std::reverse(edits.begin(), edits.end());
    return edits;
  }

  std::optional<std::vector<EditType>> shortestEditPath(std::span<const Line> a, std::span<const Line> b)
  {
    // A large, dissimilar region can be displayed as a single change when the trace reaches this limit.
    constexpr size_t traceBudget = 4 * 1024 * 1024;
    size_t traceSize = 0;
    std::vector<std::vector<int>> trace;
    const int leftSize = static_cast<int>(a.size());
    const int rightSize = static_cast<int>(b.size());
    for (int depth = 0; depth <= leftSize + rightSize; ++depth) {
      traceSize += static_cast<size_t>(depth) + 1;
      if (traceSize > traceBudget) return std::nullopt;
      auto& frontier = trace.emplace_back(static_cast<size_t>(depth) + 1, 0);
      for (int diagonal = -depth; diagonal <= depth; diagonal += 2) {
        int x = 0;
        if (depth > 0) {
          const auto& previous = trace[depth - 1];
          x = cameFromInsert(previous, depth, diagonal)
            ? previous[(diagonal + depth) / 2]
            : previous[(diagonal + depth - 2) / 2] + 1;
        }
        int y = x - diagonal;
        while (x < leftSize && y < rightSize && sameLine(a[x], b[y])) {
          ++x;
          ++y;
        }
        frontier[(diagonal + depth) / 2] = x;
        if (x >= leftSize && y >= rightSize)
          return backtrack(trace, leftSize, rightSize);
      }
    }
    return std::nullopt;
  }
}

DiffResult myersDiff(const std::vector<Line>& a, const std::vector<Line>& b)
{
  const int leftSize = static_cast<int>(a.size());
  const int rightSize = static_cast<int>(b.size());
  int prefix = 0;
  while (prefix < leftSize && prefix < rightSize && sameLine(a[prefix], b[prefix]))
    ++prefix;
  int leftEnd = leftSize;
  int rightEnd = rightSize;
  while (leftEnd > prefix && rightEnd > prefix && sameLine(a[leftEnd - 1], b[rightEnd - 1])) {
    --leftEnd;
    --rightEnd;
  }

  DiffResult result;
  auto append = [&](bool equal, int leftBegin, int leftLimit, int rightBegin, int rightLimit) {
    if (leftBegin == leftLimit && rightBegin == rightLimit) return;
    const EditType type = equal ? EditType::Equal : leftBegin == leftLimit ? EditType::Insert
      : rightBegin == rightLimit ? EditType::Delete : EditType::Replace;
    result.push_back({ type,
      { a.begin() + leftBegin, a.begin() + leftLimit },
      { b.begin() + rightBegin, b.begin() + rightLimit } });
  };
  append(true, 0, prefix, 0, prefix);

  const auto leftMiddle = std::span(a).subspan(prefix, leftEnd - prefix);
  const auto rightMiddle = std::span(b).subspan(prefix, rightEnd - prefix);
  const auto edits = leftMiddle.empty() || rightMiddle.empty()
    ? std::nullopt : shortestEditPath(leftMiddle, rightMiddle);
  if (edits) {
    int left = prefix;
    int right = prefix;
    for (auto it = edits->begin(); it != edits->end();) {
      const bool equal = *it == EditType::Equal;
      const int leftBegin = left;
      const int rightBegin = right;
      do {
        if (*it != EditType::Insert) ++left;
        if (*it != EditType::Delete) ++right;
        ++it;
      } while (it != edits->end() && (*it == EditType::Equal) == equal);
      append(equal, leftBegin, left, rightBegin, right);
    }
  } else {
    append(false, prefix, leftEnd, prefix, rightEnd);
  }
  append(true, leftEnd, leftSize, rightEnd, rightSize);
  return result;
}

void printUnifiedDiff(const DiffResult& diff, const fs::path& pathA, const fs::path& pathB, int context)
{
  // Flatten diff into a sequence of tagged lines for easy windowing
  struct FlatLine {
    EditType type;
    const Line& line;
  };

  std::vector<FlatLine> flat;
  for (const auto& h : diff) {
    switch (h.type) {
    case EditType::Equal:
      for (const auto& l : h.linesA)
        flat.push_back({ EditType::Equal, l });
      break;
    case EditType::Delete:
      for (const auto& l : h.linesA)
        flat.push_back({ EditType::Delete, l });
      break;
    case EditType::Insert:
      for (const auto& l : h.linesB)
        flat.push_back({ EditType::Insert, l });
      break;
    case EditType::Replace:
      for (const auto& l : h.linesA)
        flat.push_back({ EditType::Delete, l });
      for (const auto& l : h.linesB)
        flat.push_back({ EditType::Insert, l });
      break;
    }
  }

  // Header
  fmt::print("--- {}\n", pathA);
  fmt::print("+++ {}\n", pathB);

  const int n = (int)flat.size();

  // Find changed line indices
  std::vector<int> changed;
  for (int i = 0; i < n; ++i)
    if (flat[i].type != EditType::Equal)
      changed.push_back(i);

  if (changed.empty()) return;

  // Group changed lines into windows with context
  std::vector<std::pair<int, int>> windows; // [start, end) in flat[]

  // Split into separate hunks where gap between changes > 2*context
  int prev = changed[0];
  int hStart = std::max(0, prev - context);
  for (int i = 1; i < (int)changed.size(); ++i) {
    if (changed[i] - prev > 2 * context + 1) {
      windows.push_back({ hStart, std::min(n, prev + context + 1) });
      hStart = std::max(0, changed[i] - context);
    }
    prev = changed[i];
  }
  windows.push_back({ hStart, std::min(n, prev + context + 1) });

  int cursor = 0;
  int leftLines = 0;
  int rightLines = 0;
  auto advanceTo = [&](int end) {
    while (cursor < end) {
      const auto type = flat[cursor++].type;
      if (type != EditType::Insert) ++leftLines;
      if (type != EditType::Delete) ++rightLines;
    }
  };
  for (const auto& [start, end] : windows) {
    advanceTo(start);
    const int leftBefore = leftLines;
    const int rightBefore = rightLines;
    advanceTo(end);
    const int countA = leftLines - leftBefore;
    const int countB = rightLines - rightBefore;

    fmt::print("\033[36m@@ -{},{} +{},{} @@\033[0m\n",
      leftBefore + (countA != 0), countA, rightBefore + (countB != 0), countB);

    for (int i = start; i < end; ++i) {
      const auto& fl = flat[i];
      switch (fl.type) {
      case EditType::Equal:
        fmt::print(" {}\n", fl.line.text);
        break;
      case EditType::Delete:
        fmt::print("\033[31m-{}\033[0m\n", fl.line.text);
        break;
      case EditType::Insert:
        fmt::print("\033[32m+{}\033[0m\n", fl.line.text);
        break;
      default: break;
      }
      if (fl.line.ending == LineEnding::None)
        fmt::print("\\ No newline at end of file\n");
    }
  }
}
