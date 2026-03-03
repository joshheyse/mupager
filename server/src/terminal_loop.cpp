#include "terminal_loop.h"

#include "app.h"
#include "terminal_frontend.h"
#include "terminal_input.h"

#include <spdlog/spdlog.h>

void run_terminal(App& app, TerminalFrontend& frontend) {
  app.initialize();
  TerminalInputHandler input;

  while (app.is_running()) {
    auto event = frontend.poll_input(100);
    if (!event) {
      app.idle_tick();
      continue;
    }

    spdlog::debug("input: id={} (0x{:x}) modifiers={} type={}", event->id, event->id, event->modifiers, static_cast<int>(event->type));

    auto cmd = input.translate(*event, app.input_mode(), frontend.client_info().rows);
    if (cmd) {
      app.handle_command(*cmd);
    }
  }
}
