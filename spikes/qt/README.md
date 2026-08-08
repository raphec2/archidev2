# ArchiDev2 — Qt/QTermWidget spike

A live-first experiment, not the product. It exists to answer one question before any real
application is built:

> Can ArchiDev2 read a selection from a terminal or an editor, insert it into another surface
> **without it being submitted**, and move focus — with persistent clickable buttons?

Everything not needed to answer that is deliberately absent: no file explorer, no config file,
no save, no profiles, no packaging, no tests.

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

`ARCHIDEV2_SPIKE_SELFTEST=1` makes the app, 2.5 s after startup, select the editor contents and
route them to the Developer terminal through the real send path. It exists so the decisive
"inserted but not submitted" property can be proven without a human drag. Remove it in the
product.

## What it does

Two `QTermWidget`s (Consultant, Developer), one `QPlainTextEdit`, three persistent buttons and
a status line, in nested `QSplitter`s. The focused surface's header is highlighted.

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

## Still unproven — needs your eyes

- Codex and Claude **rendering** correctly and accepting input inside QTermWidget.
- `selectedText()` returning text after a **normal mouse drag over a live AI TUI**. This is the
  single most important remaining unknown; the WezTerm prototype showed neither CLI captures the
  mouse, which is encouraging but not the same widget.
- Focus visibly moving, and multiline text arriving intact in a real AI prompt.

## Later, not now

The product will select AI commands from generic profiles — no provider adapters, no SDKs, just
programs and arguments:

```json
{ "profiles": [
    { "name": "Codex",      "program": "codex",  "arguments": [] },
    { "name": "Local Qwen", "program": "ollama", "arguments": ["run", "qwen3"] } ] }
```

If routing holds up live, the next iteration adds `QFileSystemModel` + `QTreeView`,
double-click to open, and explicit save with dirty protection.

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
