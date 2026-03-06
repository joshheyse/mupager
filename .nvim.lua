vim.opt.rtp:prepend(vim.fn.getcwd() .. "/nvim")
require("mupager").setup {
  bin = vim.fn.getcwd() .. "/build/debug/server/mupager",
  log_level = "debug",
  fallback_bg = 0x1a1b2c,
}
