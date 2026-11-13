# ArchiDev2 — Qt native workspace

A personal, Linux-first, **CLI-launched** workspace for working with terminal AIs.

> Select text in any surface → click a destination → it is inserted without being submitted →
> focus follows it.

Three terminals (Consultant AI, Developer AI, Project Shell), one file explorer that switches
between the Consultant and Project folders, a tabbed **Notes + Editor**, and **source-local
routing controls**. Not an IDE, not a launcher app, not a
settings application.

```
┌──────────────────────────────────────────┬────────────────────────────────────┐
│ Consultant: [prof ▾][New][Resume…][Hide] │ Developer: [prof ▾][New][Resume…]  │
│ ┌──────────────┐[→ Notes][→ Shell][Dev →]│ [← Consultant] [→ Notes] [→ Shell] │
│ │FILES·PROJECT │                         │                                    │
│ │[Consultant|Project][↑]                 │ DEVELOPER terminal                 │
│ │[Open][File actions▾][⇅]│ CONSULTANT terminal │                                 │
│ │ tree         │                         │                                    │
│ └──────────────┘                         │                                    │
├──────────────────────────────────────────┴───────────┬────────────────────────┤
│ [→ Consultant] [→ Developer] [→ Shell] [→ Notes tab] │ [Restart Shell]        │
│ EDITOR  main.cpp  ● unsaved changes  — /path         │ [→ Consultant]         │
│ [Open Notes][Save][Reload][Find]        Ln 3, Col 5  │ [→ Developer][→ Notes] │
│ [Notes][main.cpp ●][util.py ✕]                       │ PROJECT SHELL          │
│  1 int main() {          (line numbers, colouring)   │                        │
└──────────────────────────────────────────────────────┴────────────────────────┘
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
# notes_file: a plain text file shown in the pinned Notes tab. It is loaded
# at startup when it exists; "Open Notes" loads or creates it. Blank keeps
# Notes as a scratch buffer that is not saved.
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
visible=true
root=consultant
consultant_current=
consultant_sort=name_asc
consultant_allow_above_start=true
project_current=
project_sort=name_asc
project_allow_above_start=false

[layout]
top_consultant=55
top_developer=45
consultant_files=24
consultant_term=76
bottom_editor=68
bottom_shell=32
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

Each source gets buttons for the other surfaces only, so nothing can target itself. The editor's
**→ Notes tab** appears only while a file tab is showing:

| Source | Buttons | Reads |
|---|---|---|
| Consultant | → Notes, → Shell, Developer → | `consultantTerminal->selectedText(true)` |
| Developer | ← Consultant, → Notes, → Shell | `developerTerminal->selectedText(true)` |
| Notes / Editor | → Consultant, → Developer, → Shell, → Notes tab | the **active tab's** selection |
| Project Shell | → Consultant, → Developer, → Notes | `shellTerminal->selectedText(true)` |

**The editor area routes from whichever tab is showing**, and the status line names it:
`Sent 6 chars from EDITOR main.cpp to DEVELOPER`, or `from NOTES` when Notes is showing. From a
file tab, **→ Notes tab** copies the selection into Notes; it is hidden while Notes itself is
showing. **Every → Notes button (from Consultant, Developer or Shell) always targets the pinned
Notes tab**, inserting at the Notes cursor and showing that tab, even when a code tab was
active. A file tab is never written to by routing.

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
- In Consultant and Developer, `Ctrl+C` copies a selection; with no selection it interrupts
  the running CLI. `Ctrl+V` pastes clipboard text without submitting it. Right-click opens
  Copy selection and Paste. `Ctrl+Shift+C/V` remain available. The Project Shell keeps its
  usual terminal `Ctrl+C` behaviour and the `Ctrl+Shift+C/V` clipboard shortcuts.
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

Before and after the insert, the destination pane is scrolled to its prompt. A terminal follows
new output only while its view is at the bottom, and `sendText()` (unlike a typed key) does not
scroll it back, so with a long Codex transcript scrolled up the text used to arrive out of sight
while the status said "Sent". (`QTermWidget::scrollToEnd()` alone does not repaint a pane whose
program stays silent, so the display's own scroll bar is moved instead.) The status line then says
"Sent N chars from X to Y - not submitted, scrolled to its prompt". "Sent" means the bytes reached
the program; if it has not redrawn its screen within 1.5 s — busy, showing a dialog, or simply not
echoing — a warning follows: *"Y has not redrawn since the insert … Check its prompt before
pressing Enter."* Ctrl+V paste into a terminal scrolls to the prompt the same way.

Live check (Xephyr, real Codex 0.167.2 `--no-alt-screen`, traced with strace): an 821-char
selection of AGENTS.md routed from an editor tab arrived as one bracketed paste, byte-exact, and
Codex showed it in the composer. With the pane scrolled up, the old build left the view on old
history (reproduced with a 300-line recorder pane); the fixed build returns to the prompt, also with
real Codex scrolled up. Not reproduced: the exact state of the user's own long Consultant session.

## Files explorer

One stock `QFileSystemModel` + `QTreeView` — lazy, threaded, self-updating, no custom model or
crawler. The only custom model code is a small `ExplorerSortProxy` that keeps folders first.
It sits inside the Consultant area, left of its terminal, and **Hide Files** hides it. The
bottom-row Project Files explorer of earlier versions is gone; its width went to the editor.

```
FILES · PROJECT
[Consultant|Project] [↑]      root switch, Parent
[Open] [File actions ▾] [⇅]   open, insert or copy, sort menu
…/current/path
tree
```

**Two roots, one pane.** *Consultant* is `consultant_directory`; *Project* is the launched
project.
- **Clicking the root that is not showing** switches to it and reopens it where you left it:
  the same folder and the same selected file.
- **Clicking the root that is showing** goes back to its top.
- The header names the root on show: `FILES · CONSULTANT` or `FILES · PROJECT`.
- **Labels:** when the pane is narrow, the root labels shorten to `Cons.` / `Proj.`. Open, File actions
  and the sort button keep their natural width; the pane gets a minimum width instead of eliding
  them.

Each root keeps its own:
- **Folder:** `*_current`, saved on close. A saved folder that is missing or out of bounds falls
  back to that root.
- **Sort order:** `*_sort`. There are six modes: name A→Z / Z→A, modified newest / oldest, size
  largest / smallest. Folders always come first, with name as the tiebreaker.
- **Boundary:** `*_allow_above_start`. With `false` (the Project default), **Parent** stops at
  the root and is disabled there, comparing canonical paths so a symlink cannot step outside.
  With `true` (the Consultant default), it can walk up to `/`.

**Explorer navigation never changes a terminal's working directory**; it only changes the view.
The active root is saved as `root=`.

**Open** opens the selected file in its own editor tab; double-clicking does the same. Notes is
never replaced.

**File actions ▾** acts on the selected item. The first two actions insert the file's text into
an AI prompt; the Shell action inserts its path. They do not press Enter:

| Action | What is sent |
|---|---|
| Insert file contents in Consultant prompt | the file's text, inserted with the bracketed-paste path and never submitted |
| Insert file contents in Developer prompt | the same, into Developer |
| Insert file path in Shell | the absolute path, single-quoted when it contains spaces or shell characters; never run |
| Copy file path | the absolute path, to the system clipboard |

The Contents actions are disabled for folders. They refuse non-UTF-8 or NUL-containing files,
and anything over 256 KB ("open it in the editor instead"). Above 16 KB they ask first, with
Cancel as the default. Trailing newlines are dropped, so nothing can submit the prompt.

## Notes + Editor

A tab bar with a pinned **Notes** tab first. Every file you open gets its own tab after it.

**Notes is a distinct document.** It is never closed (it has no close button), never moved, and
never replaced by an opened file.
- **With `notes_file` set:** the file is loaded into Notes at startup when it exists. **Open
  Notes** loads it, or offers to create it.
- **Without it:** Notes is a scratch buffer. It is not a file and cannot be saved; you're asked
  before its edits are discarded.
- **Style:** Notes keeps the proportional font, word wrap and no line numbers, and its tab label
  is amber.
- **Notes file opened from Files:** opening the configured notes file from Files shows the Notes
  tab instead of opening a second copy.

**File tabs** use a fixed-width font, no wrap, line numbers, current-line highlight and basic
syntax colouring.
- **Colouring:** keywords, strings, comments, numbers, Markdown headings and code spans, and
  markup tags. It covers C-family files (C/C++, Java, JS/TS, Rust, Go, C#, QML, CSS…), Python,
  shell (by extension or `#!` line), JSON, Markdown, HTML/XML, and `#`-comment config files
  (INI, TOML, YAML, CMake, Makefile…).
