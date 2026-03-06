local M = {}

local log = require "mupager.log"
local config = {}
local mupager_bufnr = nil
local mupager_winid = nil
local saved_hl_ = {} -- original highlight groups to restore on close
local saved_wo_ = {} -- original window options to restore on close
local focus_timer = nil
local float_timer = nil
local opaque_ns = nil -- highlight namespace for float window bg override
local closing_ = false -- re-entry guard for close()

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
      if ev.buf == mupager_bufnr then return end
      local file = vim.api.nvim_buf_get_name(ev.buf)
      log.info("BufReadCmd: intercepted buf=%d file=%s", ev.buf, file)
      -- Set buftype immediately so no plugin or autocmd can re-trigger
      -- BufReadCmd on this buffer before the scheduled M.open runs.
      vim.bo[ev.buf].buftype = "nofile"
      -- Reuse ev.buf as the mupager buffer — keeps the real file path as the
      -- buffer name so Telescope cwd_only works, avoids mupager:// scheme
      -- issues (Snacks curl, E95 name conflicts).
      vim.schedule(function() M.open(file, ev.buf) end)
    end,
  })
end

--- Compute the luminance (0-1) of an integer color (0xRRGGBB).
--- @param color number
--- @return number
local function luminance(color)
  local r = math.floor(color / 0x10000) % 256
  local g = math.floor(color / 0x100) % 256
  local b = color % 256
  return (0.299 * r + 0.587 * g + 0.114 * b) / 255
end

--- Infer a contrasting bg from a fg color.
--- Light fg → dark neutral bg, dark fg → light neutral bg.
--- @param fg number Foreground color (0xRRGGBB).
--- @return number
local function infer_bg_from_fg(fg)
  local lum = luminance(fg)
  -- Scale to opposite end: light fg → ~10% brightness, dark fg → ~90%
  local level = math.floor((1 - lum) * 0.12 * 255 + 0.5)
  if lum <= 0.5 then level = math.floor((1 - (1 - lum) * 0.12) * 255 + 0.5) end
  return level * 0x10101
end

