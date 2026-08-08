# ArchiDev2 — Qt/QTermWidget spike

A live-first experiment, not the product. It exists to answer one question before any real
application is built:

> Can ArchiDev2 read a selection from a terminal or an editor, insert it into another surface
> **without it being submitted**, and move focus — with persistent clickable buttons?

Routing is proven. This iteration adds the stock Qt file tree and single-document editing on
top of it. Still deliberately absent: AI profiles, settings, packaging and tests.

## Build and run

Requires (all from Ubuntu packages, nothing vendored):
`cmake`, `g++`, `qtbase5-dev`, `libqtermwidget5-1-dev`.

```sh
cmake -S spikes/qt -B build/qt-spike
cmake --build build/qt-spike --parallel

./build/qt-spike/archidev2-qt <project-dir> <consultant-dir> <consultant-prog> <developer-prog>
```

Both programs are resolved through `PATH` and started directly — **never through a shell
string**. It works with `/bin/bash` for both, which is how the routing was proven.

```sh
./build/qt-spike/archidev2-qt /tmp/archidev2-test /tmp/archidev2-consultant codex claude
```

### One packaging workaround

Ubuntu's `qtermwidget5` metadata is broken twice over: its `.pc` file requires a
`libutf8proc.pc` the distro does not ship, and its CMake target propagates the *dev* symlink
`/usr/lib/.../libutf8proc.so`, which lives only in `libutf8proc-dev`. `libqtermwidget5.so`
already links `libutf8proc.so.3` itself, so nothing here needs it. `CMakeLists.txt` therefore
drops propagated absolute link paths that do not exist. The alternative, if you prefer, is
`sudo apt install libutf8proc-dev`.

### Spike-only self-check

`ARCHIDEV2_SPIKE_SELFTEST=<file>` makes the app, 2 s after startup, route editor text to the
Developer terminal, then open `<file>`, append a line and save it — all through the real code
paths. It exists so "inserted but not submitted", opening and atomic saving can be proven
without a human drag. Remove it in the product.

## What it does

Two `QTermWidget`s (Consultant, Developer), a `QTreeView` file explorer, one `QPlainTextEdit`,
three persistent destination buttons, a Save button (`Ctrl+S`) and a status line, in nested
`QSplitter`s. The focused surface's header is highlighted.

**Selection source**, resolved fresh on every click: the focused surface if it has a live
selection, otherwise the most recently selected surface if its selection is still live,
otherwise any surface with one. The text is re-read at send time, so a selection that has since
disappeared is never routed as stale text — it reports `Nothing selected` instead. Sending to
the surface the text came from is refused.

**Buttons use `Qt::NoFocus`**, so clicking one cannot move focus off the source terminal. This
is reinforced by QTermWidget itself: its `focusOutEvent()` does not clear the selection — every
`clearSelection()` call in `TerminalDisplay.cpp` is in a mouse handler.

## Terminal insertion safety

The decisive criterion. Text going into a terminal is:

1. stripped of trailing `\r`/`\n` — interior newlines are preserved;
2. passed through `QTermWidget::bracketText()`, which wraps it in `\e[200~ … \e[201~` **only
   when the running program has enabled bracketed paste**;
3. sent with `QTermWidget::sendText()` — exactly those bytes.

No Enter, Return or newline is ever appended, and the desktop clipboard is not involved.

Verified against `/bin/bash`: routing `touch /tmp/ad2-selftest-EXECUTED\nsecond line\n` reported
44 characters (45 minus one stripped trailing newline) and **the file was not created** — the
text reached the prompt without being executed. The 12 extra bytes observed before the counter
was corrected were the two bracketed-paste markers, which confirms bash had the mode enabled and
the wrapping was applied.

## File tree and editor

`QFileSystemModel` + `QTreeView`, rooted at the project directory, name column only. Nothing is
custom: the model populates lazily on its own thread and keeps itself current through
`QFileSystemWatcher`, so there is no recursive crawler and large directories cost nothing up
front. Double-click a file to open it; double-clicking a directory just expands it.