- **No colouring:** other files and anything over 1 MB.
- **One tab per file:** opening an already open file switches to its tab. Two files with the
  same name are labelled with their folder, e.g. `main.cpp — src`.

**State is always visible:**
- A tab with unsaved changes shows `●`.
- A tab whose file changed or was deleted on disk turns red. This is checked when the tab
  changes, the cursor moves, or the window becomes active again.
- The header leads with the name and state: `EDITOR  main.cpp  ● unsaved changes  ⚠ changed on
  disk…  (1 other tab(s) unsaved)  — /full/path`. The path comes last, so a narrow pane clips
  that, not the markers.
- The toolbar shows the file type and `Ln, Col`. The window title names the active document.

**Toolbar and keys**

| Control | Action |
|---|---|
| **Save** / `Ctrl+S` | saves the active tab |
| **Reload** | re-reads the active tab's file; asks first if it has unsaved changes |
| **Find** / `Ctrl+F` | find bar for the active tab |
| `Enter` / `F3` | next match |
| `Shift+Enter` / `Shift+F3` | previous match |
| **Aa** | match case |
| `Esc` | closes the find bar |
| `Ctrl+W` | closes the active file tab |

The find bar counts matches ("2 of 5") and wraps around.

**Every editable document has the same safeguards**, Notes included:
- **On open or reload:** refuses unreadable, non-UTF-8, NUL-containing and oversized (> 8 MB)
  files, and leaves what is open untouched.
