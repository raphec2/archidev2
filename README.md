# ArchiDev-Flow (prototype)

A deliberately small workspace for using one AI CLI as a **consultant** and another as a
**developer**, side by side, with easy text routing between them.

It is a Bash launcher plus one WezTerm Lua config. There is no application: WezTerm owns the
window, panes, terminal emulation and selection; Yazi browses files; Micro edits them. Nothing
here is Electron, Chromium, React, a web UI or a custom terminal emulator.

```
┌───────────────────────────┬───────────────────────────┐
│ Consultant  (codex)       │ Developer  (claude)       │
│ cwd = CONSULTANT_DIR      │ cwd = selected project    │
├───────────────────────────┴───────────────────────────┤
│ Files  (yazi -> micro)    cwd = selected project      │
└───────────────────────────────────────────────────────┘
```

Linux-first. Windows/macOS are not implemented.

---

## Dependencies

**Required**

| | why |
|---|---|
| `bash` 4+ | the launcher |
| `wezterm` | window, panes, selection, text routing |
| `codex` | consultant AI (or whatever you set `CONSULTANT_COMMAND` to) |
| `claude` | developer AI (or whatever you set `DEVELOPER_COMMAND` to) |

**Optional** (the workspace still launches without them, with a reduced files pane)

| | why | without it |
|---|---|---|
| `yazi` | file manager in the bottom pane | bottom pane is a plain shell |
| `micro` | editor opened from the file manager | falls back to `nano`, then `vi` |
| `base64` | tags panes with their role for text routing | routing falls back to pane ids recorded at startup |

Check what you have — this never installs anything:

```sh
./scripts/check-dependencies
```

It reads your config, so it checks *your* configured commands, not just the defaults, and prints
per-distro install hints for anything missing.

---

## Running it

Straight from the repository, no install step:

```sh
./bin/archidev-flow .                     # current directory as the project
./bin/archidev-flow ~/code/some-project   # or any path, relative or absolute
./bin/archidev-flow                       # no argument == "."
```

Put it on `PATH` with a symlink (the script resolves its own symlink, so the repo can live
anywhere):

```sh
mkdir -p ~/.local/bin
ln -s "$PWD/bin/archidev-flow" ~/.local/bin/archidev-flow
```

### First run

If `CONSULTANT_DIR` is not configured, the launcher asks for it once:

```
First run: ArchiDev-Flow needs a consultant working directory.
Consultant directory [/home/you/archidev-consultant]:
```

Press Enter to take the suggestion, or type a path (`~` is expanded). If the directory does not
exist you are asked whether to create it. The answer is saved to
`${XDG_CONFIG_HOME:-~/.config}/archidev-flow/config` (dir `0700`, file `0600`). This is the only
file ArchiDev-Flow ever writes outside the repository, and nothing is written into your project.

Non-interactive use skips the prompt entirely — pass `--consultant-dir DIR`, or create the
config file yourself.

### Options

```
-n, --dry-run            validate everything and print the launch plan; start nothing
    --check-deps         run the dependency check and exit
-c, --config FILE        use an alternative config file
    --consultant-dir D   override CONSULTANT_DIR for this launch only (not saved)
    --no-files-pane      two panes only
    --print-config-path  print the config path and exit
-h, --help / -V, --version
```

Exit codes: `0` ok · `2` bad arguments or project path · `3` consultant-directory problem ·
`4` missing required command or WezTerm config.

---

## Configuration

`${XDG_CONFIG_HOME:-~/.config}/archidev-flow/config`, see [`examples/config.example`](examples/config.example).

```sh
CONSULTANT_DIR='/home/you/archidev-consultant'
CONSULTANT_COMMAND='codex'
DEVELOPER_COMMAND='claude'
FILE_MANAGER_COMMAND='yazi'
EDITOR_COMMAND='micro'
```

**File format.** `KEY=VALUE`, one per line. `#` comments and blank lines ignored. Whitespace
around the key and value is trimmed. One layer of matching surrounding single or double quotes
is removed. Values are then **literal**: no `$variable` expansion, no backslash escapes, no
globbing, no command substitution, no `eval` — the file is parsed as data by a hand-written
reader, never sourced. Unknown keys are ignored with a warning.

**Command format — exact rules.** Every `*_COMMAND` is a **whitespace-separated argv list**:

```
CONSULTANT_COMMAND='codex --model gpt-5'   ->  argv = ["codex", "--model", "gpt-5"]
```

* The split happens once, in Bash; the argv list is handed to WezTerm one word per line and
  executed with no shell in between. Shell metacharacters in a value are therefore inert, not
  dangerous — and also not functional: no pipes, no redirection, no `&&`.
* There is **no quoting inside a command value**, so *an argument cannot contain a space*. This
  is the documented limitation of the prototype. If you need that, write a two-line wrapper
  script and point the config at it.
* The first word must be on `PATH` or an absolute path.

