# ArchiDev2

A lightweight Linux workspace for working with two AI coding CLIs side by side. Keep a Consultant, a Developer, a project shell, files and Notes in one native Qt window.

**Current version:** the native Qt app is on `main` and builds from source on Ubuntu. It has been used with Codex and Claude Code; other interactive CLIs can be added as project-local profiles. Feedback on the workflow and rough edges is welcome.

## The main interaction

Select text in the Consultant, Developer, Shell or Notes pane, then click a destination beside that source. ArchiDev2 inserts the text there and moves focus **without pressing Enter**. Each button reads the selection in its own pane, so selections elsewhere cannot be sent by mistake.

The two AI panes also offer **New Session** and **Resume…** for Codex and Claude conversations. Each project keeps its configuration in its own `.archidev2.ini`.

## Build and run

You need Linux, an installed interactive AI CLI, and the Ubuntu build packages `cmake`, `g++`, `qtbase5-dev` and `libqtermwidget5-1-dev`.

```sh
git clone https://github.com/raphec2/archidev2.git
cd archidev2
sudo apt install cmake g++ qtbase5-dev libqtermwidget5-1-dev
cmake -S spikes/qt -B build/qt-spike
cmake --build build/qt-spike --parallel
./build/qt-spike/archidev2-qt --consultant-dir /path/to/consulting /path/to/project
```

The first launch writes `/path/to/project/.archidev2.ini`. Later, you can launch that project with just its path. The optional [`install-launcher`](spikes/qt/install-launcher) adds a short `ad2` command and a desktop entry with a project folder picker.

See the [native Qt guide](spikes/qt/README.md) for setup details, controls, behaviour, limitations and what has been tested.

## Earlier implementation

The earlier WezTerm implementation remains in the repository for reference. Its [historical README](https://github.com/raphec2/archidev2/blob/56529e0/README.md) documents that version. The native Qt app is the current ArchiDev2.

License: [GPL-2.0-or-later](LICENSE).
