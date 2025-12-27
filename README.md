# `wee` - an exercise in simplicity

`wee` is a very simple vi-clone in a **single** C file, and free of 3rd-party libraries. 
As such is it purposely restricted, and is more in tune with `busybox` vi than with `vim`. 

The fundamental data structure that holds the text is a single text buffer array.

## build

```sh
make
./wee [file]
```

## current status

Implemented (so far):

1. Raw terminal UI (screen redraw, status line)
2. Normal/insert/command modes
3. Basic UTF-8 *safe cursor stepping* (won't split codepoints on h/l and backspace)
4. Counts for motions (e.g. `4j`, `10k`, `3w`)
5. Word motions: `w`, `b`, `e`
6. Line motions: `0`, `$`, `gg`, `G`, `23G`
7. Operators (small subset): `dd`, `yy`, `p`, plus `d{motion}`, `y{motion}`, `c{motion}` for `w/b/e/0/$/gg/G/h/j/k/l`
8. Ex commands: `:w`, `:q`, `:q!`, `:wq`

## roadmap

Planned vi-ish features (non-exhaustive):

`wee` supports the following:

1. UTF-8 without depedencies
2. Character move with count (e.g. 4j, 10k, etc)
3. Word-based move with count (e.g. 4w, 8b, 3e, etc)
4. Move to begin and end of line (e.g. 0 begin and $ end)
4. Word-base delete (e.g. d3w), delete in (e.g. di(, di[, etc)
5. Goto to line (e.g. gg top, G bottom, 23G)
6. Cut between (e.g. ci(, ci[), cut till (e.g. ct), cti), cut till end (e.g. C)
7. Delete line (dd), yank line (yy), paste (p)

Amoung others...

## notes

- `wee` intentionally keeps the editing model simple: one contiguous byte buffer containing `\n`.
- This is an evolving exercise; behavior will drift toward classic vi where it makes sense.
