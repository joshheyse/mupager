# Kitty Graphics Protocol

Reference for the Kitty graphics protocol as used in this project, with emphasis on
the tmux-compatible Unicode placeholder method.

## Escape Sequence Format

All graphics commands use APC (Application Programming Command) sequences:

```
\x1b_G<key=value,...>;<base64 payload>\x1b\\
```

- `\x1b_G` — APC introducer
- Control data — comma-separated `key=value` pairs
- `;` — separator (omit if no payload)
- Payload — base64-encoded pixel data
- `\x1b\\` — String Terminator (ST)

## Key Parameters

| Key | Default | Description |
|-----|---------|-------------|
| `a` | `t` | Action: `t` transmit, `T` transmit+display, `p` place, `d` delete |
| `t` | `d` | Medium: `d` direct (inline base64), `f` file, `s` shared memory |
| `f` | `32` | Pixel format: `24` RGB, `32` RGBA, `100` PNG |
| `s` | | Image width in pixels |
| `v` | | Image height in pixels |
| `i` | `0` | Image ID |
| `m` | `0` | More data: `1` = more chunks follow, `0` = final chunk |
| `q` | `0` | Quiet: `0` = send response, `2` = suppress all responses |
| `U` | `0` | Unicode placeholders: `1` = create virtual placement |
| `c` | | Columns for virtual placement grid |
| `r` | | Rows for virtual placement grid |
| `d` | `a` | Delete target: `i` = by image ID, `a` = all |

## Chunking

Large images are split into 4096-byte base64 chunks. The first chunk carries all
metadata; continuation chunks only need `m=`:

```
\x1b_Ga=T,t=d,f=24,s=800,v=600,i=1,m=1;[4096 bytes base64]\x1b\\
\x1b_Gm=1;[4096 bytes base64]\x1b\\
\x1b_Gm=0;[remaining base64]\x1b\\
```

Continuation chunks inherit parameters from the first chunk in the sequence.

## Direct Display (non-tmux)

When running directly in Kitty (no tmux), use `a=T` to transmit and display in one step:

```
\x1b_Ga=T,t=d,f=24,s=W,v=H,i=ID,m=0;[base64]\x1b\\
```

The image renders at the cursor position. No placeholder text is needed.

## Tmux: DCS Passthrough + Unicode Placeholders

Tmux intercepts APC sequences, so they cannot reach Kitty directly. The solution
has two parts: wrap graphics commands in DCS passthrough, and use Unicode placeholder
characters (plain text that tmux handles natively) to mark where images appear.

### Step 1: DCS Passthrough Wrapping

Each APC sequence is wrapped individually in a tmux DCS passthrough. Every `\x1b`
byte inside the sequence must be doubled:

```
\x1bPtmux;\x1b\x1b_G....\x1b\x1b\\\x1b\\
^         ^              ^         ^
DCS start doubled ESC    doubled   DCS ST
          in content     ST
```

Tmux recognizes doubled `\x1b\x1b` as escaped content. The first un-doubled
`\x1b\\` terminates the DCS. The inner content is un-doubled and forwarded to Kitty.

### Step 2: Transmit with Virtual Placement

Use `a=T,U=1` to transmit the image AND create a virtual placement in one command.
Include `q=2` to suppress responses (they can't travel back through tmux passthrough).
Specify the placement grid size with `c=` (columns) and `r=` (rows):

```
a=T,U=1,t=d,f=24,q=2,s=W,v=H,i=ID,c=COLS,r=ROWS,m=...
```

Grid dimensions are computed from pixel size and terminal cell size:

```
cols = ceil(image_width_px / cell_width_px)
rows = ceil(image_height_px / cell_height_px)
```

Cell pixel dimensions are queried via `TIOCGWINSZ` ioctl:

```c
struct winsize ws;
ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws);
cell_width_px  = ws.ws_xpixel / ws.ws_col;
cell_height_px = ws.ws_ypixel / ws.ws_row;
```

### Step 3: Unicode Placeholder Text

After the DCS-wrapped graphics commands, emit plain text that tmux passes through
to the pane. Kitty recognizes this text and composites the image on top.

Each cell is: `U+10EEEE` + row diacritic + column diacritic + image ID diacritic.

**U+10EEEE** — the placeholder base character (UTF-8: `F4 8E BB AE`).

**Diacritics** — combining characters from a lookup table that encode row index,
column index, and the most significant byte of the image ID. The table has 297
entries starting with:

```
0x0305, 0x030D, 0x030E, 0x0310, 0x0312, 0x033D, 0x033E, 0x033F, ...
```

Index 0 uses U+0305, index 1 uses U+030D, etc. Row and column indices map directly
to table indices.

**Foreground color** — encodes the lower 24 bits of the image ID. Uses
colon-separated SGR with bytes in MSB-first order:

```
\x1b[38:2:(id>>16)&0xFF:(id>>8)&0xFF:id&0xFF m
```

This is the colon sub-parameter form of 24-bit color (not the semicolon form).
The byte order matches kitten icat: the first color component carries the most
significant byte.

**Third diacritic** — encodes `(image_id >> 24) & 0xFF`, the most significant byte
of a 32-bit image ID. For IDs that fit in 24 bits (MSB = 0), the diacritic at index
0 (U+0305) is used.

**Row separators** — use `\n\r` (newline then carriage return) between rows so the
cursor returns to column 0 in tmux.

**Reset** — `\x1b[39m` after all placeholder text to restore the default foreground.

### Complete Output Sequence

```
[DCS-wrapped upload chunk 1]
[DCS-wrapped upload chunk 2]
...
[DCS-wrapped upload chunk N (m=0)]
\x1b[38:2:R:G:Bm          ← set fg to encode image_id
U+10EEEE RowDiac ColDiac IDDiac  U+10EEEE RowDiac ColDiac IDDiac ...  ← row 0
\n\r
U+10EEEE RowDiac ColDiac IDDiac  U+10EEEE RowDiac ColDiac IDDiac ...  ← row 1
\n\r
...
U+10EEEE RowDiac ColDiac IDDiac  U+10EEEE RowDiac ColDiac IDDiac ...  ← last row
\x1b[39m                   ← reset fg
```

## Deleting Images

Direct:
```
\x1b_Ga=d,d=i,i=ID\x1b\\
```

In tmux, wrap in DCS passthrough.

## Requirements

- **Kitty terminal** (or Ghostty) as the outer terminal — other terminals don't
  support Unicode placeholders.
- **tmux 3.3a+** with `allow-passthrough on` (`set -g allow-passthrough on`).
- Terminal must report pixel dimensions via `TIOCGWINSZ` for cell size calculation.

## References

- [Kitty Graphics Protocol](https://sw.kovidgoyal.net/kitty/graphics-protocol/)
- [Unicode Placeholders PR](https://github.com/kovidgoyal/kitty/pull/5664)
- [kitten icat source](https://github.com/kovidgoyal/kitty/blob/master/kittens/icat/transmit.go)
