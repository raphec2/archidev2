# ArchiDev2 — Qt native workspace

A personal, Linux-first, **CLI-launched** workspace for working with terminal AIs.

> Select text in any surface → click a destination → it is inserted without being submitted →
> focus follows it.

Three terminals (Consultant AI, Developer AI, Project Shell), two file explorers, one shared
Notes/Editor, and **source-local routing controls**. Not an IDE, not a launcher app, not a
settings application.

```
┌──────────────────────────────────────────┬────────────────────────────────────┐
│ Consultant: [prof ▾][New][Resume…][Hide] │ Developer: [prof ▾][New][Resume…]  │
│ [→ Notes] [→ Shell]       [Developer →]  │ [← Consultant] [→ Notes] [→ Shell] │
│ ┌───────────────┬──────────────────────┐ │                                    │
│ │ CONSULTANT    │ CONSULTANT terminal  │ │ DEVELOPER terminal                 │
│ │ FILES         │                      │ │                                    │
│ └───────────────┴──────────────────────┘ │                                    │
├──────────────────┬───────────────────────┴────────────┬───────────────────────┤
│ PROJECT FILES    │ NOTES / EDITOR                     │ PROJECT SHELL         │
│ ↑ Parent ⌂ Start │ [Open Notes] [Save]                │ [Restart Shell]       │
│ [Sort ▾] /path   │ [→ Consultant] [→ Developer]       │ [→ Consultant]        │
│ [Open in Notes]  │ [→ Shell]                          │ [→ Developer] [→ Notes]│
└──────────────────┴────────────────────────────────────┴───────────────────────┘
│ status                                                                         │
```

## Build and run

Requires Ubuntu packages only — nothing vendored: `cmake`, `g++`, `qtbase5-dev`,
`libqtermwidget5-1-dev`.

```sh
cmake -S spikes/qt -B build/qt-spike
cmake --build build/qt-spike --parallel

./build/qt-spike/archidev2-qt /path/to/project              # normal launch
./build/qt-spike/archidev2-qt .                             # current directory
```

Options (`--help` for the full list):

```
--consultant-dir <path>        consultant working directory (written to the INI on first run)
--consultant-profile <name>    this launch only
--developer-profile <name>     this launch only
--shell <program>              project shell program (default: $SHELL)
```

CLI role/profile values apply to the current launch only. Choosing a profile in the UI and
starting a New Session or Resume with it *is* saved, because that is a deliberate workspace choice.

### Shortcut command and desktop launcher (optional)

`spikes/qt/archidev2-launch` opens any project, detached from the terminal. You can run it from
the checkout. `spikes/qt/install-launcher` installs a copy as a short command, adds a menu entry,
and can add a desktop icon. The copy has two values filled in: this checkout's app, and your
usual project, if you give one. The menu entry and Qt window use the bundled
`archidev2.svg` icon, so the app has its own identity in the taskbar. Re-run the installer after
updating the checkout to refresh an existing menu or desktop entry.

Everything machine-specific goes into files under your home directory. Nothing is added to the
repository, and there is no project list.

```sh
spikes/qt/install-launcher --desktop-icon /path/to/usual/project  # "ad2" + menu entry + desktop icon
spikes/qt/install-launcher --uninstall                            # removes only files it generated
```

| Action | What opens |
|---|---|
| `ad2` | the usual project, or the folder picker if no usual project was given |
| `ad2 /path/to/project` | that project; extra app options pass through, e.g. `ad2 --developer-profile Codex ~/src/x` |
| `ad2 --choose`, the **ArchiDev2** menu entry, the desktop icon | a folder picker (zenity) |
| Menu entry → right-click → **Open <usual> directly** | the usual project, with no picker |

**Using the picker**
- It opens in the usual project's parent folder, with the usual project highlighted.
- To open the usual project, click **OK**, or press **Enter** twice (the first Enter goes into
  the folder).
- To open another project, pick its folder or type a path with **Ctrl+L**.
- **Cancel** or **Escape** opens nothing.

**A folder without `.archidev2.ini`** goes through the app's normal first-run setup. Nothing
opens a different project instead.
- **From a terminal:** you're asked for the Consultant directory (Tab completes the path). A
  path that isn't a folder is asked again, and an empty answer cancels.
- **From the picker:**
  1. A dialog names the INI that will be created; **Cancel** is the default.
  2. **Set Up…** then asks for the Consultant directory in a second picker.
- **Either way,** the launcher then runs the app with `--consultant-dir`, and the app writes the
  project's INI. Cancelling creates nothing.

**If the app stops while starting**, for example because the INI points at a missing
Consultant directory, the launcher shows the app's message and exit code. From the menu, this
appears in a dialog instead of disappearing.

