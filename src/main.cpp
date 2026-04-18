#include <iostream>
#include <filesystem>
#include <3rdparty/CLI11.hpp>
#include <3rdparty/fmt/core.h>
#include <utils_log/logger.hpp>
#include "dirdiff.h"

namespace fs = std::filesystem;

#define DIRDIFF_APP_VERSION "1.0.0"

int main(int argc, char* argv[]) {

  // Command line:
  // mydiff --help
  // mydiff file-diff A/path/file.cpp B/path/file.cpp
  // mydiff dir-diff A/path B/path --ignore "build/;*.tmp"

  // USING CLI11

  CLI::App app{ "directory-diff utility" };
  app.set_version_flag("--version,-v",
    fmt::format("Directory-diff\nAuthor: Arman Sahakyan\nVersion: {}\nBuild date: {} {}", DIRDIFF_APP_VERSION, __DATE__, __TIME__));

  // Add file-diff subcommand
  auto* file_diff_cmd = app.add_subcommand("file-diff", "Compare two files");
  std::string file_a, file_b;
  file_diff_cmd->add_option("file_a", file_a, "First file path")->required();
  file_diff_cmd->add_option("file_b", file_b, "Second file path")->required();

  // Add dir-diff subcommand
  auto* dir_diff_cmd = app.add_subcommand("dir-diff", "Compare two directories");
  std::string dir_a, dir_b;
  dir_diff_cmd->add_option("dir_a", dir_a, "First directory path")->required();
  dir_diff_cmd->add_option("dir_b", dir_b, "Second directory path")->required();
  std::vector<std::string> ignoreList;
  dir_diff_cmd->add_option("--ignore", ignoreList, "Ignore patterns (semicolon-separated)")->delimiter(';');

  try {
    app.parse(argc, argv);
    if (*file_diff_cmd) {
      const fs::path fa = file_a, fb = file_b;
      if (!fs::exists(fa) || !fs::exists(fb)) {
        fmt::print(stderr, "One or both files do not exist.\n");
        return 1;
      }
      auto linesA = loadLines(fa);
      auto linesB = loadLines(fb);
      auto result = myersDiff(linesA, linesB);

      bool identical = std::all_of(result.begin(), result.end(),
        [](const Hunk& h) { return h.type == EditType::Equal; });

      if (identical) {
        fmt::print("Files are identical.\n");
      } else {
        printUnifiedDiff(result, fa, fb);
      }
    } else if (*dir_diff_cmd) {
      LOG_MSG << "Comparing directories:" << dir_a << "and" << dir_b;
      LOG_MSG << "Ignore patterns:" << (ignoreList.empty() ? std::string("None")
                                                          : fmt::format("{}", fmt::join(ignoreList, ", ")));
      std::map<char, std::string> ansiColors = {
        {'+', "32"},
        {'-', "31"},
        {'~', "33"},
        {' ', "0"}
      };
      DiffOptions options;
      options.useHash = true;
      options.ignorePatterns = ignoreList;
      diffDirectories(dir_a, dir_b, options, [&ansiColors](const FileDiff& diff) {
        char symbol = '?';
        switch (diff.type) {
        case DiffType::Added: symbol = '+'; break;
        case DiffType::Removed: symbol = '-'; break;
        case DiffType::Modified: symbol = '~'; break;
        case DiffType::Identical: symbol = ' '; break; // Should not be reported
        }
        if (symbol != ' ') {
          std::string color = "\033[" + ansiColors[symbol] + "m";
          std::cout << color << '[' << symbol << "] " << "\033[0m" << diff.relativePath.string() << std::endl;
        }
        });
    } else {
      std::cerr << "No subcommand specified. Use --help for usage information." << std::endl;
    }
  } catch (const CLI::ParseError& e) {
    return app.exit(e);
  }

  return 0;
}