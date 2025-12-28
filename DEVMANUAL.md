# wee developer manual

This is a quick-reference for developers hacking on `wee.c`.

Functions are listed in **alphabetical order**. Descriptions focus on intent, parameters, side effects on global editor state `E`, and return behavior.


---

## applymotion

- Signature: `static void applymotion(int key) {`
- Params: int key
- Purpose: Resolve a motion (honoring counts) and apply any pending operator (`d`/`y`/`c`).
- Effects: Moves `E.cur`; may read extra keys for multi-key motions (`g`, `t{char}`, `f{char}`); if an operator is pending, mutates `E.buf`, `E.yank`, `E.dirty`, and may enter INSERT for `c`.
- Result: None.

## applytextobjinner

- Signature: `static void applytextobjinner(int ch) {`
- Params: int ch
- Purpose: Implement operator-pending `i{char}` by selecting the “inner” region of a paired delimiter around the cursor.
- Effects: Reads `E.cur`, `E.op`, and `E.buf`; on success applies the pending operator (yank/delete/change) to the computed inner range and may enter INSERT for `c`.
- Result: None.

## backspace

- Signature: `static void backspace(void) {`
- Params: (none)
- Purpose: Primitive editing/yank/paste operation used by NORMAL/INSERT/VISUAL.
- Effects: Mutates E.buf and related editor state; may push undo and/or set yank.
- Result: None.

## bufdelrange

- Signature: `static void bufdelrange(size_t a, size_t b) {`
- Params: size_t a; size_t b
- Purpose: Edit the main buffer with undo recording.
- Effects: Mutates E.buf, E.cur, E.dirty; pushes onto undo stack.
- Result: None.

## bufinsert

- Signature: `static void bufinsert(size_t at, const void *p, size_t n) {`
- Params: size_t at; const void *p; size_t n
- Purpose: Edit the main buffer with undo recording.
- Effects: Mutates E.buf, E.cur, E.dirty; pushes onto undo stack.
- Result: None.

## cclass

- Signature: `static int cclass(int c) {`
- Params: int c
- Purpose: Classify bytes for word-motion semantics.
- Effects: None.
- Result: Returns a boolean/class id.

## clampcur

- Signature: `static void clampcur(void) {`
- Params: (none)
- Purpose: Keep the cursor offset valid.
- Effects: Clamps `E.cur` to `0..E.buf.len` and, if it lands on a UTF-8 continuation byte, backs up to the start of the codepoint.
- Result: None.

## cmdexec

- Signature: `static void cmdexec(void) {`
- Params: (none)
- Purpose: Command-line (CMD) editing and execution for `:` and `/`.
- Effects: Mutates E.cmd/E.mode and dispatches to search/save/quit/substitute/run.
- Result: None.

## cmdkey

- Signature: `static void cmdkey(int key) {`
- Params: int key
- Purpose: Command-line (CMD) editing and execution for `:` and `/`.
- Effects: Mutates E.cmd/E.mode and dispatches to search/save/quit/substitute/run.
- Result: None.

## delchar

- Signature: `static void delchar(void) {`
- Params: (none)
- Purpose: Primitive editing/yank/paste operation used by NORMAL/INSERT/VISUAL.
- Effects: Mutates E.buf and related editor state; may push undo and/or set yank.
- Result: None.

## die

- Signature: `static void die(const char *fmt, ...) {`
- Params: const char *fmt; ...
- Purpose: Fatal error handler.
- Effects: Clears the screen, prints a formatted message to stderr, and terminates the process.
- Result: Does not return (calls `exit(1)`).

## drawmsg

- Signature: `static void drawmsg(struct sbuf *ab) {`
- Params: struct sbuf *ab
- Purpose: Screen rendering pipeline.
- Effects: Mutates scroll offsets and/or appends escape/text to an output sbuf; refresh writes to stdout.
- Result: None.

## drawrows

- Signature: `static void drawrows(struct sbuf *ab) {`
- Params: struct sbuf *ab
- Purpose: Screen rendering pipeline.
- Effects: Mutates scroll offsets and/or appends escape/text to an output sbuf; refresh writes to stdout.
- Result: None.

## drawstatus

- Signature: `static void drawstatus(struct sbuf *ab) {`
- Params: struct sbuf *ab
- Purpose: Screen rendering pipeline.
- Effects: Mutates scroll offsets and/or appends escape/text to an output sbuf; refresh writes to stdout.
- Result: None.

