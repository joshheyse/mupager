#pragma once

class App;
class TerminalFrontend;

/// @brief Run the terminal event loop.
/// @param scroll_lines Lines per scroll step for the input handler.
void run_terminal(App& app, TerminalFrontend& frontend, int scroll_lines = 3);
