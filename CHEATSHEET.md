# wee cheatsheet

This is a quick reference for *implemented* functionality.

## modes

- **NORMAL**: navigation + operators
- **INSERT**: text entry
- **CMD**: ex command line (entered with `:`)

Mode switching:

- `i` → INSERT (insert at cursor)
- `a` → INSERT (append after cursor)
- `A` → INSERT (append at end of line)
- `o` → INSERT (open new line below)
- `O` → INSERT (open new line above)
- `C` → INSERT (change to end-of-line)
- `Esc` → NORMAL (steps cursor back one char if possible)
- `:` → CMD

## movement (motions)

Character / line:

- `h` / `j` / `k` / `l` — left/down/up/right

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
- `yt{char}` — yank until *before* `{char}`
- `ct{char}` — change until *before* `{char}`

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

Options:

- `:set nu` — show absolute line numbers
- `:set nonu` — hide line numbers
- `:set rnu` — show relative line numbers
- `:set nornu` — disable relative line numbers

## emergency

- `Ctrl-Q` — quit immediately
