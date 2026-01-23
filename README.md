# wee

`wee` is a minimal `vi`-like editor. It aims to stay small and dependency-free (closer in spirit to `busybox vi` than `vim`).

The editing model is intentionally simple: the file is stored as one contiguous byte buffer with `\n` line breaks.

This is the 2nd iteration of this project. The original project was a quick "tour de force" to implement something as simple as the busybox `vi`.
In this new version, all the modules are isolated in interface and implementation. 

Currently the underlying data structure to process the text is a simple dynamic array of characters, as in busybox `vi`.
However, I may change that just to make things more efficient.

The most important future change is the use of a simple parser to consume the commands inputted by the user. Currently that's a bit meggled into the code.

## build

```sh
make
./wee [file]
```

## quick start

- Start in **NORMAL** mode.
- Enter **INSERT** mode with `i`, `a`, `A`, `o`, `O`, or `C`.
- `Esc` returns to **NORMAL** (and steps the cursor back one character if possible).
- `:` opens the ex command line (**CMD** mode).
- `Ctrl-Q` quits immediately.

## implemented

- Modes: NORMAL / INSERT / CMD
- Modes: NORMAL / INSERT / VISUAL / CMD
- Raw terminal UI with status line
- Motions: `h j k l`, `w b e`, `0 $`, `gg`, `G`, `{n}G`, `t{char}`, `f{char}`
- Counts: `{n}{motion}` and `{n}{op}{motion}` where supported
- Operators: `d`, `y`, `c` with motions; plus `dd`, `yy`, `p`, `x`, `C`
- Undo: `u`
- Text objects (inner): `di{char}`, `yi{char}`, `ci{char}` for paired delimiters
- Ex commands: `:w`, `:q`, `:q!`, `:wq`
- Ex commands: `:run <script>` (insert stdout after cursor)
- Search: `/{pattern}` with `n`/`N`
- Search: `/{pattern}` with `n`/`N` (works in VISUAL too)
- Substitute: `:s/old/new/` and `:%s/old/new/g` (literal text; in VISUAL applies to selection)
- Options: `:set nu`, `:set nonu`, `:set rnu`, `:set nornu`
- Line number gutter: absolute and relative numbering
- Tabs: insert literal `\t`, render with fixed tabstop of 8
- Cursor shape: bar in INSERT and block in NORMAL/CMD (terminal support permitting)

## command reference

See [CHEATSHEET.md](CHEATSHEET.md) for the full list of implemented commands.

## notes / constraints

- This is intentionally not `vim`: no plugins, no split windows, no full compatibility goals.
- UTF-8 handling is “safe stepping” (avoid splitting continuation bytes); display width is still simplified.

## References

- [Write your own text editor](https://viewsourcecode.org/snaptoken/kilo/)