`EDITOR_COMMAND` is the exception: it is exported verbatim as `EDITOR`/`VISUAL` for the files
pane, so the file manager's own editor-launching rules apply to it.

**Defaults** if a key is absent: `codex`, `claude`, `yazi`, `micro`.

---

## Layout and shortcuts

The layout is built inside WezTerm's `gui-startup` event — the deterministic hook for exactly
this — so there is no sleeping, polling, or racing against shell startup. Consultant is spawned
first, the developer is a `Right` split of it, and the files pane is a `top_level` `Bottom`
split so it spans the full width. The requested layout is produced exactly; no compromise was
needed.

| keys | action |
|---|---|
| `Alt` `c` / `d` / `e` | focus Consultant / Developer / Files |
| `Ctrl+Alt` `c` / `d` | send **current selection** to Consultant / Developer |
| `Alt+Shift` `C` / `D` | send **clipboard** to Consultant / Developer |
| `Alt` `z` | zoom (maximise) the active pane, toggle |
| `Alt` `←↑↓→` | move focus between panes |
| `Alt` `h` | show the shortcut legend |
| `Ctrl+Shift` `C` / `V` | WezTerm's own copy / paste, unchanged |

The legend is always visible in the status bar on the right, and transfer results ("sent 128
bytes to consultant (paste, not submitted)") appear there too.

**Transferred text is never submitted.** Text is inserted with WezTerm's paste path
(`pane:send_paste`), which uses bracketed paste whenever the receiving program enables it — both
Codex and Claude Code do. No Enter, newline or other execution keystroke is ever appended, and
selections are stripped of *trailing* newlines before transfer so that a whole-line selection
cannot act as Enter. Newlines *inside* the text are preserved, which is the point: multiline
prompts arrive intact.

**Selection routing** reads the selection from the pane you are in, sends it, then clears the
selection and focuses the destination. **Clipboard routing** is the dependable fallback for
anything the terminal cannot hand to Lua as a selection.

### Pane identity

Routing must never hit the wrong pane, so identity is explicit, not positional:

1. Each pane runs `lib/archidev-pane <role> …`, which immediately emits an OSC 1337
   `SetUserVar` tagging the pane `ARCHIDEV_ROLE=consultant|developer|files`. Lua reads it with
   `pane:get_user_vars()`. It survives config reloads, resizes, and the AI process exiting.
2. A registry of pane ids recorded at `gui-startup` is used only if a pane has not reported its
   user var yet.

Both lookups are **scoped to the current mux window**, so a second `archidev-flow` launch, an
extra tab, or an unrelated pane can never receive text meant for this window. Each launch is
also a separate WezTerm process (`--always-new-process` where supported) with its own workspace
name, `archidev-<project>-<pid>-<epoch>`, so re-running the launcher never collides with a
previous session.

### When an AI exits

`lib/archidev-pane` runs the command, and when it exits prints its status and `exec`s an
interactive shell in the same pane. The pane stays usable and keeps its role tag; nothing is
destroyed.

---

## Files pane

The bottom pane starts Yazi in the project directory with `EDITOR` and `VISUAL` set to the
configured editor **for that pane's process tree only** — your shell rc files are not touched
and no environment is modified globally. Pressing `o`/`Enter` on a text file in Yazi opens
Micro; quitting Micro returns you to Yazi.

To move text out of the editor, select it with the mouse and use `Ctrl+Alt+c` / `Ctrl+Alt+d`.
Micro captures the mouse, so hold **Shift** while dragging to make WezTerm take the selection
instead of the application. Alternatively use Micro's own copy (`Ctrl+C`) and then
`Alt+Shift+C` / `Alt+Shift+D`.

No destructive file shortcuts are defined; Yazi's own defaults apply.

If Yazi is missing the pane is a plain shell in the project directory, and the launcher says so
before starting. If the editor is missing it falls back to `micro`, `nano`, then `vi`.

---

## Validation

```sh
./tests/run-tests            # 141 assertions, no GUI, no network
./bin/archidev-flow --dry-run .
./scripts/check-dependencies
```

`tests/run-tests` redirects `HOME` and `XDG_CONFIG_HOME` into a temporary directory for its
whole run, so it cannot see or modify your real configuration. It covers Bash syntax, XDG
resolution, config parsing/writing round-trips (spaces, quotes, `$`, backticks, `#`, unicode),
command splitting, path resolution (relative, absolute, spaces, leading dash, `..`, missing,
not-a-directory, unreadable, `CDPATH` hijacking), every launcher exit path, the dependency
checker, and the pane runner's role tagging and exit behaviour.

It also drives the **interactive first run on a real pty** (via `python3`) and lets it launch a
**stub `wezterm`**, asserting the config that gets written, its permissions, the directory that
gets created, and the exact argv and `ARCHIDEV_*` environment handed to WezTerm.

Lua is checked with whatever is available: `wezterm --config-file … show-keys`, a local
`luac`/`lua`, or two optional node parsers. The repository has no dependencies of its own, so
for the last option install them in a scratch directory:

```sh
d=$(mktemp -d); (cd "$d" && npm install luaparse fengari)
ADF_NODE_MODULES=$d ./tests/run-tests      # 157 assertions
```

`luaparse` syntax-checks the config; `fengari` is a Lua VM that **executes** the config's pure
helpers (argv splitting, trailing-newline stripping) and asserts their behaviour. Without any of
these the Lua checks are reported as skipped, never as passing.

`--dry-run` resolves and validates config and paths, prints a `printf %q`-quoted summary of
exactly what would launch, and starts nothing. It prints only ArchiDev's own values — it never
dumps the environment and has no access to secrets.

---

## Known limitations

* **The GUI has not been verified.** WezTerm is not installed on the machine this was built on,
  so the layout, keybindings and routing are written against WezTerm's documented API but have
  never been observed running. Everything outside the GUI — the launcher, config handling, the
  pane runner, the exec into WezTerm, and the Lua config's pure helpers — is tested. Work
  through the smoke test below before trusting it.
* **Clipboard routing cannot trim.** WezTerm's Lua API can write the clipboard but not read it,
  so clipboard transfers are performed with WezTerm's own `PasteFrom` action into the focused
  destination. That means bracketed paste is used correctly, but ArchiDev cannot strip a
  trailing newline from clipboard content the way it does for a selection. In a program that
  does not implement bracketed paste, clipboard text ending in a newline may be submitted.
  Selection routing has no such gap. (Before pasting, ArchiDev verifies the destination really
  is the focused pane and aborts rather than paste into the wrong one.)
* **Selection routing needs a WezTerm-level selection.** Full-screen TUIs that capture the mouse
  (Micro, and the AI TUIs themselves) need Shift-drag. If a program renders text that WezTerm
  cannot select cleanly, use the clipboard route.
* **`send_paste` availability.** If a WezTerm build does not expose `pane:send_paste`, ArchiDev
  falls back to `pane:send_text` (raw bytes, still no Enter) and says so in the status bar. It
  does not fabricate bracketed paste.
* **No spaces inside command arguments** (see the command format rules).
* **Routing is per-window.** Extra tabs in the ArchiDev window can still route to the panes
  (the lookup scans the whole mux window). A *separate* window has no ArchiDev panes, and
  routing from it reports "no consultant pane in this window" rather than guessing.
* Prototype scope: no plugin system, daemon, database, package manager, or Git UI.

---

## Manual smoke test (Linux, GUI)

1. `./scripts/check-dependencies` — expect all required items present.
2. `./bin/archidev-flow --dry-run .` — check the project and consultant directories.
3. `./bin/archidev-flow .` — first run asks for the consultant directory; accept the default.
4. **Layout**: consultant left, developer right, files bottom, full width. Consultant is focused.
5. **cwds**: consultant is in `CONSULTANT_DIR`, developer and files are in the project.
6. **Focus**: `Alt+d`, `Alt+e`, `Alt+c` — status bar confirms each move.
7. **Zoom**: `Alt+z` maximises the active pane, `Alt+z` restores it.
8. **Selection → developer**: in the consultant pane select a couple of lines with the mouse,
   press `Ctrl+Alt+d`. The text appears in the developer's input, **nothing is submitted**, the
   selection clears, focus moves to the developer. Confirm the prompt is still editable and
   press Escape/clear it.
9. **Multiline**: select 3+ lines, route them, and confirm the line breaks survived as one
   prompt rather than several submissions.
10. **Clipboard → consultant**: copy text from anywhere (`Ctrl+Shift+C` in a pane, or another
    app), press `Alt+Shift+C`. Text lands in the consultant, unsubmitted.
11. **Files**: `Alt+e`, browse to a text file, open it — Micro opens. Edit, save, quit — you are
    back in Yazi.
12. **Editor selection**: in Micro, Shift-drag to select and `Ctrl+Alt+c` to route to the
    consultant.
13. **Process exit**: in the developer pane exit Claude. The pane prints the exit status and
    leaves a shell. Press `Alt+c` then `Ctrl+Alt+d` with a selection — routing still finds the
    pane.
14. **Independence**: run `./bin/archidev-flow ~/some/other/project` in another terminal. A
    second window appears; routing in each window stays within that window.
15. **Missing optional tool** (optional): set `FILE_MANAGER_COMMAND='nope'` and relaunch —
    expect a warning and a plain shell in the bottom pane, not a failure.

---

## Future Windows note

Not implemented. The shape it would take: WezTerm runs natively on Windows and the Lua config
here is platform-independent apart from the `/usr/bin/env bash` pane runner, so a Windows port
is a PowerShell equivalent of `bin/archidev-flow` that resolves paths, reads the same config
file from `%APPDATA%`, and either launches native `codex.exe`/`claude.exe` with a `.ps1` pane
runner, or launches the existing Bash runner through `wsl.exe` with paths translated by
`wslpath`. The routing logic, pane identity scheme and keybindings would carry over unchanged.
