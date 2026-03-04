#include "terminal/loop.h"

#include "app.h"
#include "terminal/controller.h"
#include "terminal/frontend.h"

void run_terminal(App& app, TerminalFrontend& frontend, const KeyBindings& bindings, int scroll_lines) {
  TerminalController controller(app, frontend, bindings, scroll_lines);
  app.initialize();
  controller.initialize();

  while (app.is_running()) {
    auto event = frontend.poll_input(100);
    if (!event) {
      app.idle_tick();
      controller.idle_tick();
      continue;
    }

    controller.handle_input(*event);
  }
}