- **On save:** writes atomically with `QSaveFile`, and keeps the file's trailing-newline
  convention.
- **Changed on disk:** if the file changed since it was opened or last saved, Save asks
  "Overwrite it?" (Cancel by default) before writing.
- **Closing:** closing a tab with unsaved changes asks Save / Discard Changes / Cancel. Closing
  the window asks the same, once per unsaved tab; Cancel at any point keeps the window open.

Routing text into Notes marks it edited like any other change.

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

Splitter proportions, explorer visibility, the active root, and each root's folder and sort mode
are stored in `[layout]` and `[explorers]`. They are written **once on close** rather than
continuously while navigating or dragging. A saved folder that no longer exists, or falls outside
a bounded root, reverts to that root.

Older INIs carry `bottom_project_files` and `bottom_notes`, plus `consultant_visible` and
`project_visible`. When `bottom_editor` is missing, the editor gets `bottom_project_files +
bottom_notes`, so the freed width goes to the editor. `visible` falls back to
`consultant_visible`. The old keys are left in place and ignored afterwards. Open tabs are not
persisted. Impossible
values are ignored in favour of the compiled defaults. Editor contents are never persisted.

## Verified automatically

Clean build, zero warnings. Sandboxed, disposable projects, harmless `/bin/bash` profiles:
INI template creation, first-run non-interactive exit, `--consultant-dir`, custom and malformed
profiles, multi-argument profiles, three distinct terminals with correct cwds, per-role restart
isolation (each changes only its own PID, routing still reaches it, no orphans), routing from
the editor to all three terminals without execution, notes via a relative path, atomic save,
trailing-newline preservation, and a comment-preserving INI round-trip.

