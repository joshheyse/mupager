--- TOC picker using Telescope (with vim.ui.select fallback).
local M = {}

local server = require "mupager.server"

--- Open a fuzzy TOC picker.
--- @param outline table Array of {title, page, level} entries.
function M.pick_outline(outline)
  if not outline or #outline == 0 then
    vim.notify("[mupager] No outline", vim.log.levels.INFO)
    return
  end

  local has_telescope, _ = pcall(require, "telescope.pickers")

  if has_telescope then
    M._telescope_picker(outline)
  else
    M._vim_select_picker(outline)
  end
end

--- Telescope-based outline picker.
--- @param outline table
function M._telescope_picker(outline)
  local pickers = require "telescope.pickers"
  local finders = require "telescope.finders"
  local conf = require("telescope.config").values
  local actions = require "telescope.actions"
  local action_state = require "telescope.actions.state"

  local items = {}
  for _, entry in ipairs(outline) do
    local indent = string.rep("  ", entry.level or 0)
    local display = string.format("%s%s (p.%d)", indent, entry.title, entry.page)
    table.insert(items, {
      display = display,
      title = entry.title,
      page = entry.page,
      ordinal = entry.title,
    })
  end

  pickers
    .new({}, {
      prompt_title = "Table of Contents",
      finder = finders.new_table {
        results = items,
        entry_maker = function(item)
          return {
            value = item,
            display = item.display,
            ordinal = item.ordinal,
          }
        end,
      },
      sorter = conf.generic_sorter {},
      attach_mappings = function(prompt_bufnr)
        actions.select_default:replace(function()
          actions.close(prompt_bufnr)
          local selection = action_state.get_selected_entry()
          if selection then server.notify("command", { "goto_page", { page = selection.value.page } }) end
        end)
        return true
      end,
    })
    :find()
end

--- Fallback picker using vim.ui.select.
--- @param outline table
function M._vim_select_picker(outline)
  local items = {}
  local display_items = {}

  for _, entry in ipairs(outline) do
    local indent = string.rep("  ", entry.level or 0)
    table.insert(display_items, string.format("%s%s (p.%d)", indent, entry.title, entry.page))
    table.insert(items, entry)
  end

  vim.ui.select(display_items, {
    prompt = "Table of Contents",
  }, function(_, idx)
    if idx then server.notify("command", { "goto_page", { page = items[idx].page } }) end
  end)
end

return M