The editor holds **one document at a time**. There is no Save As, no tabs, no syntax
highlighting.

### Exact dirty-document decisions

The document is dirty whenever `QTextDocument::isModified()` is true — typing *and* routing text
in both count. Dirty state shows as ` *` in the EDITOR header and the window title.

| Situation | Behaviour |
|---|---|
| Open a file, document clean | Opens immediately |
| Open a file, document dirty | Modal **Save / Discard / Cancel**. Cancel aborts the open. Save aborts the open if the save fails. |
| Close the window, document clean | Closes |
| Close the window, document dirty | Same three-way prompt; Cancel vetoes the close, and a failed save also vetoes it |
| Save with no file open | Refused: *"No file open - nothing to save"*. A routed scratch buffer cannot be saved in this spike. |
| Save when not dirty | No write at all; reports *"No changes to save"* |
| File changed on disk since it was opened or last saved | Modal **Overwrite / Cancel**, compared on mtime *and* size. Cancel leaves the file alone and the document dirty. |

### Exact failure behaviour

Every failure leaves the current document and the file on disk untouched, reports the reason in
the status line, and keeps the document dirty if it was.

| Failure | Behaviour |
|---|---|
| File is not valid UTF-8, or contains a NUL byte | Refused; the currently open document stays open |
| File larger than 8 MB | Refused, with the size and the limit named |
| File unreadable, or cannot be opened | Refused, with Qt's error string |
| Save fails (permissions, full disk, …) | `QSaveFile` discards its temp file, **the original is never truncated**, the document stays dirty, and the status reads `SAVE FAILED: … (file left unchanged, still unsaved)` |
| Directory double-clicked | Ignored; the tree expands it |

Saving is atomic: `QSaveFile` writes a temporary file alongside the target and renames it into
place only on `commit()`. A file's trailing-newline convention is preserved — `QPlainTextEdit`
has no notion of one, so editing the last line of a file that ended with `\n` would otherwise
silently strip it and produce a spurious diff. Files that arrived without a trailing newline do
not gain one.

## Verified live

Confirmed by hand against Codex and Claude: both render and accept input inside QTermWidget, a
normal mouse drag selects, `selectedText()` returns it, clicking a destination button preserves
and routes the selection, multiline text arrives unsubmitted, and focus moves.

## Still unproven — needs your eyes

This iteration's additions have only been exercised head­lessly, through the real code paths but
without a human at the window:

- the file tree rendering, expanding, and double-click opening a file;
- the three modal prompts — unsaved-changes on open, unsaved-changes on close, and
  changed-on-disk before overwrite. The close guard in particular cannot be reached by the
  automated runs, because `SIGTERM` bypasses Qt's `closeEvent` entirely.
- `Ctrl+S` reaching the Save button.

## Later, not now

The product will select AI commands from generic profiles — no provider adapters, no SDKs, just
programs and arguments:

```json
{ "profiles": [
    { "name": "Codex",      "program": "codex",  "arguments": [] },
    { "name": "Local Qwen", "program": "ollama", "arguments": ["run", "qwen3"] } ] }
```

Next, once this iteration has been used in anger: AI command profiles, then whatever daily use
actually demands.

## Attribution and licensing

Not legal advice.

- **ArchiDev2's own code: `GPL-2.0-or-later`.** Full text in the top-level `LICENSE`; source
  files carry `// SPDX-License-Identifier: GPL-2.0-or-later`.
- **[QTermWidget](https://github.com/lxqt/qtermwidget)** by the LXQt project, derived from KDE's
  Konsole — **GPL**. Its core files (`TerminalDisplay`, `Emulation`, `Session`, `Screen`,
  `Vt102Emulation`) carry GNU General Public License headers.
- **[Qt 5](https://www.qt.io/)** by The Qt Company — available under LGPLv3, GPL, or a
  commercial licence.

Because ArchiDev2 links QTermWidget, **any distributed build must comply with GPL terms**. That
is compatible with ArchiDev2 being a GPL project, but it does rule out a future proprietary
release without replacing the terminal widget. No third-party source is vendored here; both
libraries are used as installed system packages.