The launcher watches the app for its first 1.5 seconds. It runs the app in its own session
(`setsid`), so closing the terminal does not close ArchiDev2.

| File | Purpose |
|---|---|
| `~/.local/bin/<name>` (default `ad2`, `--name` to change) | copy of `archidev2-launch` with the app path and usual project filled in |
| `~/.local/share/applications/archidev2-<name>.desktop` | menu entry "ArchiDev2"; right-click → *Add to panel* for a one-click button |
| `<Desktop>/archidev2-<name>.desktop` | with `--desktop-icon`; marked trusted so Cinnamon will run it |

Also:
- **Every launch opens a new window.** Windows you already have open are not affected.
- **Settings come only from the project's own INI.** The installer refuses a usual project
  that has no INI, and refuses to overwrite a command it did not generate.
- **nvm:** desktop sessions don't read `~/.bashrc`. CLIs installed with nvm (Codex here) would
  then be missing, or an older system `node` would win. So when nvm isn't loaded yet, the
  launcher loads nvm's default Node first.
- **Dialogs need `zenity`.** It's installed with Cinnamon; otherwise `sudo apt install zenity`.
- **The launcher does not build anything.** Rebuild after pulling changes. A running window
  keeps the code it started with until you reopen it.
- **Installer changes need a re-run.** After changing `archidev2-launch`, re-run
  `install-launcher` so the installed copy matches.

### One packaging workaround

Ubuntu's `qtermwidget5` metadata is broken twice: its `.pc` requires a `libutf8proc.pc` the
distro does not ship, and its CMake target propagates the dev-only symlink
`/usr/lib/.../libutf8proc.so`. `libqtermwidget5.so` already links `libutf8proc.so.3` itself, so
`CMakeLists.txt` drops propagated absolute link paths that do not exist. Alternative:
`sudo apt install libutf8proc-dev`.

## Configuration is project-local, always

Everything lives in **`<project>/.archidev2.ini`**. ArchiDev2 reads and writes **no** central or
user-wide configuration: no `~/.config/ArchiDev2`, no `~/.config/archidev2`, no central profile
JSON, no workspace registry, no recent-projects list. The only `QSettings` object in the codebase
is constructed against that explicit path.

> Earlier iterations used `~/.config/ArchiDev2/ArchiDev2.conf` and
> `~/.config/archidev2/ai-profiles.json`. Those are now **unused**. Nothing migrates them
> automatically and nothing deletes them — remove them by hand if you want to.

On first run the file is created with explanatory comments:

```ini
# ArchiDev2 project-local configuration.
# This is the ONLY place ArchiDev2 stores settings - nothing is written
# to your home directory. It may contain machine-specific absolute paths,
# so add .archidev2.ini to .gitignore if it should stay local.

[workspace]
consultant_directory=/path/to/consulting
# notes_file: a plain text file opened by the "Open Notes" button.
# Relative paths resolve against consultant_directory.
notes_file=

[roles]
consultant_profile=Codex
developer_profile=Claude

[profiles/Codex]
program=codex
arguments=--no-alt-screen

[profiles/Claude]
program=claude
arguments=

[shell]
# Blank uses $SHELL, falling back to /bin/bash.
program=
arguments=-l

[explorers]
consultant_visible=true
consultant_current=
consultant_sort=name_asc
consultant_allow_above_start=true
project_visible=true
project_current=
project_sort=name_asc
project_allow_above_start=false

[layout]
top_consultant=55
top_developer=45
consultant_files=32
consultant_term=68
bottom_project_files=22
bottom_notes=48
bottom_shell=30
```

**Writes preserve your comments.** `QSettings::sync()` would rewrite the whole file in its own
representation — discarding every comment and turning `[profiles/Codex]` into `[profiles]` with
`Codex\program=`. Since this file is the only settings surface and is meant to be hand-edited,
ArchiDev2 reads through QSettings but writes by editing the INI text in place, so the sections
above are exactly what you keep seeing.

### First run

```
archidev2 /path/to/project
```

If `.archidev2.ini` is absent, ArchiDev2 creates it, then resolves the consultant directory from
`--consultant-dir`, or by prompting on the terminal when stdin is a TTY. The app has **no desktop
directory chooser** of its own; the optional launcher above provides the dialogs. Non-interactive with no consultant directory, it
writes the template, prints the exact command to run, and exits 3:

```
archidev2 --consultant-dir /path/to/consulting /path/to/project
```

If the project directory is not writable it fails clearly and never falls back to central
storage.

## AI profiles

