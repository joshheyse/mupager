local M = {}

local log = require "mupager.log"
local config = {}
local mupager_bufnr = nil
local mupager_winid = nil
local saved_hl_ = {} -- original highlight groups to restore on close
local focus_timer = nil
local float_timer = nil
local opaque_ns = nil -- highlight namespace for float window bg override

-- Default extensions: only binary/archive document formats that aren't text-editable.
-- Text-editable formats (HTML, SVG, images) require explicit config.patterns opt-in.
local default_patterns = {
  "*.pdf",
  "*.epub",
  "*.xps",
  "*.oxps",
  "*.cbz",
  "*.cbr",
  "*.fb2",
  "*.mobi",
}

local function stop_timers()
  if focus_timer then
    focus_timer:stop()
    focus_timer:close()
    focus_timer = nil
  end
  if float_timer then
    float_timer:stop()
    float_timer:close()
    float_timer = nil
  end
end

--- Setup mupager plugin.
--- @param opts table|nil Configuration options.
function M.setup(opts)
  local plugin_dir = vim.fn.fnamemodify(debug.getinfo(1, "S").source:sub(2), ":h:h:h")
  local doc_dir = plugin_dir .. "/doc"
  if vim.fn.isdirectory(doc_dir) == 1 then pcall(vim.cmd, "helptags " .. vim.fn.fnameescape(doc_dir)) end

  local file_config = require("mupager.config").load()
  config = vim.tbl_deep_extend("force", file_config, opts or {})
  if config.log_level then log.set_level(config.log_level) end
  log.info("setup: opts=%s", vim.inspect(config))

  vim.api.nvim_create_user_command(
    "MupagerOpen",
    function(cmd_opts) M.open(cmd_opts.args) end,
    { nargs = 1, complete = "file", desc = "Open a document in mupager" }
  )

  vim.api.nvim_create_user_command("MupagerClose", function() M.close() end, { desc = "Close mupager" })

  vim.api.nvim_create_user_command(
    "MupagerLog",
    function() vim.cmd("edit " .. log.path()) end,
    { desc = "Open mupager log file" }
  )

  local patterns = config.patterns or default_patterns

  vim.api.nvim_create_autocmd("BufReadCmd", {
    group = vim.api.nvim_create_augroup("mupager_filetype", { clear = true }),
    pattern = patterns,
    callback = function(ev)
      local file = vim.api.nvim_buf_get_name(ev.buf)
      log.info("BufReadCmd: intercepted buf=%d file=%s", ev.buf, file)
      -- Open mupager first so the window always has a buffer, then delete
      -- the intercepted buffer. Reversing this order can briefly leave the
      -- window empty, triggering neo-tree auto-close and quitting Neovim.
      vim.schedule(function()
        M.open(file)
        if vim.api.nvim_buf_is_valid(ev.buf) and ev.buf ~= mupager_bufnr then
          vim.api.nvim_buf_delete(ev.buf, { force = true })
        end
      end)
    end,
  })
end

