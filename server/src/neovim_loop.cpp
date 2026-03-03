#include "neovim_loop.h"

#include "app.h"
#include "neovim_frontend.h"

#include <spdlog/spdlog.h>

#include <variant>

#include <msgpack.hpp>

static void send_state(RpcTransport& transport, const ViewState& state) {
  msgpack::sbuffer buf;
  msgpack::packer<msgpack::sbuffer> pk(buf);
  pk.pack_map(9);
  pk.pack("current_page");
  pk.pack(state.current_page);
  pk.pack("total_pages");
  pk.pack(state.total_pages);
  pk.pack("zoom_percent");
  pk.pack(state.zoom_percent);
  pk.pack("view_mode");
  pk.pack(state.view_mode);
  pk.pack("theme");
  pk.pack(state.theme);
  pk.pack("search_term");
  pk.pack(state.search_term);
  pk.pack("search_current");
  pk.pack(state.search_current);
  pk.pack("search_total");
  pk.pack(state.search_total);
  pk.pack("link_hints_active");
  pk.pack(state.link_hints_active);
  transport.notify_nvim_lua("state_changed", buf);
}

static void send_outline(RpcTransport& transport, const Outline& outline) {
  msgpack::sbuffer buf;
  msgpack::packer<msgpack::sbuffer> pk(buf);
  pk.pack_array(outline.size());
  for (const auto& entry : outline) {
    pk.pack_map(3);
    pk.pack("title");
    pk.pack(entry.title);
    pk.pack("page");
    pk.pack(entry.page + 1); // 1-based for plugin
    pk.pack("level");
    pk.pack(entry.level);
  }
  transport.notify_nvim_lua("outline", buf);
}

static void send_links(RpcTransport& transport, const std::vector<PageLink>& links) {
  msgpack::sbuffer buf;
  msgpack::packer<msgpack::sbuffer> pk(buf);
  pk.pack_array(links.size());
  for (const auto& link : links) {
    pk.pack_map(3);
    pk.pack("uri");
    pk.pack(link.uri);
    pk.pack("page");
    pk.pack(link.page + 1);
    pk.pack("dest_page");
    pk.pack(link.dest_page >= 0 ? link.dest_page + 1 : -1);
  }
  transport.notify_nvim_lua("links", buf);
}

void run_neovim(App& app, NeovimFrontend& frontend) {
  // Wait for the first resize from the plugin before initializing.
  // This ensures the window offset and dimensions are set correctly
  // before the first render.
  bool initialized = false;

  while (app.is_running()) {
    auto event = frontend.poll_input(100);
    if (!event) {
      if (initialized) {
        app.idle_tick();
      }
      continue;
    }

    while (auto cmd = frontend.pop_command()) {
      if (!initialized && std::holds_alternative<cmd::Resize>(*cmd)) {
        app.handle_command(*cmd);
        app.initialize();
        initialized = true;
        continue;
      }

      if (!initialized) {
        continue;
      }

      bool is_get_outline = std::holds_alternative<cmd::GetOutline>(*cmd);
      bool is_get_links = std::holds_alternative<cmd::GetLinks>(*cmd);

      app.handle_command(*cmd);

      if (is_get_outline) {
        send_outline(frontend.transport(), app.outline());
      }
      if (is_get_links) {
        send_links(frontend.transport(), app.visible_links());
      }
    }

    if (initialized) {
      send_state(frontend.transport(), app.view_state());
    }
  }
}
