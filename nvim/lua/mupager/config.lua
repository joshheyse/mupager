--- Config file loader for mupager.
--- Reads $XDG_CONFIG_HOME/mupager/config.toml (flat key-value only).
local M = {}

--- @return string
local function config_path()
  local xdg = vim.env.XDG_CONFIG_HOME
  local base = xdg and xdg or (vim.env.HOME .. "/.config")
  return base .. "/mupager/config.toml"
end

--- Parse a TOML value string into a Lua value.
--- @param raw string
--- @return string|number|boolean|nil
local function parse_value(raw)
  if raw == "true" then return true end
  if raw == "false" then return false end
  -- Quoted string
  local str = raw:match '^"(.-)"$'
  if str then return str end
  -- Number
  local num = tonumber(raw)
  if num then return num end
  return nil
end

--- Load config.toml and return a table of settings.
--- Returns an empty table if the file doesn't exist.
--- @return table
function M.load()
  local path = config_path()
  local file = io.open(path, "r")
  if not file then return {} end

  local result = {}
  for line in file:lines() do
    -- Strip comments
    line = line:gsub("#.*$", "")
    line = vim.trim(line)
    if line ~= "" then
      local key, val = line:match "^([%w%-]+)%s*=%s*(.+)$"
      if key and val then
        val = vim.trim(val)
        local parsed = parse_value(val)
        if parsed ~= nil then
          -- Convert TOML kebab-case to Lua snake_case
          local lua_key = key:gsub("%-", "_")
          result[lua_key] = parsed
        end
      end
    end
  end
  file:close()
  return result
end

return M
