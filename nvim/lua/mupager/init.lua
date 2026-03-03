local M = {}

local log = require "mupager.log"
local config = {}
local mupager_bufnr = nil
local mupager_winid = nil
local image_hidden = false

--- Setup mupager plugin.
--- @param opts table|nil Configuration options.
function M.setup(opts)
  config = opts or {}
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

  vim.api.nvim_create_autocmd("BufReadCmd", {
    group = vim.api.nvim_create_augroup("mupager_filetype", { clear = true }),
    pattern = "*.pdf",
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

--- Check if any floating window is currently visible.
--- @return boolean
local function has_floating_win()
  for _, win in ipairs(vim.api.nvim_list_wins()) do
    if vim.api.nvim_win_is_valid(win) then
      local cfg = vim.api.nvim_win_get_config(win)
      if cfg.relative and cfg.relative ~= "" then return true end
    end
  end
  return false
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

  -- Start the server
  local server = require "mupager.server"
  server.start(file, {
    bin = config.bin,
    view_mode = config.view_mode,
    oversample = config.oversample,
    log_level = config.log_level,
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

  -- Hide image when a floating window opens or cmdline activates (noice.nvim
  -- reuses floating windows so WinNew alone isn't enough).
  -- Show again when all floats are gone and cmdline is inactive.
  local function check_should_hide()
    vim.schedule(function()
      if has_floating_win() and not image_hidden then
        image_hidden = true
        server.notify "hide"
      end
    end)
  end

  local function check_should_show()
    vim.schedule(function()
      if not has_floating_win() and image_hidden then
        image_hidden = false
        server.notify "show"
      end
    end)
  end

  vim.api.nvim_create_autocmd({ "WinNew", "CmdlineEnter" }, {
    group = augroup,
    callback = check_should_hide,
  })

  vim.api.nvim_create_autocmd({ "WinClosed", "CmdlineLeave" }, {
    group = augroup,
    callback = check_should_show,
  })

  -- Also catch floats that are shown/hidden without creation/destruction
  vim.api.nvim_create_autocmd("WinEnter", {
    group = augroup,
    callback = function()
      local cfg = vim.api.nvim_win_get_config(0)
      if cfg.relative and cfg.relative ~= "" and not image_hidden then
        image_hidden = true
        server.notify "hide"
      end
    end,
  })

  vim.api.nvim_create_autocmd("WinLeave", {
    group = augroup,
    callback = check_should_show,
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
  local server = require "mupager.server"
  server.stop()

  pcall(vim.api.nvim_del_augroup_by_name, "mupager")

  if mupager_bufnr and vim.api.nvim_buf_is_valid(mupager_bufnr) then
    vim.api.nvim_buf_delete(mupager_bufnr, { force = true })
  end

  image_hidden = false
  mupager_bufnr = nil
  mupager_winid = nil
  log.close()
end

--- Get the statusline string.
--- @return string
function M.statusline() return require("mupager.statusline").get() end

return M
