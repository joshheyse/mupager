#pragma once

#include "rpc_command.h"

#include <cstdint>
#include <optional>
#include <string>

#include <msgpack.hpp>

/// @brief Msgpack-RPC message types.
enum class RpcMessageType {
  REQUEST = 0,      ///< [0, msgid, method, params]
  RESPONSE = 1,     ///< [1, msgid, error, result]
  NOTIFICATION = 2, ///< [2, method, params]
};

/// @brief A parsed incoming RPC message.
struct RpcMessage {
  RpcMessageType type;
  uint32_t msgid = 0;            ///< Only valid for REQUEST/RESPONSE.
  std::string method;            ///< Method name (REQUEST/NOTIFICATION).
  msgpack::object_handle params; ///< Parameters (owned).
};

/// @brief Msgpack-RPC transport over file descriptors.
///
/// Reads from one fd (typically stdin), writes to another (typically stdout).
/// Handles partial reads via msgpack::unpacker and chunked protocol.
class RpcTransport {
public:
  /// @brief Construct with read and write file descriptors.
  /// @param read_fd File descriptor to read from.
  /// @param write_fd File descriptor to write to.
  RpcTransport(int read_fd, int write_fd);

  /// @brief Poll for an incoming message with a timeout.
  /// @param timeout_ms Timeout in milliseconds (-1 for blocking, 0 for non-blocking).
  /// @return A parsed RpcMessage, or nullopt on timeout.
  std::optional<RpcMessage> poll(int timeout_ms);

  /// @brief Send a response to a request.
  /// @param msgid The request message ID.
  /// @param result The result to send (will be packed with msgpack).
  void respond(uint32_t msgid, const msgpack::object& result);

  /// @brief Send a response with a nil result.
  /// @param msgid The request message ID.
  void respond_nil(uint32_t msgid);

  /// @brief Send a notification to the client.
  /// @param method Notification method name.
  /// @param params Parameters to send.
  void notify(const std::string& method, const msgpack::object& params);

  /// @brief Send a notification with a pre-packed sbuffer.
  /// @param method Notification method name.
  /// @param params_buf Pre-packed parameters buffer.
  void notify(const std::string& method, const msgpack::sbuffer& params_buf);

  /// @brief Send a notification to Neovim via nvim_exec_lua.
  ///
  /// Wraps the method and params in an nvim_exec_lua notification so Neovim
  /// dispatches to `require('mupager.server')._dispatch(...)`.
  /// @param method The logical method name (e.g. "state_changed").
  /// @param params_buf Pre-packed parameters buffer.
  void notify_nvim_lua(const std::string& method, const msgpack::sbuffer& params_buf);

  /// @brief Parse an RPC method + params into an RpcCommand.
  /// @param method The RPC method name.
  /// @param params The msgpack params object.
  /// @return The parsed command, or nullopt if unrecognized.
  static std::optional<RpcCommand> parse_command(const std::string& method, const msgpack::object& params);

private:
  int read_fd_;
  int write_fd_;
  msgpack::unpacker unpacker_;

  void write_bytes(const char* data, size_t len);
};
