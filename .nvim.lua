vim.opt.rtp:prepend(vim.fn.getcwd() .. "/nvim")
require("mupager").setup {
  bin = vim.fn.getcwd() .. "/build/server/mupager",
  log_level = "debug",
}