## enterinsert

- Signature: `static void enterinsert(void) {`
- Params: (none)
- Purpose: Enter INSERT mode and start a new insert-group for undo coalescing.
- Effects: Sets `E.mode = minsert` and increments `E.insgrp`.
- Result: None.

## filenew

- Signature: `static void filenew(void) {`
- Params: (none)
- Purpose: File/buffer lifecycle: new buffer, load file, save file.
- Effects: Mutates E.buf/E.filename/E.dirty and resets undo/scroll/cursor as needed.
- Result: None.

## fileopen

- Signature: `static void fileopen(const char *path) {`
- Params: const char *path
- Purpose: File/buffer lifecycle: new buffer, load file, save file.
- Effects: Mutates E.buf/E.filename/E.dirty and resets undo/scroll/cursor as needed.
- Result: None.

## filesave

- Signature: `static void filesave(void) {`
- Params: (none)
- Purpose: File/buffer lifecycle: new buffer, load file, save file.
- Effects: Mutates E.buf/E.filename/E.dirty and resets undo/scroll/cursor as needed.
- Result: None.

## findanchnext

- Signature: `static int findanchnext(const char *pat, size_t plen, int a0, int a1, size_t start, size_t *pos) {`
- Params: const char *pat; size_t plen; int a0; int a1; size_t start; size_t *pos
- Purpose: Find the next anchored match of `pat` starting at `start`.
- Effects: Reads `E.buf` to evaluate line boundaries; does not mutate editor state.
- Result: Returns 1 and writes the match offset to `*pos`, or returns 0 if not found.

## findanchnextrange

- Signature: `static int findanchnextrange(const char *pat, size_t plen, int a0, int a1, size_t start, size_t rs, size_t re, size_t *pos) {`
- Params: const char *pat; size_t plen; int a0; int a1; size_t start; size_t rs; size_t re; size_t *pos
- Purpose: Like `findanchnext`, but restrict matches to the byte range `[rs, re]`.
- Effects: Reads `E.buf` and line boundaries; clamps inputs; no editor-state mutation.
- Result: Returns 1 and writes the match offset to `*pos`, or returns 0 if not found.

## findanchprev

- Signature: `static int findanchprev(const char *pat, size_t plen, int a0, int a1, size_t before, size_t *pos) {`
- Params: const char *pat; size_t plen; int a0; int a1; size_t before; size_t *pos
- Purpose: Find the previous anchored match of `pat` before `before`.
- Effects: Reads `E.buf` to evaluate line boundaries; does not mutate editor state.
- Result: Returns 1 and writes the match offset to `*pos`, or returns 0 if not found.

## findinnerpair

- Signature: `static int findinnerpair(int open, int close, size_t *a, size_t *b) {`
- Params: int open; int close; size_t *a; size_t *b
- Purpose: Compute the “inner” byte range between a matching delimiter pair around/near the cursor.
- Effects: Reads `E.buf`/`E.cur`; does not mutate editor state.
- Result: Returns 1 and writes the inner range to `*a`/`*b`, or returns 0 if no pair found.

## findnext

- Signature: `static int findnext(const char *s, size_t slen, const char *pat, size_t plen, size_t start, size_t *pos) {`
- Params: const char *s; size_t slen; const char *pat; size_t plen; size_t start; size_t *pos
- Purpose: Find the next occurrence of `pat` in `s`, starting at `start`.
- Effects: None (pure scan over the provided buffers).
- Result: Returns 1 and writes the match offset to `*pos`, or returns 0 if not found.

## findprev

- Signature: `static int findprev(const char *s, size_t slen, const char *pat, size_t plen, size_t before, size_t *pos) {`
- Params: const char *s; size_t slen; const char *pat; size_t plen; size_t before; size_t *pos
- Purpose: Find the last occurrence of `pat` in `s` at or before `before`.
- Effects: None (pure scan over the provided buffers).
- Result: Returns 1 and writes the match offset to `*pos`, or returns 0 if not found.

## getwinsz

- Signature: `static int getwinsz(int *rows, int *cols) {`
- Params: int *rows; int *cols
- Purpose: Terminal / input / window-size management.
- Effects: Configures termios, reads from stdin, updates E.screenrows/E.screencols, or consumes winch flag.
- Result: Varies: readkey returns a key code; getwinsz returns 0/-1; others return None.

