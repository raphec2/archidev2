# ArchiDev2 — Qt native workspace

A personal, Linux-first, **CLI-launched** workspace for working with terminal AIs.

> Select text in any surface → click a destination → it is inserted without being submitted →
> focus follows it.

Three terminals (Consultant AI, Developer AI, Project Shell), two file explorers, one shared
Notes/Editor, and **source-local routing controls**. Not an IDE, not a launcher app, not a
settings application.

```
┌──────────────────────────────────────────┬────────────────────────────────────┐
│ Consultant AI: [profile ▾][Restart][Hide]│ Developer AI: [profile ▾][Restart] │
│ [→ Developer] [→ Notes] [→ Shell]        │ [→ Consultant] [→ Notes] [→ Shell] │
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
restarting *is* saved, because that is a deliberate workspace choice.

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
arguments=

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
`--consultant-dir`, or by prompting on the terminal when stdin is a TTY. There is **no desktop
directory chooser** — this is a CLI product. Non-interactive with no consultant directory, it
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

## Roles, shell and restart

```
Consultant AI: [profile ▾] [Restart] (Restart to apply)   Developer AI: [profile ▾] [Restart]   [Restart Shell]
```

Changing a dropdown never touches the running session — it only shows `Restart to apply`.
Restart always confirms first, validates the executable *before* disturbing anything, hangs up
only that role's own child PID, replaces the `QTermWidget` in its own frame, and reconnects the
focus and selection signals. The other two terminals are untouched.

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
| Consultant | → Developer, → Notes, → Shell | `consultantTerminal->selectedText(true)` |
| Developer | → Consultant, → Notes, → Shell | `developerTerminal->selectedText(true)` |
| Notes | → Consultant, → Developer, → Shell | `editor->textCursor().selectedText()` |
| Project Shell | → Consultant, → Developer, → Notes | `shellTerminal->selectedText(true)` |

The selection is read live at click time, never cached and never borrowed. A source with nothing
selected reports `Nothing selected in CONSULTANT` (or DEVELOPER / NOTES / PROJECT SHELL) rather
than falling back. Each group's buttons enable and disable from their own source's selection
signal. Buttons use `Qt::NoFocus`, so clicking one cannot disturb the selection it is about to
read.

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
readability.

## Known limitations

- **A literal `$` cannot be passed in a profile field.** QTermWidget's
  `Session::setProgram/setArguments/setInitialWorkingDirectory` all run `ShellCommand::expand()`,
  and its `\$` escape leaves the backslash behind. ArchiDev2 warns rather than surprising you.
- No Save As, no tabs, no syntax highlighting, no settings screen, no third explorer.
- The Developer and Shell directories are the launched project; only the consultant directory is
  configurable.
- Notes creation and restart confirmations are modal, so scripted runs block on them.

## Attribution and licensing

Not legal advice.

- **ArchiDev2's own code: `GPL-2.0-or-later`** — full text in the top-level `LICENSE`; sources
  carry `// SPDX-License-Identifier: GPL-2.0-or-later`.
- **[QTermWidget](https://github.com/lxqt/qtermwidget)** (LXQt, derived from KDE Konsole) — GPL.
- **[Qt 5](https://www.qt.io/)** — LGPLv3 / GPL / commercial.

Linking QTermWidget means distributed builds must comply with GPL terms. No third-party source
is vendored; both libraries are used as installed system packages.
