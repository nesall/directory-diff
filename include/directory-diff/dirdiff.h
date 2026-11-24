#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <stop_token>

#include "filediff.h"


using DiffCallback = std::function<void(const FileDiff&)>;

enum class DirectoryDiffPhase { Scanning, Matching, Comparing, Finalizing };

struct DirectoryDiffProgress {
  std::atomic<DirectoryDiffPhase> phase{DirectoryDiffPhase::Scanning};
  std::atomic<size_t> scanned{0};
  std::atomic<size_t> compared{0};
  std::atomic<size_t> filesToCompare{0};
};

size_t diffDirectories(
  const std::filesystem::path& dirA,
  const std::filesystem::path& dirB,
  const DiffOptions& options,
  DiffCallback onDiffFound);

// Returns no count if stopped. Discard any delivered entries in that case.
// An optional, fresh progress object can be read from another thread during the call.
std::optional<size_t> diffDirectories(
  const std::filesystem::path& dirA,
  const std::filesystem::path& dirB,
  const DiffOptions& options,
  DiffCallback onDiffFound,
  std::stop_token stop,
  DirectoryDiffProgress* progress = nullptr);
