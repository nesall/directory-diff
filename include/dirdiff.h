#pragma once

#include <filesystem>
#include <functional>

#include "filediff.h"


using DiffCallback = std::function<void(const FileDiff&)>;

size_t diffDirectories(
  const std::filesystem::path& dirA,
  const std::filesystem::path& dirB,
  const DiffOptions& options,
  DiffCallback onDiffFound);
