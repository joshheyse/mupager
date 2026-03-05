#include "neovim/rpc_transport.hpp"
#include "action.hpp"
#include "action_traits.hpp"

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
#include <string>
#include <utility>
#include <msgpack/v3/sbuffer_decl.hpp>
#include <msgpack/v3/adaptor/adaptor_base_decl.hpp>

static constexpr size_t ReadBufSize = 65536;

/// @brief Extract an integer from a msgpack map by key, with a default value.
static int extract_map_int(const msgpack::object& map_obj, const char* key, int default_val) {
  if (map_obj.type != msgpack::type::MAP) {
    return default_val;
  }
  auto map = map_obj.via.map;
  for (uint32_t i = 0; i < map.size; ++i) {
    if (map.ptr[i].key.type == msgpack::type::STR && map.ptr[i].key.as<std::string>() == key) {
      return map.ptr[i].val.as<int>();
    }
  }
  return default_val;
}

/// @brief Extract a string from a msgpack map by key, with a default value.
static std::string extract_map_string(const msgpack::object& map_obj, const char* key, const std::string& default_val) {
  if (map_obj.type != msgpack::type::MAP) {
    return default_val;
  }
  auto map = map_obj.via.map;
  for (uint32_t i = 0; i < map.size; ++i) {
    if (map.ptr[i].key.type == msgpack::type::STR && map.ptr[i].key.as<std::string>() == key) {
      return map.ptr[i].val.as<std::string>();
    }
  }
  return default_val;
}

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

/// @brief Parse a "command" RPC call into an Action using compile-time lookup.
static std::optional<Action> parse_rpc_command(const msgpack::object& params) {
  if (params.type != msgpack::type::ARRAY || params.via.array.size < 1) {
    spdlog::warn("rpc: command expects array params");
    return std::nullopt;
  }
  auto* arr = params.via.array.ptr;
  std::string cmd_name = arr[0].as<std::string>();

  // Extract optional params map from second element
  const msgpack::object* args_map = nullptr;
  if (params.via.array.size >= 2 && arr[1].type == msgpack::type::MAP) {
    args_map = &arr[1];
  }

  // Try compile-time name lookup for simple (default-constructible) actions
  auto act = action_from_name(cmd_name);
  if (act) {
    // Some named actions need parameter extraction
    if (cmd_name == "scroll_down" && args_map) {
      return action::ScrollDown{extract_map_int(*args_map, "count", 1)};
    }
    if (cmd_name == "scroll_up" && args_map) {
      return action::ScrollUp{extract_map_int(*args_map, "count", 1)};
    }
    if (cmd_name == "scroll_left" && args_map) {
      return action::ScrollLeft{extract_map_int(*args_map, "count", 1)};
    }
    if (cmd_name == "scroll_right" && args_map) {
      return action::ScrollRight{extract_map_int(*args_map, "count", 1)};
    }
    return *act;
  }

  // Parameterized actions that don't have Name (not in variant lookup)
  if (cmd_name == "goto_page" && args_map) {
    int page = extract_map_int(*args_map, "page", 0);
    if (page > 0) {
      return action::GotoPage{page};
    }
  }
  if (cmd_name == "search" && args_map) {
    std::string term = extract_map_string(*args_map, "term", "");
    if (!term.empty()) {
      return action::Search{term};
    }
  }
  if (cmd_name == "set_view_mode" && args_map) {
    std::string mode = extract_map_string(*args_map, "mode", "");
    if (!mode.empty()) {
      return action::SetViewMode{mode};
    }
  }
  if (cmd_name == "set_theme" && args_map) {
    std::string theme = extract_map_string(*args_map, "theme", "");
    if (!theme.empty()) {
      return action::SetTheme{theme};
    }
  }
  if (cmd_name == "set_render_scale" && args_map) {
    std::string strategy = extract_map_string(*args_map, "strategy", "");
    if (!strategy.empty()) {
      return action::SetRenderScale{strategy};
    }
  }
  if (cmd_name == "reload") {
    return action::Reload{};
  }
  if (cmd_name == "link_hint_cancel") {
    return action::LinkHintCancel{};
  }

  spdlog::warn("rpc: unknown command '{}'", cmd_name);
  return std::nullopt;
}

std::optional<Action> RpcTransport::parse_action(const std::string& method, const msgpack::object& params) {
  if (method == "quit") {
    return action::Quit{};
  }

  if (method == "resize") {
    // Unwrap single-element array from msgpack-RPC: [arg] -> arg
    const auto& resize_params = (params.type == msgpack::type::ARRAY && params.via.array.size == 1) ? params.via.array.ptr[0] : params;
    if (resize_params.type != msgpack::type::MAP) {
      spdlog::warn("rpc: resize expects map params");
      return std::nullopt;
    }
    action::Resize r{};
    r.cols = extract_map_int(resize_params, "cols", 0);
    r.rows = extract_map_int(resize_params, "rows", 0);
    r.offset_row = extract_map_int(resize_params, "offset_row", 0);
    r.offset_col = extract_map_int(resize_params, "offset_col", 0);
    return r;
  }

  if (method == "command") {
    return parse_rpc_command(params);
  }

  if (method == "link_key") {
    // Unwrap single-element array from msgpack-RPC: [arg] -> arg
    const auto& link_params = (params.type == msgpack::type::ARRAY && params.via.array.size == 1) ? params.via.array.ptr[0] : params;
    std::string ch = extract_map_string(link_params, "char", "");
    if (!ch.empty()) {
      return action::LinkHintKey{ch[0]};
    }
    return std::nullopt;
  }

  if (method == "hide") {
    return action::Hide{};
  }
  if (method == "show") {
    return action::Show{};
  }
  if (method == "get_outline") {
    return action::GetOutline{};
  }
  if (method == "get_links") {
    return action::GetLinks{};
  }
  if (method == "get_state") {
    return action::GetState{};
  }

  spdlog::warn("rpc: unknown method '{}'", method);
  return std::nullopt;
}