An AI is only a display name, a program and an argument list. No provider adapters, no SDKs, no
model-specific behaviour. Add any interactive CLI:

```ini
[profiles/Gemini]
program=gemini
arguments=

[profiles/LocalQwen]
program=ollama
arguments=run, qwen3
```

Gemini and Ollama are **examples only** — nothing here checks that they are installed or behave
well in a terminal.

`arguments` is a comma-separated list; QSettings returns a single value as a string and several
as a list, and both are accepted. Names must be unique and `program` non-empty; each rejected
entry is reported individually in the status line and never hides the valid ones. Built-in
`Codex` and `Claude` are appended only for names the INI does not define. Bare programs resolve
through `PATH`; absolute paths are used as given. No shell is ever involved.

## Roles, new sessions and resume

```
Consultant AI: [profile ▾] [New Session] [Resume…]    Developer AI: [profile ▾] [New Session] [Resume…]    [Restart Shell]
```

Each dropdown starts out showing the profile that is actually running. Changing it never
touches the running session — it only shows `Applies on New Session or Resume`.

**New Session** always starts a *new* conversation. It confirms first (Cancel is the default),
validates the executable *before* disturbing anything, hangs up only that role's own child PID,
replaces the `QTermWidget` in its own frame, and reconnects the focus and selection signals.
The other two terminals are untouched.

**Resume…** continues an earlier conversation after a crash or restart, using the CLI's own
history and its own resume option. ArchiDev2 records nothing about sessions. It works when the
selected profile's program is `codex` or `claude` (by file name, so custom profiles such as a
`codex` with extra arguments qualify); for any other program it says so and does nothing.

- The dialog lists the conversations that CLI recorded **for this role's working directory**
  (Consultant: the consultant directory; Developer: the project), most recently active first,
  with last-active time, title, first prompt, start time and full session ID. A filter box
  narrows the list.
- **Nothing is preselected.** *Resume Selected Conversation* stays disabled until you pick a
  row, Enter in the filter box does nothing, and Cancel leaves the running session untouched.
- The chosen ID must be a UUID and is passed as one argv entry, never through a shell:
  - Codex: `codex resume <profile arguments> <id>` (so `--no-alt-screen` is kept)
  - Claude: `claude <profile arguments> --resume <id>`

  No prompt argument is ever added, so nothing is typed or submitted. The pane header shows
  `resumed <id>` (or `new session`) and the status line names the conversation.
- *Copy Resume Command* copies the equivalent `cd <dir> && …` line for use in another terminal.
- If the conversation is still open somewhere else, close it there first; neither CLI offers a
  dependable way to detect that.

Where the lists come from (private formats, read as observed in Codex CLI 0.166.0 and Claude
Code 2.1.317; if a format changes, sessions just stop being listed):

| CLI | Store | Title |
|---|---|---|
| Codex | `$CODEX_HOME` (default `~/.codex`) `sessions/YYYY/MM/DD/rollout-*.jsonl`; first record's `cwd` | `session_index.jsonl` `thread_name` |
| Claude | `$CLAUDE_CONFIG_DIR` (default `~/.claude`) `projects/<dir with / as ->/<id>.jsonl`; each file's `cwd` is checked | `custom-title`, else `ai-title`, else `summary` |

Non-interactive `codex exec` runs and Claude conversations with no real user message are not
listed. **Claude Code deletes transcripts older than 30 days by default** (it did so on this
machine on 2026-11-03), so those can no longer be resumed; raise `cleanupPeriodDays` in
`~/.claude/settings.json` to keep more. Codex kept its history.

The **Project Shell** is your normal `$SHELL` (`-l` by default) in the project directory, for
builds, tests, Git and package installs. It has no AI profile, has its own Restart, and is
unaffected by AI restarts. It is a full routing source *and* destination.

## Routing is source-local

**Every group of routing buttons belongs to one source surface and reads only that surface's
selection.** There is no global "last selected surface" — that model is gone, not merely
disabled. Consultant's buttons always send Consultant's text even if Notes, Developer and Shell
all hold selections at the same time.

Each source gets buttons for the other three surfaces only, so nothing can target itself:

| Source | Buttons | Reads |
|---|---|---|
| Consultant | → Notes, → Shell, Developer → | `consultantTerminal->selectedText(true)` |
| Developer | ← Consultant, → Notes, → Shell | `developerTerminal->selectedText(true)` |
| Notes | → Consultant, → Developer, → Shell | `editor->textCursor().selectedText()` |
| Project Shell | → Consultant, → Developer, → Notes | `shellTerminal->selectedText(true)` |

