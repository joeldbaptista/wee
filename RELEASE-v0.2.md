# wee v0.2

wee v0.2 is a small step forward in vi-like ergonomics while keeping the single-file / single-buffer design.

Changes since v0.1:

- Anchors for search + substitute: `^` (beginning-of-line) and `$` (end-of-line)
  - Works for `/` + `n`/`N` and for `:s` / `:%s`.
  - Works inside VISUAL range substitution too.
- `:run <command>`: run a shell command and insert its stdout after the cursor
  - Uses `bash -c` when available (falls back to `sh -c`).
- Page motions in NORMAL mode:
  - `)` moves one screenful down
  - `(` moves one screenful up
  - These are real motions, so they also work with operators (e.g. `d)`).
- Fix: prevent a potential infinite loop / 100% CPU hang when running anchored, zero-length substitutions over a VISUAL selection (e.g. `:s/^/- /g`).

Notes / constraints (unchanged):

- Search/substitute are still literal (no regex engine).
- No redo; VISUAL is charwise only.
- UTF-8 handling avoids splitting bytes; display width remains simplified.
