#pragma once

class App;
class KeyBindings;
class TerminalFrontend;

/// @brief Run the terminal event loop.
/// @param bindings Key bindings for the input handler.
/// @param scroll_lines Lines per scroll step for the input handler.
void run_terminal(App& app, TerminalFrontend& frontend, const KeyBindings& bindings, int scroll_lines = 3);
