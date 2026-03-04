#include "converter.h"

#include <fnmatch.h>
#include <spdlog/spdlog.h>
#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <stdexcept>

/// @brief Shell-escape a path by wrapping in single quotes.
static std::string shell_escape(const std::string& path) {
  std::string escaped = "'";
  for (char c : path) {
    if (c == '\'') {
      escaped += "'\\''";
    }
    else {
      escaped += c;
    }
  }
  escaped += "'";
  return escaped;
}

/// @brief Substitute %i, %o, %d placeholders in a command template.
static std::string substitute(const std::string& cmd, const std::string& input, const std::string& output, const std::string& tmpdir) {
  std::string result;
  for (size_t i = 0; i < cmd.size(); ++i) {
    if (cmd[i] == '%' && i + 1 < cmd.size()) {
      switch (cmd[i + 1]) {
        case 'i':
          result += shell_escape(input);
          ++i;
          continue;
        case 'o':
          result += shell_escape(output);
          ++i;
          continue;
        case 'd':
          result += shell_escape(tmpdir);
          ++i;
          continue;
        default:
          break;
      }
    }
    result += cmd[i];
  }
  return result;
}

std::optional<std::string> find_converter(const std::string& file_path, const std::map<std::string, std::string>& converters, const std::string& cli_override) {
  if (!cli_override.empty()) {
    return cli_override;
  }

  auto filename = std::filesystem::path(file_path).filename().string();
  for (const auto& [pattern, command] : converters) {
    if (fnmatch(pattern.c_str(), filename.c_str(), FNM_CASEFOLD) == 0) {
      return command;
    }
  }
  return std::nullopt;
}

ConversionResult convert(const std::string& input_path, const std::string& command) {
  auto tmpdir = std::filesystem::temp_directory_path();
  auto tmp_template = (tmpdir / "mupager_XXXXXX").string();

  std::vector<char> buf(tmp_template.begin(), tmp_template.end());
  buf.push_back('\0');

  int fd = mkstemp(buf.data());
  if (fd < 0) {
    throw std::runtime_error("failed to create temp file");
  }
  close(fd);

  std::string tmp_path(buf.data());
  std::string pdf_path = tmp_path + ".pdf";
  std::filesystem::rename(tmp_path, pdf_path);

  auto full_cmd = substitute(command, input_path, pdf_path, tmpdir.string());
  spdlog::info("converter: running '{}'", full_cmd);

  int rc = std::system(full_cmd.c_str());
  if (rc != 0) {
    std::filesystem::remove(pdf_path);
    throw std::runtime_error("converter command failed with exit code " + std::to_string(rc));
  }

  return {pdf_path, command, true};
}

void reconvert(const std::string& input_path, const std::string& output_path, const std::string& command) {
  auto tmpdir = std::filesystem::path(output_path).parent_path().string();
  auto full_cmd = substitute(command, input_path, output_path, tmpdir);
  spdlog::info("converter: reconverting '{}'", full_cmd);

  int rc = std::system(full_cmd.c_str());
  if (rc != 0) {
    throw std::runtime_error("reconversion failed with exit code " + std::to_string(rc));
  }
}