## insbyte

- Signature: `static void insbyte(int c) {`
- Params: int c
- Purpose: Primitive editing/yank/paste operation used by NORMAL/INSERT/VISUAL.
- Effects: Mutates E.buf and related editor state; may push undo and/or set yank.
- Result: None.

## inskey

- Signature: `static void inskey(int key) {`
- Params: int key
- Purpose: Handle a single keypress while in INSERT mode.
- Effects: Inserts/deletes bytes in `E.buf` (with undo recording), moves `E.cur`, and may leave INSERT on `Esc` (switching back to NORMAL).
- Result: None.

## insnl

- Signature: `static void insnl(void) {`
- Params: (none)
- Purpose: Primitive editing/yank/paste operation used by NORMAL/INSERT/VISUAL.
- Effects: Mutates E.buf and related editor state; may push undo and/or set yank.
- Result: None.

## isutfcont

- Signature: `static bool isutfcont(unsigned char c) {`
- Params: unsigned char c
- Purpose: Test whether a byte is a UTF-8 continuation byte.
- Effects: None.
- Result: Returns true for 0b10xxxxxx bytes.

## isword

- Signature: `static bool isword(int c) {`
- Params: int c
- Purpose: Classify bytes for word-motion semantics.
- Effects: None.
- Result: Returns a boolean/class id.

## linecount

