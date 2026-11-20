# QZeditor

A small terminal text editor written in C. It started from the
[Kilo tutorial](https://viewsourcecode.org/snaptoken/kilo/) ("Build Your Own Text Editor")
and keeps growing with the "Growing Your Own Text Editor" continuation
(currently through chapter 13, Unicode).

## Features

- Raw-mode terminal editing with no dependencies beyond the C standard library and POSIX
- Open, edit and save files
- Safe saving: writes to a temporary file, `fsync`s it and `rename`s it over the original,
  keeps the file's permissions, and warns if the file changed on disk since it was opened
- Redraws correctly when the terminal window is resized
- Line-number gutter that grows with the file and highlights the cursor's line
- UTF-8 aware: moves and deletes by whole characters, handles wide (CJK) characters and
  combining marks, and shows invalid bytes visibly
- Word movement, start/end of file, smart Home, go to line, and a "sticky" column
  when moving up and down through shorter lines
- Incremental search (with arrow keys to jump between matches)
- Syntax highlighting for C/C++ (`.c`, `.h`, `.cpp`): keywords, types, strings, numbers and comments
- Status bar with filename, line count, modified flag and file type

## Build & Install

Requires `gcc` (or a compatible C99 compiler) and `make` on a POSIX system (Linux, macOS, WSL):

```sh
make                  # build ./qzeditor
make debug            # build ./qzeditor-debug with -g and AddressSanitizer/UBSan
make install          # install to /usr/local/bin/qzeditor (uses sudo)
make clean            # remove built binaries
```

The sanitizer build needs the ASan/UBSan runtime libraries (e.g. `libsanitizer` on most
distributions). You can override variables such as `CC` or `PREFIX` (default `/usr/local/bin`):

```sh
make PREFIX=$HOME/.local/bin install
```

## Usage

```sh
./qzeditor            # start with an empty buffer (or `qzeditor` if installed)
./qzeditor file.c     # open a file
```

## Keybindings

| Key            | Action                                  |
|----------------|-----------------------------------------|
| `Ctrl-S`       | Save                                    |
| `Ctrl-Q`       | Quit (press 3 times if there are unsaved changes) |
| `Ctrl-F`       | Find (Enter to confirm, Esc to cancel, arrows for next/previous) |
| `Ctrl-G`       | Go to line number                       |
| Arrow keys     | Move cursor (up/down remember the column) |
| `Ctrl-Left` / `Ctrl-Right` | Previous / next word        |
| `Home`         | First non-blank character, press again for column 0 |
| `End`          | End of line                             |
| `Ctrl-Home` / `Ctrl-End` | Start / end of file           |
| `Page Up/Down` | Scroll by a screen                      |

Line numbers are on by default; build with `make CFLAGS="-Wall -Wextra -std=c99 -DQZ_LINE_NUMBERS=0"`
to turn them off.
