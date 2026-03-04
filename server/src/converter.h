#pragma once

#include <map>
#include <optional>
#include <string>

/// @brief Result of a conversion attempt.
struct ConversionResult {
  std::string path;    ///< Path to open (original or converted temp file).
  std::string command; ///< The command that was used (for reconversion).
  bool is_temp;        ///< True if path is a temporary file needing cleanup.
};

/// @brief Find a matching converter command for a file path.
/// @param file_path Path to check against converter patterns.
/// @param converters Map of glob patterns to shell commands.
/// @param cli_override CLI --converter override (takes precedence).
/// @return The matching command, or std::nullopt if no match.
std::optional<std::string> find_converter(const std::string& file_path, const std::map<std::string, std::string>& converters, const std::string& cli_override);

/// @brief Convert a file using a shell command with placeholder substitution.
/// @param input_path Absolute path to the source file.
/// @param command Shell command template with %i, %o, %d placeholders.
/// @return ConversionResult with the temp output path.
/// @throws std::runtime_error if conversion fails.
ConversionResult convert(const std::string& input_path, const std::string& command);

/// @brief Reconvert to an existing temp path (for watch-mode).
/// @param input_path Absolute path to the source file.
/// @param output_path Existing temp file path to overwrite.
/// @param command Shell command template with %i, %o, %d placeholders.
/// @throws std::runtime_error if conversion fails.
void reconvert(const std::string& input_path, const std::string& output_path, const std::string& command);
