#pragma once

#include "args.h"
#include "command.h"
#include "document.h"
#include "frontend.h"

#include <memory>

/// @brief Main application controller.
class App {
public:
  /// @brief Construct the application.
  /// @param frontend The display frontend to use.
  /// @param file Path to the document file.
  App(std::unique_ptr<Frontend> frontend, const Args& args);

  /// @brief Run the main event loop until quit.
  void run();

private:
  void handle_command(Command cmd);
  void render();
  void scroll(int dx, int dy);

  std::unique_ptr<Frontend> frontend_;
  Document doc_;
  bool running_ = true;
  int current_page_ = 0;
  float zoom_ = 1.0f;
  int scroll_x_ = 0;
  int scroll_y_ = 0;
};
