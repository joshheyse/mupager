--- Job + RPC management for the mupager server process.
local M = {}

local log = require "mupager.log"

M.state = {}

local job_id = nil
local handlers = {}

--- Register a handler for server notifications.
--- @param method string
--- @param fn function
function M.on(method, fn) handlers[method] = fn end

--- Dispatch handler invoked by Neovim's rpc channel via nvim_exec_lua.
--- @param method string The notification method name.
--- @param params any The notification parameters.
function M._dispatch(method, params)
  log.debug("dispatch: method=%s", tostring(method))
  if method == "state_changed" and type(params) == "table" then M.state = params end
  local handler = handlers[method]
  if handler then handler(params) end
end

--- Start the mupager server process.
--- @param file string Path to the document file.
--- @param opts table|nil Optional arguments (view_mode, oversample, log_level).
function M.start(file, opts)
  opts = opts or {}
  if job_id then return end

  local cmd = { opts.bin or "mupager", "--mode", "neovim" }

  if opts.view_mode then
    table.insert(cmd, "--view-mode")
    table.insert(cmd, opts.view_mode)
  end
  if opts.oversample then
    table.insert(cmd, "--oversample")
    table.insert(cmd, tostring(opts.oversample))
  end
  if opts.log_level then
    table.insert(cmd, "--log-level")
    table.insert(cmd, opts.log_level)
  end

  table.insert(cmd, file)
  log.info("start: cmd=%s", table.concat(cmd, " "))

  job_id = vim.fn.jobstart(cmd, {
    rpc = true,
    on_stderr = function(_, data)
      for _, line in ipairs(data) do
        if line ~= "" then log.warn("stderr: %s", line) end
      end
    end,
    on_exit = function(_, code)
      log.info("exit: code=%d", code)
      job_id = nil
      if code ~= 0 then vim.notify("[mupager] server exited with code " .. code, vim.log.levels.WARN) end
    end,
    stderr_buffered = false,
  })

  if job_id <= 0 then
    log.error("start: jobstart failed (returned %d)", job_id)
    vim.notify("[mupager] failed to start server", vim.log.levels.ERROR)
    job_id = nil
  else
    log.info("start: job_id=%d", job_id)
  end
end

--- Send a notification to the server (no response expected).
--- @param method string RPC method name.
--- @param ... any Arguments to send.
function M.notify(method, ...)
  if not job_id then return end
  log.debug("send: method=%s", method)
  vim.rpcnotify(job_id, method, ...)
end

--- Send the current window size and position as a resize command.
--- @param winid number|nil Window ID (defaults to current window).
function M.send_resize(winid)
  winid = winid or vim.api.nvim_get_current_win()

  local width = vim.api.nvim_win_get_width(winid)
  local height = vim.api.nvim_win_get_height(winid)
  local pos = vim.fn.win_screenpos(winid)

  M.notify("resize", {
    cols = width,
    rows = height,
    offset_row = pos[1] - 1, -- 0-based
    offset_col = pos[2] - 1, -- 0-based
  })
end

--- Stop the server process.
function M.stop()
  if job_id then
    log.info("stop: sending quit, stopping job_id=%d", job_id)
    M.notify "quit"
    vim.fn.jobstop(job_id)
    job_id = nil
  end
  M.state = {}
end

--- Check if the server is running.
--- @return boolean
function M.is_running() return job_id ~= nil end

return M
