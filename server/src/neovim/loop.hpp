#pragma once

class App;
class NeovimFrontend;

/// @brief Run the Neovim event loop.
void run_neovim(App& app, NeovimFrontend& frontend);