--- Find a bg color for opaque float backgrounds, even when the theme is transparent.
--- Nudges the color by 1 unit to ensure it differs from the terminal's own default
--- bg — Kitty treats cells with its exact default bg as "transparent" for z-index
--- image placement (kitty #7563).
--- @return number
local function find_float_bg()
  local bg
  for _, name in ipairs { "NormalFloat", "Normal", "Pmenu", "CursorLine", "StatusLine" } do
    local hl = vim.api.nvim_get_hl(0, { name = name, link = false })
    if hl.bg then
      bg = hl.bg
      break
    end
  end
  bg = bg or (vim.o.background == "light" and 0xf0f0f0 or 0x1a1b2c)
  -- Nudge blue channel so the color never exactly matches the terminal bg
  local b = bg % 256
  return bg - b + (b < 255 and b + 1 or b - 1)
end

--- Override a highlight group to have an explicit bg, saving the original for restore.
--- Groups that are links are resolved and set directly to avoid winhl link issues.
--- @param name string Highlight group name.
--- @param bg number Background color.
local function force_hl_bg(name, bg)
  local raw = vim.api.nvim_get_hl(0, { name = name })
  if raw.link then
    -- Always break links — winhl doesn't follow link chains for bg rendering
    local resolved = vim.api.nvim_get_hl(0, { name = name, link = false })
    if not saved_hl_[name] then saved_hl_[name] = raw end
    local effective_bg = resolved.bg or bg
    log.debug("force_hl_bg: %s (link→%s) breaking link, bg=%s", name, raw.link, string.format("%#x", effective_bg))
    vim.api.nvim_set_hl(0, name, vim.tbl_extend("force", resolved, { bg = effective_bg }))
    return
  end
  -- Re-force if a plugin reset a group we previously overrode (lost our bg)
  if raw.bg and not saved_hl_[name] then return end
  if not saved_hl_[name] then saved_hl_[name] = raw end
  local effective_bg = raw.bg or bg
  log.debug("force_hl_bg: %s setting bg=%s", name, string.format("%#x", effective_bg))
  vim.api.nvim_set_hl(0, name, vim.tbl_extend("force", raw, { bg = effective_bg }))
end

--- Restore all highlight groups that were overridden.
local function restore_saved_hl()
  for name, hl in pairs(saved_hl_) do
    vim.api.nvim_set_hl(0, name, hl)
  end
  saved_hl_ = {}
end

--- Populate the opaque namespace with Normal/NormalFloat that have explicit bg.
--- Float windows get this namespace via nvim_win_set_hl_ns so that Normal cells
--- always have our bg, even if a plugin (Snacks, Telescope) resets winhl.
--- @param bg number Background color.
local function update_opaque_ns(bg)
  if not opaque_ns then opaque_ns = vim.api.nvim_create_namespace "mupager_opaque" end
  for _, name in ipairs { "Normal", "NormalFloat", "NormalNC", "EndOfBuffer" } do
    local hl = vim.api.nvim_get_hl(0, { name = name, link = false })
    hl.bg = bg
    vim.api.nvim_set_hl(opaque_ns, name, hl)
  end
end

--- Force opaque bg on every highlight group used by a floating window.
--- Sets winblend=0, forces bg on winhl destination groups, and applies
--- the opaque namespace so Normal cells always have our bg.
--- @param win number Window handle.
--- @param bg number Background color.
local function ensure_opaque_float(win, bg)
  if not vim.api.nvim_win_is_valid(win) then return end
  local cfg = vim.api.nvim_win_get_config(win)
  if not cfg.relative or cfg.relative == "" then return end

  vim.wo[win].winblend = 0
  vim.wo[win].cursorline = false

  -- Force bg on all winhl destination groups
  local winhl_str = vim.wo[win].winhl or ""
  for part in winhl_str:gmatch "[^,]+" do
    local _, dst = part:match "^(%w+):(.+)$"
    if dst then force_hl_bg(dst, bg) end
  end

  -- Apply our opaque namespace — this ensures Normal/NormalFloat cells
  -- always have our bg, even if the plugin resets winhl later.
  if opaque_ns then vim.api.nvim_win_set_hl_ns(win, opaque_ns) end

  log.debug("ensure_opaque_float: win=%d winhl='%s' ns=%s", win, winhl_str, tostring(opaque_ns))
end

--- Open a document in mupager.
--- @param file string Path to the document file.
function M.open(file)
  if not file or file == "" then
    vim.notify("[mupager] No file specified", vim.log.levels.ERROR)
    return
  end

  -- Resolve to absolute path
  file = vim.fn.fnamemodify(file, ":p")
  log.info("open: file=%s", file)

  -- Close existing mupager session
  if mupager_bufnr and vim.api.nvim_buf_is_valid(mupager_bufnr) then
    log.info("open: closing existing session buf=%d", mupager_bufnr)
    M.close()
  end

  -- Create a scratch buffer
  mupager_bufnr = vim.api.nvim_create_buf(false, true)
  vim.api.nvim_buf_set_option(mupager_bufnr, "buftype", "nofile")
  vim.api.nvim_buf_set_option(mupager_bufnr, "bufhidden", "wipe")
  vim.api.nvim_buf_set_option(mupager_bufnr, "swapfile", false)
  vim.api.nvim_buf_set_name(mupager_bufnr, "mupager://" .. vim.fn.fnamemodify(file, ":t"))

  -- Switch to the buffer
  vim.api.nvim_set_current_buf(mupager_bufnr)
  mupager_winid = vim.api.nvim_get_current_win()

  -- Disable window decorations so the image fills the full window area
  vim.wo[mupager_winid].signcolumn = "no"
  vim.wo[mupager_winid].number = false
  vim.wo[mupager_winid].relativenumber = false
  vim.wo[mupager_winid].foldcolumn = "0"
  vim.wo[mupager_winid].statuscolumn = ""
  vim.wo[mupager_winid].fillchars = "eob: "
  vim.wo[mupager_winid].statusline = "%{%v:lua.require('mupager').statusline()%}"

  -- Start the server
  local server = require "mupager.server"
  server.start(file, {
    bin = config.bin,
    view_mode = config.view_mode,
    render_scale = config.render_scale,
    log_level = config.log_level,
    show_stats = config.show_stats,
    theme = config.theme,
    scroll_lines = config.scroll_lines,
    watch = config.watch,
    converter = config.converter,
    converters = config.converters,
  })

  -- Set up keybindings
  require("mupager.commands").setup(mupager_bufnr, config)

  -- Register notification handlers
  server.on("state_changed", function(params)
    log.debug("notification: state_changed page=%s", tostring(params and params.current_page))
    vim.cmd "redrawstatus"
  end)

  server.on("flash", function(params)
    log.debug("notification: flash message=%s", tostring(params and params.message))
    if params and params.message then vim.notify(params.message, vim.log.levels.INFO) end
  end)

  server.on("link_hints", function(params)
    log.debug("notification: link_hints active=%s", tostring(params and params.active))
    if params and not params.active then vim.cmd "redraw" end
  end)

  server.on("outline", function(entries)
    if entries then vim.schedule(function() require("mupager.telescope").pick_outline(entries) end) end
  end)

  -- Set up autocmds
  local augroup = vim.api.nvim_create_augroup("mupager", { clear = true })
  local float_bg = find_float_bg()
  log.info("open: float_bg=%s", string.format("%#x", float_bg))
  update_opaque_ns(float_bg)

  -- With z < -1073741824 (INT32_MIN/2), Kitty renders images below cells
  -- that have non-default backgrounds. Force float-related highlight groups
  -- to have an explicit bg so floating windows cover the image.
  -- The bg must differ from Kitty's own default bg (kitty #7563).
  local float_groups = {
    "NormalFloat",
    "FloatBorder",
    "FloatTitle",
    "NoiceCmdlinePopup",
    "NoiceCmdlinePopupBorder",
    "NoiceCmdlinePopupTitle",
    "NoicePopup",
    "NoicePopupBorder",
    "TelescopeNormal",
    "TelescopeBorder",
    "TelescopePromptNormal",
    "TelescopePromptBorder",
    "TelescopeResultsNormal",
    "TelescopeResultsBorder",
    "TelescopePreviewNormal",
    "TelescopePreviewBorder",
  }
  for _, name in ipairs(float_groups) do
    force_hl_bg(name, float_bg)
  end
  vim.o.winblend = 0
  vim.o.pumblend = 0

  vim.api.nvim_create_autocmd({ "WinResized", "VimResized" }, {
    group = augroup,
    callback = function()
      if mupager_winid and vim.api.nvim_win_is_valid(mupager_winid) then server.send_resize(mupager_winid) end
    end,
  })

  -- Hide image when the mupager buffer is replaced in its window
  vim.api.nvim_create_autocmd("BufWinLeave", {
    group = augroup,
    buffer = mupager_bufnr,
    callback = function() server.notify "hide" end,
  })

  -- Hide/show when switching tmux windows. Kitty images persist at
  -- terminal-absolute coordinates across tmux window switches.
  -- Poll #{window_active} to detect switches — FocusLost/FocusGained
  -- aren't reliable for tmux keybinding-driven window cycling.
  local tmux_pane = vim.env.TMUX_PANE
  local focus_hidden = false

  local tmux_polling = false

  local function check_window_active()
    if not tmux_pane or tmux_polling then return end
    tmux_polling = true
    vim.system(
      { "tmux", "display-message", "-t", tmux_pane, "-p", "#{window_active}" },
      { text = true },
      vim.schedule_wrap(function(obj)
        tmux_polling = false
        local active = vim.trim(obj.stdout or "") == "1"
        if focus_hidden and active then
          focus_hidden = false
          server.notify "show"
        elseif not focus_hidden and not active then
          focus_hidden = true
          server.notify "hide"
        end
      end)
    )
  end

  local poll_interval = config.poll_interval or 100

  if tmux_pane then
    focus_timer = vim.uv.new_timer()
    focus_timer:start(poll_interval, poll_interval, vim.schedule_wrap(check_window_active))
  end

  vim.api.nvim_create_autocmd("FocusLost", {
    group = augroup,
    callback = function()
      if tmux_pane then return end -- tmux uses polling instead
      focus_hidden = true
      server.notify "hide"
    end,
  })

  vim.api.nvim_create_autocmd("FocusGained", {
    group = augroup,
    callback = function()
      if tmux_pane then return end -- tmux uses polling instead
      if focus_hidden then
        focus_hidden = false
        server.notify "show"
      end
    end,
  })

  -- Catch floating windows and force opaque backgrounds.
  -- A periodic timer is the most reliable approach — autocmd-based checks
  -- miss windows created with noautocmd (notifications, noice cmdline).
  float_timer = vim.uv.new_timer()
  float_timer:start(
    50,
    poll_interval,
    vim.schedule_wrap(function()
      for _, win in ipairs(vim.api.nvim_list_wins()) do
        ensure_opaque_float(win, float_bg)
      end
    end)
  )

  -- Re-apply highlight overrides after colorscheme changes
  vim.api.nvim_create_autocmd("ColorScheme", {
    group = augroup,
    callback = function()
      saved_hl_ = {}
      float_bg = find_float_bg()
      update_opaque_ns(float_bg)
      for _, name in ipairs(float_groups) do
        force_hl_bg(name, float_bg)
      end
    end,
  })

  vim.api.nvim_create_autocmd("BufWipeout", {
    group = augroup,
    buffer = mupager_bufnr,
    callback = function() M.close() end,
  })

  -- Send initial resize after a small delay to let the window settle
  vim.defer_fn(function()
    if mupager_winid and vim.api.nvim_win_is_valid(mupager_winid) then server.send_resize(mupager_winid) end
  end, 50)
end

--- Close the mupager session.
function M.close()
  log.info "close: stopping server"
  stop_timers()
  local server = require "mupager.server"
  server.stop()

  pcall(vim.api.nvim_del_augroup_by_name, "mupager")
  restore_saved_hl()

  if mupager_bufnr and vim.api.nvim_buf_is_valid(mupager_bufnr) then
    vim.api.nvim_buf_delete(mupager_bufnr, { force = true })
  end

  opaque_ns = nil
  mupager_bufnr = nil
  mupager_winid = nil
  log.close()
end

--- Get the statusline string.
--- @return string
function M.statusline() return require("mupager.statusline").get() end

return M
