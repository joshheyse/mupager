#include "app.h"

#include <notcurses/nckeys.h>
#include <spdlog/spdlog.h>

App::App(std::unique_ptr<Frontend> frontend, const Args& args)
    : frontend_(std::move(frontend))
    , doc_(args.file) {
  spdlog::info("opened document: {} pages", doc_.page_count());
}

void App::run() {
  frontend_->clear();

  while (running_) {
    auto event = frontend_->poll_input(100);
    if (!event) {
      continue;
    }

    spdlog::debug("input: id={} (0x{:x}) modifiers={} type={}", event->id, event->id, event->modifiers, static_cast<int>(event->type));
    bool is_press = event->type == EventType::PRESS || event->type == EventType::UNKNOWN;
    if (event->id == 'q' && event->modifiers == 0 && is_press) {
      handle_command(Command::QUIT);
    }
    else if (event->id == NCKEY_RESIZE) {
      handle_command(Command::RESIZE);
    }
  }
}

void App::handle_command(Command cmd) {
  switch (cmd) {
    case Command::QUIT:
      spdlog::info("quit");
      running_ = false;
      break;
    case Command::RESIZE: {
      auto [pxy, pxx] = frontend_->pixel_size();
      auto [celly, cellx] = frontend_->cell_size();
      spdlog::info("resize: {}x{} px, {}x{} cell", pxx, pxy, cellx, celly);
      frontend_->clear();
      break;
    }
  }
}
