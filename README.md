# ArchiDev2

**Looking for the current native Qt app?** It lives on the [`qt-native-spike` branch](https://github.com/raphec2/archidev2/tree/qt-native-spike). Start with its [front page](https://github.com/raphec2/archidev2/blob/qt-native-spike/README.md) and [build and usage guide](https://github.com/raphec2/archidev2/blob/qt-native-spike/spikes/qt/README.md). The Qt prototype puts a Consultant AI, Developer AI, project shell, two file explorers and Notes in one Linux window. Select text in one pane, click a destination, and it is inserted without being submitted.

The Qt version currently builds from source on Ubuntu. It has its own project-local settings and optional desktop launcher. **The code and instructions below are for the earlier WezTerm version on `main`.**

---

# Earlier WezTerm prototype

A deliberately small workspace for using one AI CLI as a **consultant** and another as a
**developer**, side by side, with easy text routing between them.

It is a Bash launcher plus one WezTerm Lua config. There is no application: WezTerm owns the
window, panes, terminal emulation and selection; Yazi browses files; Micro edits them. Nothing
here is Electron, Chromium, React, a web UI or a custom terminal emulator.

```
┌───────────────────────────┬───────────────────────────┐
│ CONSULTANT  (codex)       │ DEVELOPER  (claude)       │
│ cwd = CONSULTANT_DIR      │ cwd = selected project    │
├───────────────────────────┴───────────────────────────┤
│ FILES  (yazi -> micro)    cwd = selected project      │
├───────────────────────────────────────────────────────┤
│ ▶ CONSULTANT | DEVELOPER | FILES     Send selection…  │  <- status bar
└───────────────────────────────────────────────────────┘
```

Linux-first. Windows/macOS are not implemented.

> ArchiDev2 replaces an older Electron application called ArchiDev-Flow. The only places that
> name still appears are the legacy config migration and a deprecated launcher shim, both
> described below.

---

## First use, start to finish

```sh
# 1. Check what you have. This never installs anything.
./scripts/check-dependencies

# 2. Launch against a project directory (use a scratch one the first time).
./bin/archidev2 ~/some/project
```

On first run it asks once for a **consultant directory** — where the consultant AI starts, kept
separate from your project — and remembers it.

Then, in the window:

3. **Select text** with the mouse in any pane.
4. **Send it**: `Alt+Shift+C` → CONSULTANT, `Alt+Shift+D` → DEVELOPER. Or **right-click** for a
   menu of actions.
5. **Focus follows the text.** The status bar confirms: `Sent selection to DEVELOPER — focus
   moved`. Nothing is ever submitted for you; press Enter yourself when you're ready.
6. **Open and edit files** in the bottom pane: `Alt+F`, browse in Yazi, press Enter on a file to
   open Micro, quit Micro to return to Yazi.
7. **From Micro, copy instead of dragging.** Micro keeps its selection to itself, so ArchiDev2
   cannot read it. Select in Micro, press `Ctrl+C`, then use a **Send clipboard** action.

Press `Alt+H` at any time for the full list.

---

## Dependencies

**Required**

| | why |
|---|---|
| `bash` 4+ | the launcher |
| `wezterm` | window, panes, selection, text routing |
| `codex` | consultant AI (or whatever you set `CONSULTANT_COMMAND` to) |
| `claude` | developer AI (or whatever you set `DEVELOPER_COMMAND` to) |

**Optional** (the workspace still launches without them)

| | why | without it |
|---|---|---|
| `yazi` | file manager in the bottom pane | bottom pane is a plain shell |
| `micro` | editor opened from the file manager | falls back to `nano`, then `vi` |
| `base64` | tags panes with their role for text routing | routing falls back to pane ids recorded at startup |

`./scripts/check-dependencies` reads your config, so it checks *your* configured commands, not
just the defaults, and prints per-distro install hints for anything missing.

---

## Running it

Straight from the repository, no install step:

```sh
./bin/archidev2 .                     # current directory as the project
./bin/archidev2 ~/code/some-project   # or any path, relative or absolute
./bin/archidev2                       # no argument == "."
```

Put it on `PATH` with a symlink (the script resolves its own symlink, so the repo can live
anywhere):

```sh
mkdir -p ~/.local/bin
ln -s "$PWD/bin/archidev2" ~/.local/bin/archidev2
```

### Options

```
-n, --dry-run            validate everything and print the launch plan; start nothing
    --check-deps         run the dependency check and exit
-c, --config FILE        use an alternative config file
    --consultant-dir D   override CONSULTANT_DIR for this launch only (not saved)
    --no-files-pane      two panes only
    --no-migrate         do not migrate a config from the older prototype
-q, --quiet              do not print the startup hint
    --print-config-path  print the config path and exit
-h, --help / -V, --version
```

Exit codes: `0` ok · `2` bad arguments or project path · `3` consultant-directory problem ·
`4` missing required command or WezTerm config.

---

## Controls

### Keyboard

| keys | action |
|---|---|
| `Alt+Shift+C` / `Alt+Shift+D` | send the **selection** to Consultant / Developer |
| `Ctrl+Alt+Shift+C` / `Ctrl+Alt+Shift+D` | send the **clipboard** to Consultant / Developer |
| `Alt+C` / `Alt+D` / `Alt+F` | focus Consultant / Developer / Files |
| `Alt+Enter` | open the action menu (keyboard twin of right-click) |
| `Alt+Z` | maximise / restore the active pane |
| `Alt+H` | help overlay |
| `Alt+←↑↓→` | move focus between panes |
| `Ctrl+Shift+C` / `Ctrl+Shift+V` | WezTerm's own copy / paste, never overridden |

Legacy aliases kept from the first prototype: `Ctrl+Alt+C` / `Ctrl+Alt+D` (send selection) and
`Alt+E` (focus Files).

**Why the defaults moved.** `Ctrl+Alt+D` is "show desktop" on Cinnamon and never reaches
WezTerm, so selection sending moved to `Alt+Shift`. Note the one deliberate change of meaning:
`Alt+Shift+C`/`D` used to send the *clipboard* and now send the *selection*; the clipboard moved
to `Ctrl+Alt+Shift`. Every binding is configurable (see below).

### Mouse

**Right-click opens the ArchiDev2 action menu**, on the button *release*. WezTerm has no default
right-button binding, so nothing is taken away from you.

The menu offers, depending on whether text is selected:

- Send selection to CONSULTANT / DEVELOPER
- Copy selection to the clipboard (normal WezTerm copy)
- Send clipboard to CONSULTANT / DEVELOPER
- Paste clipboard into this pane
- Show all shortcuts
- Cancel

Rows can be **clicked with the mouse**, picked with the arrow keys, or chosen by pressing the
number shown against them. Escape cancels and does nothing.

Micro and Yazi capture the mouse, so a plain right-click goes to them rather than to ArchiDev2.
Holding **Shift** is WezTerm's documented bypass and should route the click to ArchiDev2 instead;
this was not confirmed on the tested setup, so **`Alt+Enter` is the reliable way to open the menu
from those panes** — that is verified to work. ArchiDev2 deliberately does not set
`mouse_reporting = true`, which would take the button away from Micro and Yazi entirely.

The menu deliberately opens on the **completed click** (WezTerm's `Up` event) rather than on
press. Opening it on press meant the release that followed landed on the just-opened chooser and
dismissed it — unavoidable on a trackpad, where press and release are one tap. The press itself
is bound to `Nop` for whichever button is configured, so a button that has a WezTerm default
(middle-click pastes the primary selection) cannot fire that default and then open the menu.
Left-button selection, copy and paste are untouched.

This is the whole of what WezTerm supports for mouse-driven controls: it has no toolbar,
clickable-widget or custom-overlay API, and its `InputSelector` chooser is the only clickable
control available to a config. No second GUI, web server, daemon or synthetic-mouse trickery was
added to fake anything more.

`MOUSE_MENU_BUTTON` can be set to `Right`, `Middle`, or `off`. With `off`, WezTerm's own default
middle-click paste is left in place.

### What works where (tested)

Micro and WezTerm expose different selection models, so there is **no single uniform gesture yet**.
This is the matrix as actually live-tested on Linux Mint with WezTerm 20240203 and Micro 2.0.13:

| Source | How to send it |
|---|---|
| **CONSULTANT** pane | normal mouse drag → action menu, or `Alt+Shift+C`/`D` |
| **DEVELOPER** pane | normal mouse drag → action menu, or `Alt+Shift+C`/`D` |
| **Micro** (files pane) | select in Micro → `Ctrl+C` → `Alt+Enter` → **Send clipboard to …** |
| **Another application** | copy there → action menu → **Send clipboard to …** |

Why the difference: ArchiDev2 reads selections through WezTerm's
`get_selection_text_for_pane`, which only knows about selections WezTerm itself made. Micro draws
and owns its own selection internally, so WezTerm has nothing to hand over — dragging in Micro,
with or without Shift, leaves ArchiDev2 reporting *no text selected*. Clipboard routing is the
supported path out of Micro, and it still moves focus to the destination.

Keyboard shortcuts are accelerators. **The mouse action menu is the preferred, discoverable
control** — it shows exactly which destinations are available for what you have selected.

### Selection routing vs clipboard routing

**Selection routing** (`Alt+Shift+…`, and the menu's *Send selection* rows) reads the text
WezTerm itself has selected, strips trailing newlines, inserts it into the destination, clears
the source selection and focuses the destination. This is the preferred path.

**Clipboard routing** (`Ctrl+Alt+Shift+…`, and the menu's *Send clipboard* rows) is the
dependable fallback for anything WezTerm cannot hand to Lua as a selection. It focuses the
destination and performs WezTerm's own paste there.

**Nothing is ever submitted.** Text is inserted with WezTerm's paste path (`pane:send_paste`),
which uses bracketed paste whenever the receiving program enables it — both Codex and Claude
Code do. No Enter, newline or other execution keystroke is ever appended. Newlines *inside* the
text are preserved, so multiline prompts arrive intact.

---

## Seeing which pane has focus

- Inactive panes are **dimmed** (`INACTIVE_PANE_BRIGHTNESS`, default `0.60`) but stay readable.
- The divider between panes is a bright accent colour.
- The **status bar along the bottom** always shows the three role labels — `CONSULTANT`,
  `DEVELOPER`, `FILES` — with the active one drawn as a filled, coloured chip and the others
  dim.
- After a transfer the status bar shows a confirmation such as `Sent selection to DEVELOPER —
  focus moved` for a few seconds. The periodic status repaint checks for a live confirmation and
  leaves it alone rather than erasing it.

WezTerm has **no per-pane title bar** ([upstream issue #1970](https://github.com/wezterm/wezterm/issues/1970)),
so a label cannot be drawn inside each pane. The status-bar chips are the closest supported
equivalent; the alternative — printing labels into the panes — would land in each AI's scrollback
and be wiped by its first full-screen redraw, so it is not done.

---

## Configuration

`${XDG_CONFIG_HOME:-~/.config}/archidev2/config`, see [`examples/config.example`](examples/config.example).

```sh
CONSULTANT_DIR='/home/you/archidev2-consultant'
CONSULTANT_COMMAND='codex'
DEVELOPER_COMMAND='claude'
FILE_MANAGER_COMMAND='yazi'
EDITOR_COMMAND='micro'
KEY_SEND_SELECTION_CONSULTANT='ALT|SHIFT c'
MOUSE_MENU_BUTTON='Right'
INACTIVE_PANE_BRIGHTNESS='0.60'
STARTUP_HINT='on'
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
  executed with no shell in between. Shell metacharacters in a value are inert, not dangerous —
  and also not functional: no pipes, no redirection, no `&&`.
* There is **no quoting inside a command value**, so *an argument cannot contain a space*. If you
  need that, write a two-line wrapper script and point the config at it.
* The first word must be on `PATH` or an absolute path.

**Key binding format.** `MODS key`, where `MODS` is one or more of `CTRL ALT SHIFT SUPER` joined
by `|`, or `NONE`; and `key` is a single letter or digit, or one of `Enter Tab Space Escape
Backspace Delete Insert Home End PageUp PageDown LeftArrow RightArrow UpArrow DownArrow
F1`–`F24`. Anything outside that grammar is rejected with a warning and the default is used, so
a bad value can never break the workspace or reach Lua as code. Configurable keys:
`KEY_SEND_SELECTION_CONSULTANT`, `KEY_SEND_SELECTION_DEVELOPER`,
`KEY_SEND_CLIPBOARD_CONSULTANT`, `KEY_SEND_CLIPBOARD_DEVELOPER`, `KEY_FOCUS_CONSULTANT`,
`KEY_FOCUS_DEVELOPER`, `KEY_FOCUS_FILES`, `KEY_ACTION_MENU`, `KEY_HELP`, `KEY_ZOOM`.

`archidev2 --dry-run` prints every resolved binding with its human-readable label.

### Migrating from the first prototype

The pre-rename prototype stored its settings in `~/.config/archidev-flow/config`. On first
launch, **if — and only if — no ArchiDev2 config exists yet**, those settings are copied to
`~/.config/archidev2/config` and the move is reported:

```
archidev2: migrated settings from the previous prototype:
  from: /home/you/.config/archidev-flow/config
  to:   /home/you/.config/archidev2/config  (mode 600, preserved)
  keys: CONSULTANT_DIR
The old file was left untouched; nothing else in that directory was read.
```

An existing ArchiDev2 config is **never** overwritten, the old file is **never** deleted, and the
file's permissions are preserved. Note that `~/.config/archidev-flow/` is shared with the old
Electron application's caches, cookies and workspaces: migration reads exactly one file,
`config`, and touches nothing else in there. `--no-migrate` skips it entirely.

### Deprecated launcher

`bin/archidev-flow` remains as a thin shim: it prints a one-line deprecation notice and `exec`s
`bin/archidev2` with your arguments unchanged. It exists only so an existing symlink on `PATH`
keeps working, and can be deleted.

---

## Files pane

The bottom pane starts Yazi in the project directory with `EDITOR` and `VISUAL` set to the
configured editor **for that pane's process tree only** — your shell rc files are not touched and
no environment is modified globally. Pressing Enter on a text file opens Micro; quitting Micro
returns you to Yazi.

To move text out of the editor, use Micro's own copy: select in Micro, press `Ctrl+C`, open the
action menu with `Alt+Enter`, and choose **Send clipboard to CONSULTANT/DEVELOPER**. Focus still
moves to the destination. Dragging in Micro does not produce a selection ArchiDev2 can read (see
the interaction matrix above).

No destructive file shortcuts are defined; Yazi's own defaults apply.

If Yazi is missing the pane is a plain shell in the project directory, and the launcher says so
before starting. If the editor is missing it falls back to `micro`, `nano`, then `vi`.

### When an AI exits

The pane prints one dim line — `archidev2: DEVELOPER exited (status 0) - this pane is now a
shell.` — and leaves an interactive shell with its role tag intact, so routing keeps working.
No banner is repeated on every shell restart.

---

## Validation

```sh
./tests/run-tests            # 283 assertions, no GUI, no network
./bin/archidev2 --dry-run .
./scripts/check-dependencies
```

`tests/run-tests` redirects `HOME` and `XDG_CONFIG_HOME` into a temporary directory for its whole
run, so it cannot see or modify your real configuration. It covers Bash syntax, XDG resolution,
config parsing/writing round-trips (spaces, quotes, `$`, backticks, `#`, unicode), command
splitting, key-spec and mouse/brightness validation (including rejection of injection-shaped
values), path resolution (relative, absolute, spaces, leading dash, `..`, missing,
not-a-directory, unreadable, `CDPATH` hijacking), every launcher exit path, legacy migration in
all four states plus permission preservation and shared-directory safety, the deprecation shim,
the dependency checker, the pane runner's role tagging and exit behaviour, and a naming sweep
that fails if `ArchiDev-Flow` appears in any normal program output.

It also drives the **interactive first run on a real pty** (via `python3`) and lets it launch a
**stub `wezterm`**, asserting the config written, its permissions, and the exact argv and
`ARCHIDEV2_*` environment handed to WezTerm.

When `wezterm` is installed, the suite validates the real Lua config with
`wezterm --config-file … show-keys`, and asserts that the generated key and mouse bindings
actually register — including that `Alt+C` (focus) and `Alt+Shift+C` (send) stay distinct, and
that the action menu is bound to the button's `Up` event and not its `Down`, that the press is a
no-op, that `MOUSE_MENU_BUTTON=Middle` overrides the default middle-click paste, and that
`MOUSE_MENU_BUTTON=off` produces no mouse binding and restores that default. Two optional node parsers add a syntax
check and execute the config's pure helpers in a real Lua VM:

```sh
d=$(mktemp -d); (cd "$d" && npm install luaparse fengari)
AD2_NODE_MODULES=$d ./tests/run-tests      # 299 assertions
```

`--dry-run` resolves and validates config and paths, prints a `printf %q`-quoted summary of what
would launch plus every key binding, and starts nothing. It prints only ArchiDev2's own values —
it never dumps the environment and has no access to secrets.

**GUI behaviour is not verified automatically.** Everything above runs headless. The layout,
dimming, status chips, menus and routing must be confirmed by hand.

---

## Manual smoke test

1. **Visible focus** — launch. Consultant is focused: its chip in the bottom bar is a filled
   colour, the other two are dim, and the other panes are visibly darker but still readable.
   Press `Alt+D`, then `Alt+F`, then `Alt+C`, and watch the chip and the dimming follow.
2. **Keyboard selection routing** — select two lines in the consultant, press `Alt+Shift+D`. The
   text appears in the developer's input, unsubmitted; the selection clears; focus moves; the
   bar shows `Sent selection to DEVELOPER — focus moved` and does not get wiped a second later.
   Confirm you can still edit the prompt, then clear it.
3. **Multiline** — select 3+ lines and route them; confirm they arrive as one prompt, not several
   submissions.
4. **Mouse routing** — select text, **right-click** (including a trackpad tap, and a slow
   press-hold-release), and confirm the menu stays open on release. Pick *Send selection to
   CONSULTANT* by clicking the row; repeat picking it by number; repeat pressing Escape to
   confirm cancelling does nothing.
5. **Clipboard routing** — copy something (`Ctrl+Shift+C`), press `Ctrl+Alt+Shift+C`.
6. **Micro → Consultant** — `Alt+F`, open a file in Micro, select some text *in Micro*, press
   `Ctrl+C`, then `Alt+Enter` and choose **Send clipboard to CONSULTANT**. The text arrives and
   focus moves. Quit Micro and confirm you land back in Yazi.
7. **Two independent windows** — run `./bin/archidev2 ~/another/project` in a second terminal.
   Route text in each window and confirm it never lands in the other one's panes.
8. **Process exit** — exit Claude in the developer pane; confirm the one-line notice, a working
   shell, and that routing to DEVELOPER still finds that pane.
9. **Help** — `Alt+H`; check the shortcuts match what you actually pressed, and that clicking a
   *focus* row moves you there.

---

## Known limitations

* **No per-pane labels.** WezTerm cannot draw a title bar per pane
  ([#1970](https://github.com/wezterm/wezterm/issues/1970)); role labels live in the bottom
  status bar instead.
* **No clickable toolbar.** `InputSelector` is the only clickable control a WezTerm config can
  create. There are no free-floating buttons, and nothing here fakes them.
* **Clipboard routing cannot trim.** WezTerm's Lua API can write the clipboard but not read it,
  so clipboard transfers use WezTerm's own paste into the focused destination. Bracketed paste
  is used correctly, but a trailing newline in the clipboard cannot be stripped, and a program
  without bracketed-paste support may treat it as Enter. Selection routing has no such gap.
  Before pasting, ArchiDev2 confirms the destination really is focused and aborts otherwise.
* **Selection routing needs a WezTerm-level selection.** It works for text dragged in the
  CONSULTANT and DEVELOPER panes. It does **not** work from Micro: Micro's selection is internal
  and `get_selection_text_for_pane` returns nothing for it on the tested setup, with or without
  Shift-drag. Use Micro's `Ctrl+C` and a clipboard action instead.
* **`Alt+Shift` may be a keyboard-layout switcher** on some desktop setups. If so, rebind via
  `KEY_SEND_SELECTION_*`.
* If a WezTerm build lacks `pane:send_paste`, ArchiDev2 falls back to `send_text` (raw, still no
  Enter) and says so in the status bar rather than faking bracketed paste.
* **No spaces inside command arguments** — commands are whitespace-split argv lists.
* **Routing is per-window.** Extra tabs in an ArchiDev2 window can still route to its panes; a
  separate window reports `No CONSULTANT pane in this window` rather than guessing.
* Prototype scope: no plugin system, daemon, database, package manager, or Git UI.

---

## Future Windows note

Not implemented. WezTerm runs natively on Windows and this Lua config is platform-independent
apart from the `/usr/bin/env bash` pane runner, so a port would be a PowerShell equivalent of
`bin/archidev2` that resolves paths, reads the same config format from `%APPDATA%`, and either
launches native `codex.exe`/`claude.exe` with a `.ps1` pane runner or runs the existing Bash
runner through `wsl.exe` with paths translated by `wslpath`. The routing logic, pane identity
scheme, menus and key bindings would carry over unchanged.
