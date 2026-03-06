--- Job + RPC management for the mupager server process.
local M = {}

local log = require "mupager.log"

M.state = {}

local job_id = nil
local stopping_ = false
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
--- @param opts table|nil Optional arguments (view_mode, render_scale, log_level).
function M.start(file, opts)
  opts = opts or {}
  if job_id then return end

  local cmd = { opts.bin or "mupager", "--mode", "neovim" }

  if opts.view_mode then
    table.insert(cmd, "--view-mode")
    table.insert(cmd, opts.view_mode)
  end
  if opts.render_scale then
    table.insert(cmd, "--render-scale")
    table.insert(cmd, tostring(opts.render_scale))
  end
  if opts.log_level then
    table.insert(cmd, "--log-level")
    table.insert(cmd, opts.log_level)
  end
  if opts.show_stats then table.insert(cmd, "--show-stats") end
  if opts.watch then table.insert(cmd, "--watch") end
  if opts.converter then
    table.insert(cmd, "--converter")
    table.insert(cmd, opts.converter)
  end
  if opts.converters then
    for pattern, convert_cmd in pairs(opts.converters) do
      table.insert(cmd, "--converter-pattern")
      table.insert(cmd, pattern .. "=" .. convert_cmd)
    end
  end
  if opts.theme then
    table.insert(cmd, "--theme")
    table.insert(cmd, opts.theme)
  end
  if opts.scroll_lines then
    table.insert(cmd, "--scroll-lines")
    table.insert(cmd, tostring(opts.scroll_lines))
  end

  -- Pass Neovim's terminal colors to the server for theme/recolor detection.
  -- Falls back to config.fallback_bg/fg for transparent themes, then infers bg from fg.
  local hl = vim.api.nvim_get_hl(0, { name = "Normal", link = false })
  local fg = hl.fg or opts.fallback_fg
  local bg = hl.bg or opts.fallback_bg
  if not bg and fg then
    -- Infer bg from fg luminance: light fg → dark bg, dark fg → light bg
    local r = math.floor(fg / 0x10000) % 256
    local g = math.floor(fg / 0x100) % 256
    local b = fg % 256
    local lum = (0.299 * r + 0.587 * g + 0.114 * b) / 255
    local level = lum > 0.5 and math.floor((1 - lum) * 0.12 * 255 + 0.5)
      or math.floor((1 - (1 - lum) * 0.12) * 255 + 0.5)
    bg = level * 0x10101
  end
  if fg then
    table.insert(cmd, "--terminal-fg")
    table.insert(cmd, string.format("#%06x", fg))
  end
  if bg then
    table.insert(cmd, "--terminal-bg")
    table.insert(cmd, string.format("#%06x", bg))
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
    on_exit = function(id, code)
      log.info("exit: job=%d code=%d", id, code)
      -- Only clear job_id if it still matches this job. A rapid close+start
      -- cycle can cause the old job's on_exit to fire after a new job started;
      -- clobbering job_id would orphan the new server.
      if job_id == id then job_id = nil end
      -- 143 = SIGTERM (from jobstop), 141 = SIGPIPE — both expected during intentional shutdown.
      if code ~= 0 and not stopping_ then
        vim.notify("[mupager] server exited with code " .. code, vim.log.levels.WARN)
      end
      stopping_ = false
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
    stopping_ = true
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