The selection is read live at click time, never cached and never borrowed. A source with nothing
selected reports `Nothing selected in CONSULTANT` (or DEVELOPER / NOTES / PROJECT SHELL) rather
than falling back. Routing buttons remain enabled because QTermWidget does not reliably signal
mouse selection changes. Buttons use `Qt::NoFocus`, so clicking one cannot disturb the selection
it is about to read. In the two AI terminal panes, left-button drag selects locally even when
the child program enables mouse reporting; wheel events still reach the program for scrolling.

### Scrolling and selecting in the AI panes

What the CLIs ask of the terminal decides who handles the mouse. Captured from their startup
output: `codex --no-alt-screen` and `claude` turn on **no** mouse reporting and no alternate
screen, so drags and the wheel go to QTermWidget and scroll its own history. Plain `codex`
uses the alternate screen with full mouse reporting (`?1049h`, `?1000/1002/1003/1006h`), so
the wheel goes to Codex instead.

In the Consultant and Developer panes:

- **Wheel** scrolls the history 3 lines per notch (the desktop setting). **Shift+wheel**
  scrolls a page. **Shift+PgUp/PgDn/Home/End** and **Shift+Up/Down** scroll
  from the keyboard.
- **Drag past the top or bottom edge** to keep extending the selection. ArchiDev2 speeds up
  QTermWidget's edge scrolling the longer the pointer stays outside: roughly 25 lines/s at
  first, several hundred after a few seconds. Holding one line above the edge for 3 s selected
  ~629 lines, against ~73 with QTermWidget alone.
- **Hold the button and use the wheel**: the selection follows each wheel step even without
  moving the mouse, so *press, wheel, release* selects across any number of screens.
- On release the status line reports how many lines and characters are selected.
- Routing a selection larger than 16 KB into a terminal asks first (Cancel is the default).
  Routing to Notes and `Ctrl+Shift+C` do not ask.

Shift+click to extend an existing selection is not offered: QTermWidget 1.4.0's public
`setSelectionStart` always switches to rectangular selection, and its selection anchor is
private.

