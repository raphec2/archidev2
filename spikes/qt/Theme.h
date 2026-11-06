// SPDX-License-Identifier: GPL-2.0-or-later
//
// One built-in dark theme. No chooser, no settings, no theme subsystem.
#pragma once

#include <QString>
class QApplication;

namespace Theme {

// Black surfaces, blue outlines, white text - the ArchiDev-Flow character.
inline constexpr const char *Window     = "#0b0d10";
inline constexpr const char *Panel      = "#11151b";
inline constexpr const char *Raised     = "#171d26";
inline constexpr const char *Blue       = "#4c8dff";
inline constexpr const char *BlueActive = "#6aa6ff";
inline constexpr const char *Text       = "#e8edf4";
inline constexpr const char *TextDim    = "#8d99aa";
inline constexpr const char *Selection  = "#245ea8";
inline constexpr const char *Amber      = "#e0a84e";
inline constexpr const char *Red        = "#d96c75";
inline constexpr const char *Divider    = "#26364d";

// Name verified against the installed schemes in
// /usr/share/qtermwidget5/color-schemes (background 0,0,0 / foreground 255,255,255).
inline constexpr const char *TerminalScheme = "WhiteOnBlack";

void apply(QApplication &app);          // dark QPalette + focused stylesheet

// Border styles for the pane frames, so the focused surface is unmistakable.
QString paneBorder(bool active);

} // namespace Theme
