#include "directory-diff/filediff.h"
#include <fstream>
#include <sstream>
#include <directory-diff/3rdparty/fmt/core.h>

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


std::vector<Line> loadLines(const fs::path& p) {
  std::ifstream ifs(p);
  if (!ifs) return {};

  std::vector<Line> lines;
  std::string raw;
  int n = 1;
  while (std::getline(ifs, raw)) {
    if (!raw.empty() && raw.back() == '\r')
      raw.pop_back();
    lines.push_back({ n++, std::move(raw) });
  }
  return lines;
}

DiffResult myersDiff(const std::vector<Line>& a, const std::vector<Line>& b) {
  const int N = (int)a.size();
  const int M = (int)b.size();
  const int MAX = N + M;

  if (MAX == 0) return {};

  // v[k] = furthest x reached on diagonal k
  // offset by MAX so we can index negatives
  std::vector<int> v(2 * MAX + 1, 0);

  // trace[d] = snapshot of v after d edits
  std::vector<std::vector<int>> trace;
  trace.reserve(MAX + 1);

  // Forward pass — record trace
  for (int d = 0; d <= MAX; ++d) {
    trace.push_back(v);
    for (int k = -d; k <= d; k += 2) {
      int x;
      if (k == -d || (k != d && v[k - 1 + MAX] < v[k + 1 + MAX]))
        x = v[k + 1 + MAX];      // move down (insert from b)
      else
        x = v[k - 1 + MAX] + 1;  // move right (delete from a)

      int y = x - k;
      while (x < N && y < M && a[x].text == b[y].text) {
        ++x; ++y;               // snake (equal)
      }
      v[k + MAX] = x;
      if (x >= N && y >= M) goto done;
    }
  }
done:;

  // Backtrack through trace to recover edit path
  struct Move { int x, y, px, py; };
  std::vector<Move> path;

  int x = N, y = M;
  for (int d = (int)trace.size() - 1; d > 0 && (x > 0 || y > 0); --d) {
    const auto& vv = trace[d];
    int k = x - y;

    int pk;
    if (k == -d || (k != d && vv[k - 1 + MAX] < vv[k + 1 + MAX]))
      pk = k + 1;  // came from down
    else
      pk = k - 1;  // came from right

    int px = vv[pk + MAX];
    int py = px - pk;

    // walk back over the snake
    while (x > px && y > py) {
      path.push_back({ x, y, x - 1, y - 1 });
      --x; --y;
    }
    // the actual edit move
    if (d > 0) path.push_back({ x, y, px, py });
    x = px; y = py;
  }
  std::reverse(path.begin(), path.end());

  // Build raw hunks (Equal/Insert/Delete only)
  std::vector<Hunk> raw;
  auto flush = [&](Hunk& h) {
    if (!h.linesA.empty() || !h.linesB.empty())
      raw.push_back(std::move(h));
    };

  Hunk cur;
  cur.type = EditType::Equal;

  for (const auto& m : path) {
    bool isEqual = (m.x - m.px == 1) && (m.y - m.py == 1);
    bool isDelete = (m.x - m.px == 1) && (m.y == m.py);
    // else insert

    EditType et = isEqual ? EditType::Equal
      : isDelete ? EditType::Delete
      : EditType::Insert;

    if (et != cur.type) { flush(cur); cur = Hunk{}; cur.type = et; }

    if (isEqual || isDelete) cur.linesA.push_back(a[m.px]);
    if (isEqual || !isDelete) cur.linesB.push_back(b[m.py]);
  }
  flush(cur);

  // Merge consecutive Delete+Insert into Replace
  DiffResult result;
  for (size_t i = 0; i < raw.size(); ++i) {
    if (raw[i].type == EditType::Delete &&
      i + 1 < raw.size() &&
      raw[i + 1].type == EditType::Insert) {
      result.push_back({
          EditType::Replace,
          std::move(raw[i].linesA),
          std::move(raw[i + 1].linesB)
        });
      ++i;
    } else {
      result.push_back(std::move(raw[i]));
    }
  }

  return result;
}

void printUnifiedDiff(const DiffResult& diff, const fs::path& pathA, const fs::path& pathB, int context)
{
  // Flatten diff into a sequence of tagged lines for easy windowing
  struct FlatLine {
    EditType type;
    Line     line;   // linesA for Delete/Replace/Equal, linesB for Insert/Replace
    bool     isB;    // true = came from B side
  };

  std::vector<FlatLine> flat;
  for (const auto& h : diff) {
    switch (h.type) {
    case EditType::Equal:
      for (const auto& l : h.linesA)
        flat.push_back({ EditType::Equal, l, false });
      break;
    case EditType::Delete:
      for (const auto& l : h.linesA)
        flat.push_back({ EditType::Delete, l, false });
      break;
    case EditType::Insert:
      for (const auto& l : h.linesB)
        flat.push_back({ EditType::Insert, l, true });
      break;
    case EditType::Replace:
      for (const auto& l : h.linesA)
        flat.push_back({ EditType::Delete, l, false });
      for (const auto& l : h.linesB)
        flat.push_back({ EditType::Insert, l, true });
      break;
    }
  }

  // Header
  fmt::print("--- {}\n", pathA.string());
  fmt::print("+++ {}\n", pathB.string());

  const int n = (int)flat.size();

  // Find changed line indices
  std::vector<int> changed;
  for (int i = 0; i < n; ++i)
    if (flat[i].type != EditType::Equal)
      changed.push_back(i);

  if (changed.empty()) return;

  // Group changed lines into windows with context
  std::vector<std::pair<int, int>> windows; // [start, end) in flat[]
  //int wStart = std::max(0, changed.front() - context);
  //int wEnd = std::min(n, changed.back() + context + 1);

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

  // Render each hunk
  for (const auto& [start, end] : windows) {
    // Compute @@ line numbers
    int lineA = 1, countA = 0, lineB = 1, countB = 0;
    bool firstA = true, firstB = true;
    for (int i = start; i < end; ++i) {
      const auto& fl = flat[i];
      if (fl.type != EditType::Insert) {
        if (firstA) { lineA = fl.line.number; firstA = false; }
        ++countA;
      }
      if (fl.type != EditType::Delete) {
        if (firstB) { lineB = fl.line.number; firstB = false; }
        ++countB;
      }
    }

    fmt::print("\033[36m@@ -{},{} +{},{} @@\033[0m\n",
      lineA, countA, lineB, countB);

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
    }
  }
}
