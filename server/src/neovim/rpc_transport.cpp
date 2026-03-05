#include "neovim/rpc_transport.hpp"
#include "command.hpp"

#include <poll.h>
#include <spdlog/spdlog.h>
#include <sys/poll.h>
#include <sys/_types/_ssize_t.h>
#include <unistd.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <msgpack/v3/object_fwd_decl.hpp>
#include <msgpack/v3/detail/cpp11_zone_decl.hpp>
#include <utility>
#include <msgpack/v3/sbuffer_decl.hpp>
#include <msgpack/v3/adaptor/adaptor_base_decl.hpp>

static constexpr size_t ReadBufSize = 65536;

/// @brief Parse a decoded msgpack array into an RpcMessage.
static std::optional<RpcMessage> parse_rpc_message(const msgpack::object& obj) {
  if (obj.type != msgpack::type::ARRAY || obj.via.array.size < 3) {
    spdlog::warn("rpc: malformed message (not an array or too short)");
    return std::nullopt;
  }

  auto* arr = obj.via.array.ptr;
  int type_val = arr[0].as<int>();

  RpcMessage msg;
  if (type_val == 0 && obj.via.array.size >= 4) {
    msg.type = RpcMessageType::Request;
    msg.msgid = arr[1].as<uint32_t>();
    msg.method = arr[2].as<std::string>();
    auto z = std::make_unique<msgpack::zone>();
    msgpack::object params_copy(arr[3], *z);
    msg.params = msgpack::object_handle(params_copy, std::move(z));
  }
  else if (type_val == 2 && obj.via.array.size >= 3) {
    msg.type = RpcMessageType::Notification;
    msg.method = arr[1].as<std::string>();
    auto z = std::make_unique<msgpack::zone>();
    msgpack::object params_copy(arr[2], *z);
    msg.params = msgpack::object_handle(params_copy, std::move(z));
  }
  else {
    spdlog::warn("rpc: unknown message type {}", type_val);
    return std::nullopt;
  }

  spdlog::debug("rpc recv: type={} method={}", type_val, msg.method);
  return msg;
}

RpcTransport::RpcTransport(int read_fd, int write_fd)
    : read_fd_(read_fd)
    , write_fd_(write_fd) {
  unpacker_.reserve_buffer(ReadBufSize);
}

