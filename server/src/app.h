#pragma once

#include "command.h"
#include "document.h"
#include "frontend.h"

#include <memory>
#include <string>

/// @brief Main application controller.
class App {
public:
  /// @brief Construct the application.
  /// @param frontend The display frontend to use.
  /// @param file Path to the document file.
  App(std::unique_ptr<Frontend> frontend, const std::string& file);

  /// @brief Run the main event loop until quit.
  void run();

private:
  void handle_command(Command cmd);

  std::unique_ptr<Frontend> frontend_;
  Document doc_;
  bool running_ = true;
};
