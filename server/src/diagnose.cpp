#include "diagnose.hpp"

#include "terminal/osc_query.hpp"

#include <sys/ioctl.h>
#include <sys/utsname.h>
#include <unistd.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

/// @brief Read output from a shell command, trimming trailing newlines.
static std::string shell_exec(const char* cmd) {
  std::string result;
  FILE* pipe = popen(cmd, "r");
  if (!pipe) {
    return {};
  }
  std::array<char, 256> buf{};
  while (fgets(buf.data(), static_cast<int>(buf.size()), pipe)) {
    result += buf.data();
  }
  pclose(pipe);
  while (!result.empty() && (result.back() == '\n' || result.back() == '\r')) {
    result.pop_back();
  }
  return result;
}

/// @brief Get an environment variable or a fallback string.
static std::string env_or(const char* name, const char* fallback = "not set") {
  const char* v = std::getenv(name);
  return v ? v : fallback;
}

/// @brief Compute the config file path (mirrors config.cpp:config_path()).
static std::filesystem::path config_path() {
  const char* xdg = std::getenv("XDG_CONFIG_HOME");
  std::filesystem::path dir = xdg ? std::filesystem::path(xdg) : std::filesystem::path(std::getenv("HOME")) / ".config";
  return dir / "mupager" / "config.toml";
}

int run_diagnose() {
  // Version / build info
  std::string compiler = "unknown";
#if defined(__clang__)
  compiler = "clang " __clang_version__;
#elif defined(__GNUC__)
  compiler = "GCC " + std::to_string(__GNUC__) + "." + std::to_string(__GNUC_MINOR__) + "." + std::to_string(__GNUC_PATCHLEVEL__);
#endif
  while (!compiler.empty() && compiler.back() == ' ') {
    compiler.pop_back();
  }

  std::string build_type = MUPAGER_BUILD_TYPE;
  if (build_type.empty()) {
    build_type = "unknown";
  }

  std::printf("mupager v%s (%s, %s, %s)\n", MUPAGER_VERSION, MUPAGER_GIT_HASH, compiler.c_str(), build_type.c_str());

  // OS
  struct utsname uts{};
  if (uname(&uts) == 0) {
    std::printf("OS:         %s %s %s\n", uts.sysname, uts.release, uts.machine);
  }

  // Shell
  std::printf("Shell:      %s\n", env_or("SHELL").c_str());

  // Terminal
  std::string term = env_or("TERM", "unknown");
  std::string term_program = env_or("TERM_PROGRAM", "");
  std::string term_version = env_or("TERM_PROGRAM_VERSION", "");
  if (!term_program.empty()) {
    std::string terminal = term + " (" + term_program;
    if (!term_version.empty()) {
      terminal += " " + term_version;
    }
    terminal += ")";
    std::printf("Terminal:   %s\n", terminal.c_str());
  }
  else {
    std::printf("Terminal:   %s\n", term.c_str());
  }

  // Kitty
  const char* kitty_pid = std::getenv("KITTY_PID");
  if (kitty_pid) {
    std::printf("Kitty:      PID %s\n", kitty_pid);
  }
  else {
    std::printf("Kitty:      not detected\n");
  }

  // Tmux
  const char* tmux_env = std::getenv("TMUX");
  if (tmux_env && tmux_env[0] != '\0') {
    std::string tmux_ver = shell_exec("tmux -V 2>/dev/null");
    std::string passthrough = shell_exec("tmux show -gv allow-passthrough 2>/dev/null");
    std::printf(
        "Tmux:       %s (allow-passthrough: %s)\n", tmux_ver.empty() ? "active" : tmux_ver.c_str(), passthrough.empty() ? "unknown" : passthrough.c_str()
    );
  }
  else {
    std::printf("Tmux:       not active\n");
  }

  // Display
  if (isatty(STDOUT_FILENO)) {
    struct winsize ws{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
      if (ws.ws_xpixel > 0 && ws.ws_ypixel > 0) {
        int cell_w = ws.ws_col > 0 ? ws.ws_xpixel / ws.ws_col : 0;
        int cell_h = ws.ws_row > 0 ? ws.ws_ypixel / ws.ws_row : 0;
        std::printf("Display:    %dx%d cells, %dx%d px, %dx%d cell size\n", ws.ws_col, ws.ws_row, ws.ws_xpixel, ws.ws_ypixel, cell_w, cell_h);
      }
      else {
        std::printf("Display:    %dx%d cells (pixel size unavailable)\n", ws.ws_col, ws.ws_row);
      }
    }
  }
  else {
    std::printf("Display:    not a terminal\n");
  }

  // Terminal colors
  if (isatty(STDOUT_FILENO)) {
    auto colors = query_terminal_colors();
    if (colors) {
      std::printf(
          "Colors:     fg=#%02x%02x%02x bg=#%02x%02x%02x\n",
          colors->first.r,
          colors->first.g,
          colors->first.b,
          colors->second.r,
          colors->second.g,
          colors->second.b
      );
    }
    else {
      std::printf("Colors:     could not detect\n");
    }
  }
  else {
    std::printf("Colors:     not a terminal\n");
  }

  // Neovim
  const char* nvim = std::getenv("NVIM");
  if (nvim && nvim[0] != '\0') {
    std::printf("Neovim:     %s\n", nvim);
  }
  else {
    std::printf("Neovim:     not active\n");
  }

  // Config
  auto path = config_path();
  std::string home = env_or("HOME", "");
  std::string display_path = path.string();
  if (!home.empty() && display_path.find(home) == 0) {
    display_path = "~" + display_path.substr(home.size());
  }
  bool exists = std::filesystem::exists(path);
  std::printf("Config:     %s (%s)\n", display_path.c_str(), exists ? "exists" : "not found");

  return 0;
}
