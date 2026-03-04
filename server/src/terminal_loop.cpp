#include "terminal_loop.h"

#include "app.h"
#include "terminal_frontend.h"
#include "terminal_input.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <optional>
#include <utility>

/// @brief Extract pixel scroll delta from any scroll-type command.
static std::optional<std::pair<int, int>> scroll_delta(const RpcCommand& cmd, const CellSize& cell) {
  if (auto* ms = std::get_if<cmd::MouseScroll>(&cmd)) {
    return std::pair{ms->dx, ms->dy};
  }
  if (auto* sd = std::get_if<cmd::ScrollDown>(&cmd)) {
    return std::pair{0, cell.height * std::max(1, sd->count)};
  }
  if (auto* su = std::get_if<cmd::ScrollUp>(&cmd)) {
    return std::pair{0, -cell.height * std::max(1, su->count)};
  }
  if (auto* sl = std::get_if<cmd::ScrollLeft>(&cmd)) {
    return std::pair{-cell.width * std::max(1, sl->count), 0};
  }
  if (auto* sr = std::get_if<cmd::ScrollRight>(&cmd)) {
    return std::pair{cell.width * std::max(1, sr->count), 0};
  }
  return std::nullopt;
}

void run_terminal(App& app, TerminalFrontend& frontend, const KeyBindings& bindings, int scroll_lines) {
  app.initialize();
  TerminalInputHandler input_handler(bindings, scroll_lines);

  while (app.is_running()) {
    auto event = frontend.poll_input(100);
    if (!event) {
      app.idle_tick();
      continue;
    }

    spdlog::debug("input: id={} (0x{:x}) modifiers={} type={}", event->id, event->id, event->modifiers, static_cast<int>(event->type));

    auto client = frontend.client_info();
    auto cmd = input_handler.translate(*event, app.input_mode(), client.rows, client.cell);
    if (!cmd) {
      continue;
    }

    // Scroll coalescing: drain pending scroll events before rendering
    auto delta = scroll_delta(*cmd, client.cell);
    if (delta) {
      int dx = delta->first;
      int dy = delta->second;
      while (auto next = frontend.poll_input(0)) {
        auto next_cmd = input_handler.translate(*next, app.input_mode(), client.rows, client.cell);
        if (next_cmd) {
          auto next_delta = scroll_delta(*next_cmd, client.cell);
          if (next_delta) {
            dx += next_delta->first;
            dy += next_delta->second;
          }
          else {
            // Flush accumulated scroll, then handle the non-scroll command
            if (dx != 0 || dy != 0) {
              app.handle_command(cmd::MouseScroll{dx, dy});
              dx = 0;
              dy = 0;
            }
            app.handle_command(*next_cmd);
            break;
          }
        }
      }
      if (dx != 0 || dy != 0) {
        app.handle_command(cmd::MouseScroll{dx, dy});
      }
    }
    else if (auto* move = std::get_if<cmd::SelectionMove>(&*cmd)) {
      int dx = move->dx;
      int dy = move->dy;
      while (auto next = frontend.poll_input(0)) {
        auto next_cmd = input_handler.translate(*next, app.input_mode(), client.rows, client.cell);
        if (next_cmd) {
          if (auto* nm = std::get_if<cmd::SelectionMove>(&*next_cmd)) {
            dx += nm->dx;
            dy += nm->dy;
          }
          else {
            app.handle_command(cmd::SelectionMove{dx, dy});
            app.handle_command(*next_cmd);
            dx = 0;
            dy = 0;
            break;
          }
        }
      }
      if (dx != 0 || dy != 0) {
        app.handle_command(cmd::SelectionMove{dx, dy});
      }
    }
    else if (auto* drag = std::get_if<cmd::DragUpdate>(&*cmd)) {
      int col = drag->col;
      int row = drag->row;
      while (auto next = frontend.poll_input(0)) {
        auto next_cmd = input_handler.translate(*next, app.input_mode(), client.rows, client.cell);
        if (next_cmd) {
          if (auto* nd = std::get_if<cmd::DragUpdate>(&*next_cmd)) {
            col = nd->col;
            row = nd->row;
          }
          else {
            app.handle_command(cmd::DragUpdate{col, row});
            app.handle_command(*next_cmd);
            col = -1;
            break;
          }
        }
      }
      if (col >= 0) {
        app.handle_command(cmd::DragUpdate{col, row});
      }
    }
    else {
      app.handle_command(*cmd);
    }
  }
}
