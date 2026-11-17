# QZeditor

A small terminal text editor written in C, built by following the
[Kilo tutorial](https://viewsourcecode.org/snaptoken/kilo/) ("Build Your Own Text Editor").

## Features

- Raw-mode terminal editing with no dependencies beyond the C standard library and POSIX
- Open, edit and save files
- Incremental search (with arrow keys to jump between matches)
- Syntax highlighting for C/C++ (`.c`, `.h`, `.cpp`): keywords, types, strings, numbers and comments
- Status bar with filename, line count, modified flag and file type

## Build

Requires a C compiler and `make` on a POSIX system (Linux, macOS, WSL):

```sh
make
```

## Usage

```sh
./qzeditor            # start with an empty buffer
./qzeditor file.c     # open a file
```

## Keybindings

| Key            | Action                                  |
|----------------|-----------------------------------------|
| `Ctrl-S`       | Save                                    |
| `Ctrl-Q`       | Quit (press 3 times if there are unsaved changes) |
| `Ctrl-F`       | Find (Enter to confirm, Esc to cancel, arrows for next/previous) |
| Arrow keys     | Move cursor                             |
| `Home` / `End` | Start / end of line                     |
| `Page Up/Down` | Scroll by a screen                      |
