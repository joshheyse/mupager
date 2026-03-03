#pragma once

#include <string>
#include <vector>

/// @brief A single entry from a document's table of contents.
struct OutlineEntry {
  std::string title; ///< Section title.
  int page;          ///< Zero-based page number.
  int level;         ///< Nesting depth (0 = top-level).
};

/// @brief Flattened document outline (table of contents).
using Outline = std::vector<OutlineEntry>;
