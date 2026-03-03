--- File-based logging for mupager.
--- Log file: stdpath("log")/mupager.log
local M = {}

local LEVELS = { DEBUG = 0, INFO = 1, WARN = 2, ERROR = 3 }
local LEVEL_NAMES = { [0] = "DEBUG", [1] = "INFO", [2] = "WARN", [3] = "ERROR" }

local log_level = LEVELS.INFO
local log_file = nil

--- Open the log file (lazy, on first write).
local function ensure_open()
  if log_file then return true end
  local dir = vim.fn.stdpath "log"
  vim.fn.mkdir(dir, "p")
  local path = dir .. "/mupager.log"
  log_file = io.open(path, "a")
  if not log_file then
    vim.notify("[mupager] failed to open log: " .. path, vim.log.levels.WARN)
    return false
  end
  return true
end

--- Set the minimum log level.
--- @param level string One of "debug", "info", "warn", "error".
function M.set_level(level)
  local l = LEVELS[(level or "info"):upper()]
  if l then log_level = l end
end

--- Write a log entry.
--- @param level number Log level constant.
--- @param fmt string Format string.
--- @param ... any Format arguments.
local function write(level, fmt, ...)
  if level < log_level then return end
  if not ensure_open() then return end
  local msg = string.format(fmt, ...)
  local ts = os.date "%Y-%m-%d %H:%M:%S"
  log_file:write(string.format("[%s] [%s] %s\n", ts, LEVEL_NAMES[level] or "?", msg))
  log_file:flush()
end

function M.debug(fmt, ...) write(LEVELS.DEBUG, fmt, ...) end
function M.info(fmt, ...) write(LEVELS.INFO, fmt, ...) end
function M.warn(fmt, ...) write(LEVELS.WARN, fmt, ...) end
function M.error(fmt, ...) write(LEVELS.ERROR, fmt, ...) end

--- Return the log file path.
--- @return string
function M.path() return vim.fn.stdpath "log" .. "/mupager.log" end

--- Close the log file.
function M.close()
  if log_file then
    log_file:close()
    log_file = nil
  end
end

return M