std::optional<RpcMessage> RpcTransport::poll(int timeout_ms) {
  // First check if we already have a complete message buffered
  msgpack::object_handle oh;
  if (unpacker_.next(oh)) {
    return parse_rpc_message(oh.get());
  }

  // No buffered message — poll the fd
  struct pollfd pfd{};
  pfd.fd = read_fd_;
  pfd.events = POLLIN;

  int ret = ::poll(&pfd, 1, timeout_ms);
  if (ret <= 0) {
    return std::nullopt;
  }

  if ((pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
    spdlog::info("rpc: read fd closed or error");
    return std::nullopt;
  }

  // Read available data
  unpacker_.reserve_buffer(ReadBufSize);
  ssize_t n = ::read(read_fd_, unpacker_.buffer(), unpacker_.buffer_capacity());
  if (n <= 0) {
    spdlog::info("rpc: read returned {}", n);
    return std::nullopt;
  }
  unpacker_.buffer_consumed(static_cast<size_t>(n));

  // Try to parse again
  if (unpacker_.next(oh)) {
    return parse_rpc_message(oh.get());
  }

  return std::nullopt;
}

void RpcTransport::write_bytes(const char* data, size_t len) {
  size_t written = 0;
  while (written < len) {
    ssize_t n = ::write(write_fd_, data + written, len - written);
    if (n <= 0) {
      spdlog::error("rpc: write failed");
      return;
    }
    written += static_cast<size_t>(n);
  }
}

void RpcTransport::respond(uint32_t msgid, const msgpack::object& result) {
  msgpack::sbuffer buf;
  msgpack::packer<msgpack::sbuffer> pk(buf);
  pk.pack_array(4);
  pk.pack(1); // response type
  pk.pack(msgid);
  pk.pack_nil(); // no error
  pk.pack(result);
  write_bytes(buf.data(), buf.size());
}

void RpcTransport::respond_nil(uint32_t msgid) {
  msgpack::sbuffer buf;
  msgpack::packer<msgpack::sbuffer> pk(buf);
  pk.pack_array(4);
  pk.pack(1); // response type
  pk.pack(msgid);
  pk.pack_nil(); // no error
  pk.pack_nil(); // nil result
  write_bytes(buf.data(), buf.size());
}

void RpcTransport::notify(const std::string& method, const msgpack::object& params) {
  msgpack::sbuffer buf;
  msgpack::packer<msgpack::sbuffer> pk(buf);
  pk.pack_array(3);
  pk.pack(2); // notification type
  pk.pack(method);
  pk.pack(params);
  write_bytes(buf.data(), buf.size());
}

void RpcTransport::notify(const std::string& method, const msgpack::sbuffer& params_buf) {
  msgpack::sbuffer buf;
  msgpack::packer<msgpack::sbuffer> pk(buf);
  pk.pack_array(3);
  pk.pack(2); // notification type
  pk.pack(method);
  // Write the raw pre-packed params
  buf.write(params_buf.data(), params_buf.size());
  write_bytes(buf.data(), buf.size());
}

void RpcTransport::notify_nvim_lua(const std::string& method, const msgpack::sbuffer& params_buf) {
  static const std::string LuaCode = "require('mupager.server')._dispatch(...)";

  msgpack::sbuffer args;
  msgpack::packer<msgpack::sbuffer> apk(args);
  apk.pack_array(2);
  apk.pack(method);
  args.write(params_buf.data(), params_buf.size());

  msgpack::sbuffer outer;
  msgpack::packer<msgpack::sbuffer> pk(outer);
  pk.pack_array(2);
  pk.pack(LuaCode);
  outer.write(args.data(), args.size());

  notify("nvim_exec_lua", outer);
}

std::optional<Command> RpcTransport::parse_command(const std::string& method, const msgpack::object& params) {
  if (method == "quit") {
    return cmd::Quit{};
  }

  if (method == "resize") {
    // Unwrap single-element array from msgpack-RPC: [arg] -> arg
    const auto& resize_params = (params.type == msgpack::type::ARRAY && params.via.array.size == 1) ? params.via.array.ptr[0] : params;
    if (resize_params.type != msgpack::type::MAP) {
      spdlog::warn("rpc: resize expects map params");
      return std::nullopt;
    }
    cmd::Resize r{};
    auto map = resize_params.via.map;
    for (uint32_t i = 0; i < map.size; ++i) {
      auto& kv = map.ptr[i];
      if (kv.key.type != msgpack::type::STR) {
        continue;
      }
      std::string key = kv.key.as<std::string>();
      if (key == "cols") {
        r.cols = kv.val.as<int>();
      }
      else if (key == "rows") {
        r.rows = kv.val.as<int>();
      }
      else if (key == "offset_row") {
        r.offset_row = kv.val.as<int>();
      }
      else if (key == "offset_col") {
        r.offset_col = kv.val.as<int>();
      }
    }
    return r;
  }

  if (method == "command") {
    // params is an array: ["command_name", {optional args}]
    if (params.type != msgpack::type::ARRAY || params.via.array.size < 1) {
      spdlog::warn("rpc: command expects array params");
      return std::nullopt;
    }
    auto* arr = params.via.array.ptr;
    std::string cmd_name = arr[0].as<std::string>();

    // Extract optional count from second element (map with "count" key)
    int count = 1;
    if (params.via.array.size >= 2 && arr[1].type == msgpack::type::MAP) {
      auto map = arr[1].via.map;
      for (uint32_t i = 0; i < map.size; ++i) {
        if (map.ptr[i].key.type == msgpack::type::STR && map.ptr[i].key.as<std::string>() == "count") {
          count = map.ptr[i].val.as<int>();
        }
      }
    }

    if (cmd_name == "scroll_down") {
      return cmd::ScrollDown{count};
    }
    if (cmd_name == "scroll_up") {
      return cmd::ScrollUp{count};
    }
    if (cmd_name == "half_page_down") {
      return cmd::HalfPageDown{};
    }
    if (cmd_name == "half_page_up") {
      return cmd::HalfPageUp{};
    }
    if (cmd_name == "page_down") {
      return cmd::PageDown{};
    }
    if (cmd_name == "page_up") {
      return cmd::PageUp{};
    }
    if (cmd_name == "scroll_left") {
      return cmd::ScrollLeft{count};
    }
    if (cmd_name == "scroll_right") {
      return cmd::ScrollRight{count};
    }
    if (cmd_name == "goto_first_page") {
      return cmd::GotoFirstPage{};
    }
    if (cmd_name == "goto_last_page") {
      return cmd::GotoLastPage{};
    }
    if (cmd_name == "zoom_in") {
      return cmd::ZoomIn{};
    }
    if (cmd_name == "zoom_out") {
      return cmd::ZoomOut{};
    }
    if (cmd_name == "zoom_reset") {
      return cmd::ZoomReset{};
    }
    if (cmd_name == "toggle_view_mode") {
      return cmd::ToggleViewMode{};
    }
    if (cmd_name == "toggle_theme") {
      return cmd::ToggleTheme{};
    }
    if (cmd_name == "reload") {
      return cmd::Reload{};
    }
    if (cmd_name == "search_next") {
      return cmd::SearchNext{};
    }
    if (cmd_name == "search_prev") {
      return cmd::SearchPrev{};
    }
    if (cmd_name == "clear_search") {
      return cmd::ClearSearch{};
    }
    if (cmd_name == "jump_back") {
      return cmd::JumpBack{};
    }
    if (cmd_name == "jump_forward") {
      return cmd::JumpForward{};
    }
    if (cmd_name == "enter_link_hints") {
      return cmd::EnterLinkHints{};
    }
    if (cmd_name == "link_hint_cancel") {
      return cmd::LinkHintCancel{};
    }

    // Commands with string args in the params map
    if (cmd_name == "goto_page" && params.via.array.size >= 2 && arr[1].type == msgpack::type::MAP) {
      auto map = arr[1].via.map;
      for (uint32_t i = 0; i < map.size; ++i) {
        if (map.ptr[i].key.type == msgpack::type::STR && map.ptr[i].key.as<std::string>() == "page") {
          return cmd::GotoPage{map.ptr[i].val.as<int>()};
        }
      }
    }
    if (cmd_name == "search" && params.via.array.size >= 2 && arr[1].type == msgpack::type::MAP) {
      auto map = arr[1].via.map;
      for (uint32_t i = 0; i < map.size; ++i) {
        if (map.ptr[i].key.type == msgpack::type::STR && map.ptr[i].key.as<std::string>() == "term") {
          return cmd::Search{map.ptr[i].val.as<std::string>()};
        }
      }
    }
    if (cmd_name == "set_view_mode" && params.via.array.size >= 2 && arr[1].type == msgpack::type::MAP) {
      auto map = arr[1].via.map;
      for (uint32_t i = 0; i < map.size; ++i) {
        if (map.ptr[i].key.type == msgpack::type::STR && map.ptr[i].key.as<std::string>() == "mode") {
          return cmd::SetViewMode{map.ptr[i].val.as<std::string>()};
        }
      }
    }
    if (cmd_name == "set_theme" && params.via.array.size >= 2 && arr[1].type == msgpack::type::MAP) {
      auto map = arr[1].via.map;
      for (uint32_t i = 0; i < map.size; ++i) {
        if (map.ptr[i].key.type == msgpack::type::STR && map.ptr[i].key.as<std::string>() == "theme") {
          return cmd::SetTheme{map.ptr[i].val.as<std::string>()};
        }
      }
    }
    if (cmd_name == "set_render_scale" && params.via.array.size >= 2 && arr[1].type == msgpack::type::MAP) {
      auto map = arr[1].via.map;
      for (uint32_t i = 0; i < map.size; ++i) {
        if (map.ptr[i].key.type == msgpack::type::STR && map.ptr[i].key.as<std::string>() == "strategy") {
          return cmd::SetRenderScale{map.ptr[i].val.as<std::string>()};
        }
      }
    }

    spdlog::warn("rpc: unknown command '{}'", cmd_name);
    return std::nullopt;
  }

  if (method == "link_key") {
    // Unwrap single-element array from msgpack-RPC: [arg] -> arg
    const auto& link_params = (params.type == msgpack::type::ARRAY && params.via.array.size == 1) ? params.via.array.ptr[0] : params;
    if (link_params.type == msgpack::type::MAP) {
      auto map = link_params.via.map;
      for (uint32_t i = 0; i < map.size; ++i) {
        if (map.ptr[i].key.type == msgpack::type::STR && map.ptr[i].key.as<std::string>() == "char") {
          std::string ch = map.ptr[i].val.as<std::string>();
          if (!ch.empty()) {
            return cmd::LinkHintKey{ch[0]};
          }
        }
      }
    }
    return std::nullopt;
  }

  if (method == "hide") {
    return cmd::Hide{};
  }
  if (method == "show") {
    return cmd::Show{};
  }
  if (method == "get_outline") {
    return cmd::GetOutline{};
  }
  if (method == "get_links") {
    return cmd::GetLinks{};
  }
  if (method == "get_state") {
    return cmd::GetState{};
  }

  spdlog::warn("rpc: unknown method '{}'", method);
  return std::nullopt;
}
