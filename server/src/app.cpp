#include "app.h"

#include "input_event.h"

#include "stopwatch.h"

#include <spdlog/spdlog.h>

#include <algorithm>

App::App(std::unique_ptr<Frontend> frontend, const Args& args)
    : frontend_(std::move(frontend))
    , doc_(args.file) {
  spdlog::info("opened document: {} pages", doc_.page_count());
}

void App::render() {
  auto [pxy, pxx] = frontend_->pixel_size();
  auto [page_w, page_h] = doc_.page_size(current_page_);
  zoom_ = static_cast<float>(pxx) / page_w;

  {
    Stopwatch sw("mupdf render");
    auto pixmap = doc_.render_page(current_page_, zoom_);
    Stopwatch sw2("display (transmit)");
    frontend_->display(pixmap);
  }

  frontend_->show_region(scroll_x_, scroll_y_);
}

void App::run() {
  frontend_->clear();
  render();

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
    else if (event->id == 'j' && is_press) {
      handle_command(Command::SCROLL_DOWN);
    }
    else if (event->id == 'k' && is_press) {
      handle_command(Command::SCROLL_UP);
    }
    else if (event->id == 'd' && is_press) {
      handle_command(Command::HALF_PAGE_DOWN);
    }
    else if (event->id == 'u' && is_press) {
      handle_command(Command::HALF_PAGE_UP);
    }
    else if (event->id == input::RESIZE) {
      handle_command(Command::RESIZE);
    }
  }
}

void App::scroll(int dx, int dy) {
  auto [pxy, pxx] = frontend_->pixel_size();
  auto [page_w, page_h] = doc_.page_size(current_page_);
  int max_y = std::max(0, static_cast<int>(page_h * zoom_) - static_cast<int>(pxy));
  int max_x = std::max(0, static_cast<int>(page_w * zoom_) - static_cast<int>(pxx));
  int new_x = std::clamp(scroll_x_ + dx, 0, max_x);
  int new_y = std::clamp(scroll_y_ + dy, 0, max_y);
  if (new_x == scroll_x_ && new_y == scroll_y_) {
    return;
  }
  scroll_x_ = new_x;
  scroll_y_ = new_y;
  frontend_->show_region(scroll_x_, scroll_y_);
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
      render();
      break;
    }
    case Command::SCROLL_DOWN: {
      auto [celly, cellx] = frontend_->cell_size();
      scroll(0, static_cast<int>(celly));
      break;
    }
    case Command::SCROLL_UP: {
      auto [celly, cellx] = frontend_->cell_size();
      scroll(0, -static_cast<int>(celly));
      break;
    }
    case Command::HALF_PAGE_DOWN: {
      auto [pxy, pxx] = frontend_->pixel_size();
      scroll(0, static_cast<int>(pxy) / 2);
      break;
    }
    case Command::HALF_PAGE_UP: {
      auto [pxy, pxx] = frontend_->pixel_size();
      scroll(0, -static_cast<int>(pxy) / 2);
      break;
    }
  }
}
