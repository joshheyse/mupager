--- Statusline component for mupager.
local M = {}

local server = require "mupager.server"

--- Get a formatted statusline string from the current server state.
--- @return string
function M.get()
  local s = server.state
  if not s or not s.current_page then return "" end

  local parts = {}

  -- Search info
  if s.search_term and s.search_term ~= "" then
    if s.search_current and s.search_current > 0 then
      table.insert(parts, string.format("/%s [%d/%d]", s.search_term, s.search_current, s.search_total))
    else
      table.insert(parts, string.format("/%s", s.search_term))
    end
  end

  -- View mode
  if s.view_mode then table.insert(parts, s.view_mode) end

  -- Zoom (only if not 100%)
  if s.zoom_percent and s.zoom_percent ~= 100 then table.insert(parts, string.format("%d%%", s.zoom_percent)) end

  -- Theme
  if s.theme then table.insert(parts, s.theme:upper()) end

  -- Page
  table.insert(parts, string.format("%d/%d", s.current_page, s.total_pages))

  return table.concat(parts, " | ")
end

return M