--- Find the Normal fg color from highlight groups.
--- @return number|nil
local function find_normal_fg()
  for _, name in ipairs { "Normal", "NormalFloat" } do
    local hl = vim.api.nvim_get_hl(0, { name = name, link = false })
    if hl.fg then return hl.fg end
  end
  return nil
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
  -- Fallback for transparent themes where no highlight group has an explicit bg.
  -- Set config.fallback_bg to your terminal's actual background color.
  if not bg then bg = config.fallback_bg end
  if not bg then
    local fg = find_normal_fg()
    if fg then bg = infer_bg_from_fg(fg) end
  end
  if not bg then return 0 end
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
--- @param reuse_buf number|nil Buffer handle to reuse (from BufReadCmd).
function M.open(file, reuse_buf)
  if not file or file == "" then
    vim.notify("[mupager] No file specified", vim.log.levels.ERROR)
    return
  end

  -- Resolve to absolute path
  file = vim.fn.fnamemodify(file, ":p")
  log.info("open: file=%s reuse_buf=%s", file, tostring(reuse_buf))

  -- Close existing mupager session
  if mupager_bufnr and vim.api.nvim_buf_is_valid(mupager_bufnr) then
    log.info("open: closing existing session buf=%d", mupager_bufnr)
    M.close()
  end

  -- Reuse the buffer from BufReadCmd if provided, otherwise create one.
  if reuse_buf and vim.api.nvim_buf_is_valid(reuse_buf) then
    mupager_bufnr = reuse_buf
  else
    mupager_bufnr = vim.api.nvim_create_buf(true, false)
    vim.api.nvim_buf_set_name(mupager_bufnr, file)
  end
  vim.bo[mupager_bufnr].buftype = "nofile"
  vim.bo[mupager_bufnr].bufhidden = "hide"
  vim.bo[mupager_bufnr].swapfile = false
  vim.bo[mupager_bufnr].modifiable = false
  vim.bo[mupager_bufnr].buflisted = true
  vim.bo[mupager_bufnr].filetype = "mupager"

  -- Switch to the buffer
  vim.api.nvim_set_current_buf(mupager_bufnr)
  mupager_winid = vim.api.nvim_get_current_win()

  -- Disable window decorations so the image fills the full window area.
  -- Save originals so we can restore them when mupager closes.
  local wo_overrides = {
    signcolumn = "no",
    number = false,
    relativenumber = false,
    foldcolumn = "0",
    statuscolumn = "",
    fillchars = "eob: ",
    statusline = "%{%v:lua.require('mupager').statusline()%}",
  }
  saved_wo_ = {}
  for k, _ in pairs(wo_overrides) do
    saved_wo_[k] = vim.wo[mupager_winid][k]
  end
  for k, v in pairs(wo_overrides) do
    vim.wo[mupager_winid][k] = v
  end

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
    fallback_fg = config.fallback_fg,
    fallback_bg = config.fallback_bg,
  })

  -- Set up keybindings
  require("mupager.commands").setup(mupager_bufnr, config)

  -- Register notification handlers
  local prev_link_hints_active = false
  server.on("state_changed", function(params)
    log.debug("notification: state_changed page=%s", tostring(params and params.current_page))
    -- Full redraw when link hints deactivate to clear hint labels from cells
    if prev_link_hints_active and params and not params.link_hints_active then vim.cmd "redraw" end
    prev_link_hints_active = params and params.link_hints_active or false
    vim.cmd "redrawstatus"
  end)

  server.on("flash", function(params)
    log.debug("notification: flash message=%s", tostring(params and params.message))
    if params and params.message then vim.notify(params.message, vim.log.levels.INFO) end
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

  -- Hide image when the mupager buffer leaves a window
  vim.api.nvim_create_autocmd("BufWinLeave", {
    group = augroup,
    buffer = mupager_bufnr,
    callback = function()
      if server.is_running() then server.notify "hide" end
    end,
  })

  -- Show image and re-apply window options when switching back to the mupager buffer
  vim.api.nvim_create_autocmd("BufWinEnter", {
    group = augroup,
    buffer = mupager_bufnr,
    callback = function()
      mupager_winid = vim.api.nvim_get_current_win()
      for k, v in pairs(wo_overrides) do
        vim.wo[mupager_winid][k] = v
      end
      if server.is_running() then
        server.send_resize(mupager_winid)
        server.notify "show"
      end
    end,
  })

  -- Hide/show when switching tmux windows. Kitty images persist at
  -- terminal-absolute coordinates across tmux window switches.
  -- Poll #{window_active} to detect switches — FocusLost/FocusGained
  -- aren't reliable for tmux keybinding-driven window cycling.
  local tmux_pane = vim.env.TMUX_PANE
  local focus_hidden = false

  local tmux_polling = false

  local function check_window_active()
    if not tmux_pane or tmux_polling or not server.is_running() then return end
    tmux_polling = true
    vim.system(
      { "tmux", "display-message", "-t", tmux_pane, "-p", "#{window_active}" },
      { text = true },
      vim.schedule_wrap(function(obj)
        tmux_polling = false
        if not server.is_running() then return end
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
    callback = function() M.close(true) end,
  })

  -- Send initial resize after a small delay to let the window settle
  vim.defer_fn(function()
    if mupager_winid and vim.api.nvim_win_is_valid(mupager_winid) then server.send_resize(mupager_winid) end
  end, 50)
end

--- Close the mupager session.
--- @param skip_buf_delete boolean|nil If true, skip buffer deletion (called from BufWipeout).
function M.close(skip_buf_delete)
  if closing_ then return end
  closing_ = true

  log.info "close: stopping server"
  stop_timers()
  local server = require "mupager.server"
  server.stop()

  pcall(vim.api.nvim_del_augroup_by_name, "mupager")
  restore_saved_hl()

  -- Restore window options so the window behaves normally for the next buffer.
  if mupager_winid and vim.api.nvim_win_is_valid(mupager_winid) then
    for k, v in pairs(saved_wo_) do
      pcall(function() vim.wo[mupager_winid][k] = v end)
    end
  end
  saved_wo_ = {}

  -- Only delete the buffer when close() is called directly (e.g. :MupagerClose).
  -- When called from BufWipeout, the buffer is already being wiped by Neovim —
  -- attempting to delete it again triggers E937.
  if not skip_buf_delete and mupager_bufnr and vim.api.nvim_buf_is_valid(mupager_bufnr) then
    pcall(vim.api.nvim_buf_delete, mupager_bufnr, { force = true })
  end

  opaque_ns = nil
  mupager_bufnr = nil
  mupager_winid = nil
  closing_ = false
  log.close()
end

--- Get the statusline string.
--- @return string
function M.statusline() return require("mupager.statusline").get() end

return M
