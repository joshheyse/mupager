#pragma once

/// Terminal query utilities.
namespace terminal {

/// @brief Pixel dimensions of a single terminal cell.
struct CellSize {
  int width_px;
  int height_px;
};

/// @brief Query the terminal cell size in pixels via TIOCGWINSZ ioctl on stdout.
/// @throws std::runtime_error if the ioctl fails or returns zero dimensions.
CellSize cell_size();

} // namespace terminal
