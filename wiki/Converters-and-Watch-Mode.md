# Converters and Watch Mode

## Overview

mupager natively renders formats supported by MuPDF (PDF, EPUB, XPS, etc.). For other formats like Markdown, you can configure external converter commands that transform the source file into PDF before rendering.

Combined with watch mode, this enables a live-preview workflow: edit a document in one terminal, see it rendered and auto-updated in another.

## Configuration

### config.toml

```toml
[converters]
"*.md" = "pandoc %i -o %o"
"*.markdown" = "pandoc %i -o %o"
"*.rst" = "pandoc %i -o %o"
"*.tex" = "pdflatex -output-directory %d %i"
```

### CLI Flags

```bash
# Override converter for a single invocation
mupager --converter "pandoc %i -o %o" notes.md

# Add pattern-based converters
mupager --converter-pattern "*.md=pandoc %i -o %o" document.md
```

### Neovim Setup

```lua
require("mupager").setup({
  converter = "pandoc %i -o %o",  -- override for all files
  converters = {
    ["*.md"] = "pandoc %i -o %o",
    ["*.rst"] = "pandoc %i -o %o",
  },
})
```

## Placeholders

Converter commands support these placeholders:

| Placeholder | Description |
|-------------|-------------|
| `%i` | Absolute path to the input (source) file |
| `%o` | Path to the output file (a temp PDF that mupager will open) |
| `%d` | Path to the temp directory (for tools that need an output directory) |

## How Conversion Works

1. When mupager opens a file, it checks converter patterns against the filename
2. The CLI `--converter` flag takes priority over config file patterns
3. If a match is found, mupager:
   - Creates a temporary file for the output
   - Substitutes `%i`, `%o`, `%d` placeholders in the command
   - Runs the command as a shell process
   - Opens the resulting PDF for rendering
4. The temp file is cleaned up when mupager exits

## Watch Mode

Enable watch mode to automatically reload the document when the source file changes:

```bash
mupager --watch notes.md
```

Or in config.toml:

```toml
watch = true
```

When watch mode is active:

- mupager polls the source file's modification time
- When a change is detected:
  - If a converter is configured, the file is reconverted to the same temp path
  - The document is reloaded and re-rendered
- The current scroll position and view state are preserved across reloads

## Live Preview Example

Edit Markdown in one terminal, view the rendered PDF in another:

**Terminal 1 — edit:**

```bash
vim notes.md
```

**Terminal 2 — preview:**

```bash
mupager --converter "pandoc %i -o %o" --watch notes.md
```

Every time you save `notes.md`, the preview updates automatically.

### With Neovim

You can configure the plugin to handle Markdown files and auto-convert:

```lua
require("mupager").setup({
  patterns = { "*.pdf", "*.epub", "*.md" },
  converters = { ["*.md"] = "pandoc %i -o %o" },
  watch = true,
})
```

## See Also

- [[Configuration]] — `[converters]` section reference
- [[Command Line Reference]] — `--converter`, `--converter-pattern`, `--watch` flags
