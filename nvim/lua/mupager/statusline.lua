--- Statusline component for mupager.
local M = {}

local server = require "mupager.server"

--- Get a formatted statusline string from the current server state.
--- @return string
function M.get()
  local s = server.state
  if not s or not s.current_page then return "" end

  -- Left side: search info
  local left = ""
  if s.search_term and s.search_term ~= "" then
    if s.search_current and s.search_current > 0 then
      left = string.format(" /%s [%d/%d]", s.search_term, s.search_current, s.search_total)
    else
      left = string.format(" /%s", s.search_term)
    end
  end

  -- Right side: view mode, zoom, theme, page
  local right = {}
  if s.view_mode then table.insert(right, s.view_mode) end
  if s.zoom_percent and s.zoom_percent ~= 100 then table.insert(right, string.format("%d%%%%", s.zoom_percent)) end
  if s.theme then table.insert(right, s.theme:upper()) end
  table.insert(right, string.format("%d/%d", s.current_page, s.total_pages))

  -- Debug: cache stats
  if s.cache_pages and s.cache_pages ~= "" then
    local size
    if s.cache_bytes >= 1024 * 1024 then
      size = string.format("%.1fM", s.cache_bytes / (1024 * 1024))
    else
      size = string.format("%.0fK", s.cache_bytes / 1024)
    end
    table.insert(right, string.format("[%s] %s", s.cache_pages, size))
  end

  return left .. "%=" .. table.concat(right, " | ") .. " "
end

return M
