# wee v0.1

wee v0.1 is the first public cut of a tiny, dependency-free, vi-ish editor implemented in a single C file.
The whole file lives in one growable byte buffer (with `\n` line breaks), aiming for a Plan9/suckless-style codebase: small, readable, and hackable.

Highlights:

- NORMAL/INSERT/VISUAL/CMD modes with a simple full-screen terminal UI
- Classic vi-ish motions and operators (`d/y/c`, counts, `dd`, `yy`, `p`, `x`, `C`)
- Undo (`u`), `/` search with `n`/`N`, and literal `:s` / `:%s` substitute (VISUAL range supported)
- Line numbers (`:set nu`, `:set rnu`) and a fixed tabstop for `\t`
- `:run <script>` runs a shell command and inserts its stdout after the cursor

Known constraints (by design):

- No regex engine (search/substitute are literal)
- No redo, no split windows, and only charwise VISUAL
- UTF-8 handling avoids splitting bytes; display width is simplified