Dragging past the top or bottom edge of a terminal scrolls its history and keeps extending the
selection while the pointer stays outside, so a selection can span many screens. That only works
for text the terminal actually holds. The alternate screen has no history: a program drawing
there (Codex by default) keeps everything off-screen to itself, and its wheel scrolling redraws
the pane rather than scrolling the terminal. The Codex profile therefore passes
`--no-alt-screen`, which writes the transcript into ordinary scrollback. The two AI terminals
keep 20000 lines of history (QTermWidget's default is 1000); the Project Shell keeps the default.
If a drag leaves a pane that has no scrollback, the status line says so. An existing INI that
still has `arguments=` under `[profiles/Codex]` needs `--no-alt-screen` added by hand.

QTermWidget 1.4.0 does not reflow on resize (`Screen::resizeImage` only truncates or pads), and
its screen classes are not public API. Inline Codex does the reflow itself: once a resize
settles it clears the screen and scrollback (`CSI 2J`, `CSI 3J`) and reprints the transcript at
the new width, which QTermWidget honours. A resize therefore also clears any selection.

Inline Codex writes its transcript with a top-anchored scroll region and newlines, which
QTermWidget saves to history line by line: a captured 150-line Codex reply replayed through
QTermWidget kept every line, in order. (QTermWidget's `CSI n S` keeps only one of n lines, but
Codex does not use it.) Codex places each wrapped row itself, so copied or routed paragraphs
contain line breaks at the pane width; QTermWidget cannot tell those from real ones.

For terminal destinations the text has trailing CR/LF stripped (interior newlines preserved),
goes through `QTermWidget::bracketText()` — which wraps in `\e[200~…\e[201~` only when the
running program enabled bracketed paste — and is delivered with `sendText()`. **No Enter is ever
appended**, and the desktop clipboard is never used for internal routing.

## Explorers

Both are stock `QFileSystemModel` + `QTreeView` — lazy, threaded, self-updating, no custom model
or crawler. The only custom model code is a small `ExplorerSortProxy` that keeps folders first.

**Consultant Files** is attached inside the Consultant area, left of its terminal, and can be
hidden with **Hide Files**. **Project Files** sits at the bottom left. Each has its own toolbar:

```
[↑ Parent] [⌂ Start] [Sort ▾]  /current/path   [Send File → Consultant] [Open in Notes]
```

*Start* is where the explorer opens and where ⌂ returns to; *Current* is where it is looking now.
**Parent** walks up from Current. With `allow_above_start=false` (the Project default) it stops
at Start, comparing canonical paths so a symlink cannot step outside; with `true` (the Consultant
default) it can walk to `/`. Either way **explorer navigation never changes a terminal's working
directory** — it only changes the view.

Six sort modes — name A→Z / Z→A, modified newest / oldest, size largest / smallest — with folders
always first and name as the tiebreaker. Each explorer sorts independently, keeps its mode while
navigating, and falls back to `name_asc` on an unrecognised value.

**Send File → Consultant** (Consultant Files only) inserts the selected text file straight into
the Consultant prompt using the same bracketed-paste path, never submitted. It refuses
directories, non-UTF-8/NUL files and anything over 256 KB, and asks for confirmation above 16 KB.
Double-clicking a file opens it in Notes; sending to Consultant stays an explicit button.

## Notes / Editor

One shared document, opened from either explorer or from **Open Notes**: explicit **Save** /
`Ctrl+S`, dirty marker in the header and title, atomic writes via `QSaveFile`, an overwrite prompt if the file changed on disk, and
refusal of non-UTF-8, NUL-containing, oversized (>8 MB) or unreadable files with the current
document left open. A file's trailing-newline convention is preserved.

**Open Notes** opens `notes_file` in that same editor — relative paths resolve against the
consultant directory. If it is missing you are offered to create it; if unset, the status line
says to set `notes_file` in the INI. There is no separate notes editor. Routing text into Notes
marks the document dirty like any other edit.

## Project Shell

Bottom right: your `$SHELL` (`-l` by default) in the project directory, for builds, tests, Git
and package installs. No AI profile, its own **Restart Shell**, unaffected by AI restarts, and a
full routing source and destination.

## Dark theme

One built-in dark theme, no chooser: black surfaces, blue outlines, white text. Applied as a
dark `QPalette` plus a focused stylesheet covering panes, headers, trees, editor, dropdowns,
buttons, status line, splitter handles, scrollbars, menus and dialogs. Disabled controls stay
legible. The focused surface gets a bright blue border and a highlighted header; the others are
muted. Terminals use the installed **`WhiteOnBlack`** scheme (verified in
`/usr/share/qtermwidget5/color-schemes`: background `0,0,0`, foreground `255,255,255`).

## Layout persistence

Splitter proportions, explorer visibility, each explorer's current directory and each explorer's
sort mode are stored in `[layout]` and `[explorers]`, written **once on close** rather than
continuously while navigating or dragging. A saved current directory that no longer exists or
falls outside a bounded root silently reverts to Start. Impossible
values are ignored in favour of the compiled defaults. Editor contents are never persisted.

## Verified automatically

Clean build, zero warnings. Sandboxed, disposable projects, harmless `/bin/bash` profiles:
INI template creation, first-run non-interactive exit, `--consultant-dir`, custom and malformed
profiles, multi-argument profiles, three distinct terminals with correct cwds, per-role restart
isolation (each changes only its own PID, routing still reaches it, no orphans), routing from
the editor to all three terminals without execution, notes via a relative path, atomic save,
trailing-newline preservation, and a comment-preserving INI round-trip.

## Needs your eyes

The three-terminal layout and proportions; the dark theme and whether the active-surface border
reads clearly; Consultant vs Project explorer widths; collapse/restore; the notes flow including
the creation prompt; restarting into **real** Codex/Claude sessions from the dropdowns; routing
between all four surfaces by hand; multiline safety in a live AI prompt; and modal dialog
readability. In your normal window: edge-drag speed and wheel-while-dragging with your own
mouse or touchpad, and Resume… into a real conversation in each role.

## Known limitations

- **A literal `$` cannot be passed in a profile field.** QTermWidget's
  `Session::setProgram/setArguments/setInitialWorkingDirectory` all run `ShellCommand::expand()`,
  and its `\$` escape leaves the backslash behind. ArchiDev2 warns rather than surprising you.
- No Save As, no tabs, no syntax highlighting, no settings screen, no third explorer.
- The Developer and Shell directories are the launched project; only the consultant directory is
  configurable.
- Notes creation, New Session and Resume dialogs are modal, so scripted runs block on them.
- Resume lists only the role's own working directory and cannot tell whether a conversation is
  open in another terminal.

## Attribution and licensing

Not legal advice.

- **ArchiDev2's own code: `GPL-2.0-or-later`** — full text in the top-level `LICENSE`; sources
  carry `// SPDX-License-Identifier: GPL-2.0-or-later`.
- **[QTermWidget](https://github.com/lxqt/qtermwidget)** (LXQt, derived from KDE Konsole) — GPL.
- **[Qt 5](https://www.qt.io/)** — LGPLv3 / GPL / commercial.

Linking QTermWidget means distributed builds must comply with GPL terms. No third-party source
is vendored; both libraries are used as installed system packages.
