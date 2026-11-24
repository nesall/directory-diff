#pragma once

#include <mutex>

// Guard disk mutations across directory operations and File Diff saves.
inline std::mutex fileMutationMutex;