The tabbed editor and single explorer were checked live on a separate X display, driven with
synthetic mouse and keyboard input. The test projects used recorder programs in place of the AIs
and shell, so every inserted byte could be compared. Covered:
- switching roots, Parent stopping at the Project boundary, and per-root folder and selection
  memory across a close and reopen;
- several unsaved tabs, a save conflict (cancel, then overwrite), and Reload;
- find;
- file sends: contents to Consultant and Developer byte for byte without Enter, the 16 KB
  confirmation, and refusals for 300 KB, binary and folder;
- a quoted path to Shell, and Copy Path;
- routing from a code tab, AI → Notes while a code tab is active, and Notes file loading;
- close prompts;
- the layout at 1100×700 and at the 1043×640 minimum.

After the merge to `main` (2026-11-13), the rebuilt `build/qt-spike` binary was rechecked on a
separate X display. Consultant ran **real Codex 0.167.2** (`--no-alt-screen`, input traced with
strace); Developer and Shell ran recorders. Covered:
- routing: an editor selection to Codex (one bracketed paste, byte-exact, shown, no Enter);
  Developer → Notes while a code tab was active; editor → a scrolled-up Developer (byte-exact,
  view back at the prompt);
- File actions: a 22 KB file into Developer after the confirmation (byte-exact), a quoted path to
  Shell, Copy file path, refusals for 300 KB and binary files, and contents disabled for folders;
- copy and paste: Ctrl+C with a selection in Codex copied without interrupting it; Ctrl+V,
  Ctrl+Shift+V and right-click Paste delivered clipboard text to Codex (bracketed) and Developer
  without Enter; right-click Copy selection in Developer; Ctrl+C with no selection sent `0x03`
  to both;
- the offscreen routing self-test.

Codex's own startup "Update available" dialog was open for the first insert. Codex did not read
the text while the dialog was up, and the text never reached its prompt. The status line flagged
this with the "has not redrawn" warning. After the dialog was skipped, the same insert worked.

## Needs your eyes

The three-terminal layout and proportions; the dark theme and whether the active-surface border
reads clearly; the Files explorer's width and root switch with your real folders; the tabbed
editor with your own files (colouring, find, unsaved markers); collapse/restore; the notes flow
including the creation prompt; restarting into **real** Codex/Claude sessions from the dropdowns; routing
between all four surfaces by hand; multiline safety in a live AI prompt; and modal dialog
readability. In your normal window: edge-drag speed and wheel-while-dragging with your own
mouse or touchpad, and Resume… into a real conversation in each role.

## Known limitations

- **A literal `$` cannot be passed in a profile field.** QTermWidget's
  `Session::setProgram/setArguments/setInitialWorkingDirectory` all run `ShellCommand::expand()`,
  and its `\$` escape leaves the backslash behind. ArchiDev2 warns rather than surprising you.
- No Save As, no replace, no language server, no settings screen. Syntax colouring is one line
  at a time with a small keyword list per file type, so it is approximate.
- Tabs cannot be reordered, and open tabs are not restored after a restart.
- The explorer does not watch for renames of a file open in a tab. A tab whose file disappears
  turns red, and Save recreates the file at the old path.
- The Developer and Shell directories are the launched project; only the consultant directory is
  configurable.
- Notes creation, unsaved-changes, New Session and Resume dialogs are modal, so scripted runs
  block on them.
- Text inserted while a CLI shows its own dialog (for example Codex's update prompt) can be
  dropped by that CLI. ArchiDev2 cannot see the dialog. It warns when the pane does not redraw,
  but a CLI that redraws anyway would not trigger the warning.
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