- Signature: `static int linecount(void); static void normreset(void); static void yankset(size_t a, size_t b, bool linewise); static void bufdelrange(size_t a, size_t b); static void setwinsz(void); static void undoclear(void); static void undopushins(size_t at, const void *p, size_t n, size_t cur, bool merge); static void undopushdel(size_t at, const void *p, size_t n, size_t cur); static void undodo(void); static void enterinsert(void); static size_t utfprev(const char *s, size_t len, size_t i); static size_t utfnext(const char *s, size_t len, size_t i); static int findnext(const char *s, size_t slen, const char *pat, size_t plen, size_t start, size_t *pos); static int findprev(const char *s, size_t slen, const char *pat, size_t plen, size_t before, size_t *pos); static void searchdo(int dir); static void subcmd(const char *cmd, size_t rs, size_t re, int hasrange); static void bufinsert(size_t at, const void *p, size_t n); static int runstdout(const char *cmd, struct sbuf *out, int *ws); static void vison(void); static void visoff(void); static int visrange(size_t *a, size_t *b); static int viswant(void); static void viskey(int key); static void onsigwinch(int sig) {`
- Params: void); static void normreset(void); static void yankset(size_t a; size_t b; bool linewise); static void bufdelrange(size_t a; size_t b); static void setwinsz(void); static void undoclear(void); static void undopushins(size_t at; const void *p; size_t n; size_t cur; bool merge); static void undopushdel(size_t at; const void *p; size_t n; size_t cur); static void undodo(void); static void enterinsert(void); static size_t utfprev(const char *s; size_t len; size_t i); static size_t utfnext(const char *s; size_t len; size_t i); static int findnext(const char *s; size_t slen; const char *pat; size_t plen; size_t start; size_t *pos); static int findprev(const char *s; size_t slen; const char *pat; size_t plen; size_t before; size_t *pos); static void searchdo(int dir); static void subcmd(const char *cmd; size_t rs; size_t re; int hasrange); static void bufinsert(size_t at; const void *p; size_t n); static int runstdout(const char *cmd; struct sbuf *out; int *ws); static void vison(void); static void visoff(void); static int visrange(size_t *a; size_t *b); static int viswant(void); static void viskey(int key); static void onsigwinch(int sig
- Purpose: Count the number of lines in the current buffer.
- Effects: None.
- Result: Returns >= 1.

## linecount

- Signature: `static int linecount(void) {`
- Params: (none)
- Purpose: Count the number of lines in the current buffer.
- Effects: None.
- Result: Returns >= 1.

## lineend

- Signature: `static size_t lineend(size_t at) {`
- Params: size_t at
- Purpose: Find the start/end byte offset of the line containing a position.
- Effects: None.
- Result: Returns a byte offset in E.buf.

## linestart

- Signature: `static size_t linestart(size_t at) {`
- Params: size_t at
- Purpose: Find the start/end byte offset of the line containing a position.
- Effects: None.
- Result: Returns a byte offset in E.buf.

## main

- Signature: `int main(int argc, char **argv) {`
- Params: int argc; char **argv
- Purpose: Program entrypoint: initialize editor state, open the initial file (if any), and run the main input/redraw loop.
- Effects: Enables raw mode, installs SIGWINCH handler, loads the file into `E.buf`, then repeatedly handles resize, redraws, and dispatches keypresses.
- Result: Returns 0 on normal exit; may call `exit()` for forced quits/errors.

## modestr

- Signature: `static const char *modestr(void) {`
- Params: (none)
- Purpose: Status-line helper.
- Effects: Mutates E.status/E.statustime or reads E.mode.
- Result: Returns nothing / a pointer to a static string.

## motionb

- Signature: `static size_t motionb(size_t p) {`
- Params: size_t p
- Purpose: Compute a vi-ish motion from a starting byte offset.
- Effects: None; does not edit the buffer.
- Result: Returns the destination byte offset.

## motionbol

- Signature: `static size_t motionbol(size_t p) {`
- Params: size_t p
- Purpose: Compute a vi-ish motion from a starting byte offset.
- Effects: None; does not edit the buffer.
- Result: Returns the destination byte offset.

## motioncapg

- Signature: `static size_t motioncapg(size_t p) {`
- Params: size_t p
- Purpose: Compute a vi-ish motion from a starting byte offset.
- Effects: None; does not edit the buffer.
- Result: Returns the destination byte offset.

## motione

- Signature: `static size_t motione(size_t p) {`
- Params: size_t p
- Purpose: Compute a vi-ish motion from a starting byte offset.
- Effects: None; does not edit the buffer.
- Result: Returns the destination byte offset.

## motioneol

- Signature: `static size_t motioneol(size_t p) {`
- Params: size_t p
- Purpose: Compute a vi-ish motion from a starting byte offset.
- Effects: None; does not edit the buffer.
- Result: Returns the destination byte offset.

## motionf

- Signature: `static size_t motionf(size_t p, int ch, int n) {`
- Params: size_t p; int ch; int n
- Purpose: Compute a vi-ish motion from a starting byte offset.
- Effects: None; does not edit the buffer.
- Result: Returns the destination byte offset.

## motiongg

- Signature: `static size_t motiongg(size_t p) {`
- Params: size_t p
- Purpose: Compute a vi-ish motion from a starting byte offset.
- Effects: None; does not edit the buffer.
- Result: Returns the destination byte offset.

## motionh

- Signature: `static size_t motionh(size_t p) {`
- Params: size_t p
- Purpose: Compute a vi-ish motion from a starting byte offset.
- Effects: None; does not edit the buffer.
- Result: Returns the destination byte offset.

## motionj

- Signature: `static size_t motionj(size_t p) {`
- Params: size_t p
- Purpose: Compute a vi-ish motion from a starting byte offset.
- Effects: None; does not edit the buffer.
- Result: Returns the destination byte offset.

## motionk

- Signature: `static size_t motionk(size_t p) {`
- Params: size_t p
- Purpose: Compute a vi-ish motion from a starting byte offset.
- Effects: None; does not edit the buffer.
- Result: Returns the destination byte offset.

## motionl

- Signature: `static size_t motionl(size_t p) {`
- Params: size_t p
- Purpose: Compute a vi-ish motion from a starting byte offset.
- Effects: None; does not edit the buffer.
- Result: Returns the destination byte offset.

## motiont

- Signature: `static size_t motiont(size_t p, int ch, int n) {`
- Params: size_t p; int ch; int n
- Purpose: Compute a vi-ish motion from a starting byte offset.
- Effects: None; does not edit the buffer.
- Result: Returns the destination byte offset.

## motionw

- Signature: `static size_t motionw(size_t p) {`
- Params: size_t p
- Purpose: Compute a vi-ish motion from a starting byte offset.
- Effects: None; does not edit the buffer.
- Result: Returns the destination byte offset.

## ndigits

- Signature: `static int ndigits(int n) {`
- Params: int n
- Purpose: Compute formatting widths for the line-number gutter.
- Effects: None.
- Result: Returns a small integer width.

## normkey

- Signature: `static void normkey(int key) {`
- Params: int key
- Purpose: Handle a single keypress in NORMAL mode (counts, operators, motions, and commands).
- Effects: Updates `E.count`/`E.op`, switches modes (INSERT/VISUAL/CMD), and dispatches to motions or edit primitives (which may mutate the buffer).
- Result: None.

## normreset

- Signature: `static void normreset(void) {`
- Params: (none)
- Purpose: Clear any pending NORMAL-mode count/operator state.
- Effects: Resets `E.count` and `E.op` to 0.
- Result: None.

## numw

- Signature: `static int numw(void) {`
- Params: (none)
- Purpose: Compute formatting widths for the line-number gutter.
- Effects: None.
- Result: Returns a small integer width.

## off2col

- Signature: `static int off2col(size_t off) {`
- Params: size_t off
- Purpose: Translate between buffer offsets and (row,col) screen-ish coordinates.
- Effects: None.
- Result: Returns a row/col or byte offset.

## off2row

- Signature: `static int off2row(size_t off) {`
- Params: size_t off
- Purpose: Translate between buffer offsets and (row,col) screen-ish coordinates.
- Effects: None.
- Result: Returns a row/col or byte offset.

## offatcol

- Signature: `static size_t offatcol(size_t ls, size_t le, int want) {`
- Params: size_t ls; size_t le; int want
- Purpose: Translate between buffer offsets and (row,col) screen-ish coordinates.
- Effects: None.
- Result: Returns a row/col or byte offset.

## openabove

- Signature: `static void openabove(void) {`
- Params: (none)
- Purpose: Primitive editing/yank/paste operation used by NORMAL/INSERT/VISUAL.
- Effects: Mutates E.buf and related editor state; may push undo and/or set yank.
- Result: None.

## openbelow

- Signature: `static void openbelow(void) {`
- Params: (none)
- Purpose: Primitive editing/yank/paste operation used by NORMAL/INSERT/VISUAL.
- Effects: Mutates E.buf and related editor state; may push undo and/or set yank.
- Result: None.

## pairfor

- Signature: `static int pairfor(int c, int *open, int *close) {`
- Params: int c; int *open; int *close
- Purpose: Map a delimiter character to its matching open/close pair for text objects.
- Effects: Writes the selected pair to `*open` and `*close`.
- Result: Returns 1 if `c` is a supported delimiter ((), [], {}, <>, '', ""), otherwise 0.

## parsepat

- Signature: `static void parsepat(const char *s, size_t slen, struct sbuf *out, int *a0, int *a1) {`
- Params: const char *s; size_t slen; struct sbuf *out; int *a0; int *a1
- Purpose: Parse a raw pattern string into a literal pattern plus `^`/`$` anchor flags.
- Effects: Overwrites `out` and writes anchor flags to `*a0`/`*a1` (if non-NULL); does not touch editor state.
- Result: None.

## pasteafter

- Signature: `static void pasteafter(void) {`
- Params: (none)
- Purpose: Primitive editing/yank/paste operation used by NORMAL/INSERT/VISUAL.
- Effects: Mutates E.buf and related editor state; may push undo and/or set yank.
- Result: None.

## prevlinestart

- Signature: `static size_t prevlinestart(size_t ls) {`
- Params: size_t ls
- Purpose: Given a line-start offset `ls`, return the start offset of the previous line.
- Effects: Reads `E.buf` only.
- Result: Returns a byte offset (0 if already at the first line).

## processkey

- Signature: `static void processkey(void) {`
- Params: (none)
- Purpose: Read one key from the terminal and dispatch it to the active mode handler.
- Effects: Consumes input via `readkey()`, handles `Ctrl-Q` immediate exit, and calls `normkey`/`inskey`/`viskey`/`cmdkey`.
- Result: None.

## rawoff

- Signature: `static void rawoff(void) {`
- Params: (none)
- Purpose: Terminal / input / window-size management.
- Effects: Configures termios, reads from stdin, updates E.screenrows/E.screencols, or consumes winch flag.
- Result: Varies: readkey returns a key code; getwinsz returns 0/-1; others return None.

## rawon

- Signature: `static void rawon(void) {`
- Params: (none)
- Purpose: Terminal / input / window-size management.
- Effects: Configures termios, reads from stdin, updates E.screenrows/E.screencols, or consumes winch flag.
- Result: Varies: readkey returns a key code; getwinsz returns 0/-1; others return None.

## readkey

- Signature: `static int readkey(void) {`
- Params: (none)
- Purpose: Terminal / input / window-size management.
- Effects: Configures termios, reads from stdin, updates E.screenrows/E.screencols, or consumes winch flag.
- Result: Varies: readkey returns a key code; getwinsz returns 0/-1; others return None.

## refresh

- Signature: `static void refresh(void) {`
- Params: (none)
- Purpose: Screen rendering pipeline.
- Effects: Mutates scroll offsets and/or appends escape/text to an output sbuf; refresh writes to stdout.
- Result: None.

## row2off

- Signature: `static size_t row2off(int row) {`
- Params: int row
- Purpose: Translate between buffer offsets and (row,col) screen-ish coordinates.
- Effects: None.
- Result: Returns a row/col or byte offset.

## runstdout

- Signature: `static int runstdout(const char *cmd, struct sbuf *out, int *ws) {`
- Params: const char *cmd; struct sbuf *out; int *ws
- Purpose: Run a shell command and capture its stdout into an sbuf.
- Effects: Fork/execs; reads from a pipe; overwrites out; may set wait status.
- Result: Returns 0 on success, -1 on failure.

## sbufdel

- Signature: `static void sbufdel(struct sbuf *b, size_t at, size_t n) {`
- Params: struct sbuf *b; size_t at; size_t n
- Purpose: Delete bytes from a growable byte buffer.
- Effects: Mutates the given struct sbuf (may realloc/memmove).
- Result: None.

## sbuffree

- Signature: `static void sbuffree(struct sbuf *b) {`
- Params: struct sbuf *b
- Purpose: Operate on a growable byte buffer.
- Effects: Mutates the given struct sbuf (may realloc/memmove).
- Result: None.

## sbufgrow

- Signature: `static void sbufgrow(struct sbuf *b, size_t need) {`
- Params: struct sbuf *b; size_t need
- Purpose: Ensure capacity for a growable byte buffer.
- Effects: Mutates the given struct sbuf (may realloc/memmove).
- Result: None.

## sbufins

- Signature: `static void sbufins(struct sbuf *b, size_t at, const void *p, size_t n) {`
- Params: struct sbuf *b; size_t at; const void *p; size_t n
- Purpose: Insert bytes into a growable byte buffer.
- Effects: Mutates the given struct sbuf (may realloc/memmove).
- Result: None.

## sbufsetlen

- Signature: `static void sbufsetlen(struct sbuf *b, size_t n) {`
- Params: struct sbuf *b; size_t n
- Purpose: Set buffer length and maintain NUL terminator.
- Effects: Mutates the given struct sbuf (may realloc/memmove).
- Result: None.

## scroll

- Signature: `static void scroll(void) {`
- Params: (none)
- Purpose: Screen rendering pipeline.
- Effects: Mutates scroll offsets and/or appends escape/text to an output sbuf; refresh writes to stdout.
- Result: None.

## searchdo

- Signature: `static void searchdo(int dir) {`
- Params: int dir
- Purpose: Search/replace implementation helpers (literal matching plus ^/$ anchors).
- Effects: May mutate E.search/E.cur/E.buf and set status; some helpers are pure match computations.
- Result: Varies: find* return 1/0; others return None.

## setstatus

- Signature: `static void setstatus(const char *fmt, ...) {`
- Params: const char *fmt; ...
- Purpose: Status-line helper.
- Effects: Mutates E.status/E.statustime or reads E.mode.
- Result: Returns nothing / a pointer to a static string.

## setwinsz

- Signature: `static void setwinsz(void) {`
- Params: (none)
- Purpose: Terminal / input / window-size management.
- Effects: Configures termios, reads from stdin, updates E.screenrows/E.screencols, or consumes winch flag.
- Result: Varies: readkey returns a key code; getwinsz returns 0/-1; others return None.

## subcmd

- Signature: `static void subcmd(const char *cmd, size_t rs, size_t re, int hasrange) {`
- Params: const char *cmd; size_t rs; size_t re; int hasrange
- Purpose: Search/replace implementation helpers (literal matching plus ^/$ anchors).
- Effects: May mutate E.search/E.cur/E.buf and set status; some helpers are pure match computations.
- Result: Varies: find* return 1/0; others return None.

## undoclear

- Signature: `static void undoclear(void) {`
- Params: (none)
- Purpose: Free and reset the undo stack.
- Effects: Mutates E.undo/E.undolen and/or E.buf/E.cur; may set E.dirty.
- Result: None.

## undodo

- Signature: `static void undodo(void) {`
- Params: (none)
- Purpose: Pop and apply one undo record.
- Effects: Mutates E.undo/E.undolen and/or E.buf/E.cur; may set E.dirty.
- Result: None.

## undogrow

- Signature: `static void undogrow(int need) {`
- Params: int need
- Purpose: Ensure capacity for the undo stack array.
- Effects: Mutates E.undo/E.undolen and/or E.buf/E.cur; may set E.dirty.
- Result: None.

## undopushdel

- Signature: `static void undopushdel(size_t at, const void *p, size_t n, size_t cur) {`
- Params: size_t at; const void *p; size_t n; size_t cur
- Purpose: Record a delete edit for undo.
- Effects: Mutates E.undo/E.undolen and/or E.buf/E.cur; may set E.dirty.
- Result: None.

## undopushins

- Signature: `static void undopushins(size_t at, const void *p, size_t n, size_t cur, bool merge) {`
- Params: size_t at; const void *p; size_t n; size_t cur; bool merge
- Purpose: Record an insert edit for undo (optionally coalesce).
- Effects: Mutates E.undo/E.undolen and/or E.buf/E.cur; may set E.dirty.
- Result: None.

## usecount

- Signature: `static int usecount(void) {`
- Params: (none)
- Purpose: Convert the current numeric prefix into an effective repeat count.
- Effects: None.
- Result: Returns `E.count` if non-zero, otherwise 1.

## utfnext

- Signature: `static size_t utfnext(const char *s, size_t len, size_t i) {`
- Params: const char *s; size_t len; size_t i
- Purpose: Move to the previous/next UTF-8 codepoint boundary (byte offset).
- Effects: None; pure computation.
- Result: Returns a clamped byte offset.

## utfprev

- Signature: `static size_t utfprev(const char *s, size_t len, size_t i) {`
- Params: const char *s; size_t len; size_t i
- Purpose: Move to the previous/next UTF-8 codepoint boundary (byte offset).
- Effects: None; pure computation.
- Result: Returns a clamped byte offset.

## viskey

- Signature: `static void viskey(int key) {`
- Params: int key
- Purpose: VISUAL mode selection tracking and key handling.
- Effects: Mutates E.mode/E.vmark and may edit/yank/delete within the selection.
- Result: Varies: viswant/visrange return booleans; others return None.

## visoff

- Signature: `static void visoff(void) {`
- Params: (none)
- Purpose: VISUAL mode selection tracking and key handling.
- Effects: Mutates E.mode/E.vmark and may edit/yank/delete within the selection.
- Result: Varies: viswant/visrange return booleans; others return None.

## vison

- Signature: `static void vison(void) {`
- Params: (none)
- Purpose: VISUAL mode selection tracking and key handling.
- Effects: Mutates E.mode/E.vmark and may edit/yank/delete within the selection.
- Result: Varies: viswant/visrange return booleans; others return None.

## visrange

- Signature: `static int visrange(size_t *a, size_t *b) {`
- Params: size_t *a; size_t *b
- Purpose: VISUAL mode selection tracking and key handling.
- Effects: Mutates E.mode/E.vmark and may edit/yank/delete within the selection.
- Result: Varies: viswant/visrange return booleans; others return None.

## viswant

- Signature: `static int viswant(void) {`
- Params: (none)
- Purpose: VISUAL mode selection tracking and key handling.
- Effects: Mutates E.mode/E.vmark and may edit/yank/delete within the selection.
- Result: Varies: viswant/visrange return booleans; others return None.

## winchtick

- Signature: `static void winchtick(void) {`
- Params: (none)
- Purpose: Terminal / input / window-size management.
- Effects: Configures termios, reads from stdin, updates E.screenrows/E.screencols, or consumes winch flag.
- Result: Varies: readkey returns a key code; getwinsz returns 0/-1; others return None.

## yankset

- Signature: `static void yankset(size_t a, size_t b, bool linewise) {`
- Params: size_t a; size_t b; bool linewise
- Purpose: Primitive editing/yank/paste operation used by NORMAL/INSERT/VISUAL.
- Effects: Mutates E.buf and related editor state; may push undo and/or set yank.
- Result: None.
