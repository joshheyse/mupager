--- Keybindings for mupager buffers.
local M = {}

local server = require "mupager.server"

--- Set up buffer-local keymaps for a mupager buffer.
--- @param bufnr number Buffer number.
--- @param opts table|nil User configuration options.
function M.setup(bufnr, opts)
  opts = opts or {}

  local function map(lhs, rhs, desc)
    vim.keymap.set("n", lhs, rhs, { buffer = bufnr, nowait = true, desc = "mupager: " .. desc })
  end

  local function cmd(name, extra)
    return function()
      if extra then
        server.notify("command", name, extra)
      else
        server.notify("command", name)
      end
    end
  end

  local function cmd_with_count(name)
    return function()
      local count = vim.v.count1
      server.notify("command", name, { count = count })
    end
  end

  -- Scrolling
  map("j", cmd_with_count "scroll_down", "Scroll down")
  map("k", cmd_with_count "scroll_up", "Scroll up")
  map("<Down>", cmd_with_count "scroll_down", "Scroll down")
  map("<Up>", cmd_with_count "scroll_up", "Scroll up")
  map("d", cmd "half_page_down", "Half page down")
  map("u", cmd "half_page_up", "Half page up")
  map("<C-f>", cmd "page_down", "Page down")
  map("<C-b>", cmd "page_up", "Page up")
  map("h", cmd_with_count "scroll_left", "Scroll left")
  map("l", cmd_with_count "scroll_right", "Scroll right")
  map("<Left>", cmd_with_count "scroll_left", "Scroll left")
  map("<Right>", cmd_with_count "scroll_right", "Scroll right")

  -- Navigation
  map("gg", function()
    local count = vim.v.count
    if count > 0 then
      server.notify("command", "goto_page", { page = count })
    else
      server.notify("command", "goto_first_page")
    end
  end, "First page / goto page")

  map("G", function()
    local count = vim.v.count
    if count > 0 then
      server.notify("command", "goto_page", { page = count })
    else
      server.notify("command", "goto_last_page")
    end
  end, "Last page / goto page")

  map("H", cmd "jump_back", "Jump back")
  map("L", cmd "jump_forward", "Jump forward")

  -- Zoom
  map("+", cmd "zoom_in", "Zoom in")
  map("=", cmd "zoom_in", "Zoom in")
  map("-", cmd "zoom_out", "Zoom out")
  map("0", cmd "zoom_reset", "Zoom reset")
  map("w", cmd "zoom_reset", "Fit width")

  -- View mode
  map("<Tab>", cmd "toggle_view_mode", "Toggle view mode")
  map("t", cmd "toggle_theme", "Toggle theme")

  -- Reload
  map("R", cmd "reload", "Reload document")

  -- Search
  map("/", function()
    vim.ui.input({ prompt = "/" }, function(term)
      if term and term ~= "" then server.notify("command", "search", { term = term }) end
    end)
  end, "Search")

  map("n", cmd "search_next", "Next match")
  map("N", cmd "search_prev", "Previous match")
  map("<Esc>", cmd "clear_search", "Clear search")

  -- Table of contents
  map("o", function() server.notify "get_outline" end, "Table of contents")

  -- Link hints
  map("f", function()
    server.notify("command", "enter_link_hints")

    -- Enter a char-capture loop
    vim.schedule(function()
      local function capture_key()
        local ok, ch = pcall(vim.fn.getcharstr)
        if not ok or ch == "\27" then
          -- Escape pressed
          server.notify("command", "link_hint_cancel")
          return
        end
        if ch:match "^%a$" then
          server.notify("link_key", { char = ch })
          -- Continue capturing if hints might need more chars
          vim.schedule(function()
            if server.state.link_hints_active then capture_key() end
          end)
        else
          server.notify("command", "link_hint_cancel")
        end
      end
      capture_key()
    end)
  end, "Link hints")

  -- Quit
  map("q", function()
    server.notify("command", "quit")
    require("mupager").close()
  end, "Quit")
end

return M
