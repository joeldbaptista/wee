# wee cheatsheet

This is a quick reference for *implemented* functionality.

## modes

- **NORMAL**: navigation + operators
- **INSERT**: text entry
- **CMD**: ex command line (entered with `:`)
- **VISUAL**: select text (entered with `v`)

Mode switching:

- `i` → INSERT (insert at cursor)
- `a` → INSERT (append after cursor)
- `A` → INSERT (append at end of line)
- `o` → INSERT (open new line below)
- `O` → INSERT (open new line above)
- `C` → INSERT (change to end-of-line)
- `Esc` → NORMAL (steps cursor back one char if possible)
- `:` → CMD
- `v` → VISUAL (toggle)

## movement (motions)

Character / line:

- `h` / `j` / `k` / `l` — left/down/up/right
- `)` — next page (one screenful down)
- `(` — previous page (one screenful up)

Words:

- `w` — next word
- `b` — previous word
- `e` — end of word

Line boundaries:

- `0` — beginning of line
- `$` — end of line

File / line jumps:

- `gg` — first line
- `G` — last line
- `{n}G` — go to line *n*

Find on line:

- `t{char}` — move to *before* the next `{char}` on the current line
- `f{char}` — move to the next `{char}` on the current line

Search:

- `/{pattern}` — search forward (literal text)
- `n` — repeat search forward
- `N` — repeat search backward

Anchors (search + substitute):

- `^` at the start of `{pattern}` matches the beginning of a line
- `$` at the end of `{pattern}` matches the end of a line

In VISUAL mode, search moves the cursor and extends the selection.

Counts:

- Prefix most motions with a count, e.g. `10j`, `3w`, `2t)`.

## editing (NORMAL)

- `x` — delete character under cursor
- `u` — undo last change
- `p` — paste yanked/deleted text after cursor
- `dd` — delete (cut) line
- `yy` — yank (copy) line

## operators (NORMAL)

Operators combine with motions:

- `d{motion}` — delete (cut)
- `y{motion}` — yank (copy)
- `c{motion}` — change (delete then enter INSERT)

Examples:

- `dw`, `d2w`, `de`, `db`
- `d0`, `d$`
- `dgg`, `dG`, `d10G`
- `dt{char}` — delete until *before* `{char}` (same target as `t{char}`)
- `df{char}` — delete through `{char}`
- `yt{char}` — yank until *before* `{char}`
- `yf{char}` — yank through `{char}`
- `ct{char}` — change until *before* `{char}`
- `cf{char}` — change through `{char}`

Notes:

- `C` is equivalent to `c$`.

## text objects (inner)

When an operator is pending, `i{char}` selects the “inner” region for paired delimiters:

- `di{char}` — delete inside
- `yi{char}` — yank inside
- `ci{char}` — change inside

Supported pairs:

- `()` `[]` `{}` `<>`
- `''` `""`

Examples:

- `di(`, `ci{`, `yi"`

## insert mode notes

- Regular typing inserts bytes into the single text buffer.
- `Tab` inserts a literal `\t` (displayed with a fixed tabstop of 8).

## ex commands (CMD mode)

Enter CMD mode with `:` then type a command and press Enter.

Files:

- `:w` — write file
- `:q` — quit (fails if modified)
- `:q!` — quit without saving
- `:wq` — write then quit

Substitute (literal text, not regex):

Syntax:

- `:[address]s/old_text/new_text/` (optional trailing `g` flag)

Address components:

- `.` — current line
- `n` — line number `n`
- `.+m` / `.-m` — current line plus/minus `m` lines
- `$` — last line
- `/string/` — a line that contains `string`
- `%` — entire file
- `[addr1],[addr2]` — a range

Semantics:

- Without `g`: replace the **first** occurrence on **each** addressed line
- With `g`: replace **all** occurrences on **each** addressed line

Examples:

- `:.,.+10s/Banana/Kumquat` — first occurrence per line for 11 lines starting at `.`
- `:%s/apple/pear/g` — all occurrences per line for the whole file
- `:%s/.$//` — delete the last character of every line

VISUAL mode:

- In VISUAL mode, `:s/old/new/` uses the selected **line range** if you don't provide an explicit address.

Options:

- `:set nu` — show absolute line numbers
- `:set nonu` — hide line numbers
- `:set rnu` — show relative line numbers
- `:set nornu` — disable relative line numbers

External:

- `:run <script>` — run `<script>` and insert its stdout into the buffer after the cursor

## emergency

- `Ctrl-Q` — quit immediately
