-- archidev-flow.lua - project-owned WezTerm configuration.
--
-- This file is NEVER installed over the user's own ~/.wezterm.lua. The
-- launcher runs:
--
--     wezterm --config-file <repo>/wezterm/archidev-flow.lua start
--
-- which makes WezTerm ignore the user's config entirely for this process, so
-- the workspace behaves the same regardless of what the user has configured.
--
-- Everything the layout needs arrives through environment variables set by
-- bin/archidev-flow (panes inherit the GUI process environment).

local wezterm = require 'wezterm'
local mux = wezterm.mux
local act = wezterm.action

-- config_builder gives better error messages; fall back for old versions.
local config = wezterm.config_builder and wezterm.config_builder() or {}

-- ---------------------------------------------------------------------------
-- Helpers
-- ---------------------------------------------------------------------------

local function env(name, fallback)
  local v = os.getenv(name)
  if v == nil or v == '' then
    return fallback
  end
  return v
end

-- The launcher splits each configured command into argv words and joins them
-- with newlines. Splitting therefore happens exactly once, in Bash, and Lua
-- never has to guess about quoting.
local function split_argv(s)
  local out = {}
  if not s or s == '' then
    return out
  end
  for word in string.gmatch(s, '[^\n]+') do
    if word ~= '' then
      out[#out + 1] = word
    end
  end
  return out
end

local ARCHIDEV_HOME = env('ARCHIDEV_HOME', wezterm.config_dir .. '/..')
local PANE_RUNNER = env('ARCHIDEV_PANE_RUNNER', ARCHIDEV_HOME .. '/lib/archidev-pane')
local PROJECT_DIR = env('ARCHIDEV_PROJECT_DIR', wezterm.home_dir)
local CONSULTANT_DIR = env('ARCHIDEV_CONSULTANT_DIR', PROJECT_DIR)
local WORKSPACE = env('ARCHIDEV_WORKSPACE', 'archidev')
local WANT_FILES_PANE = env('ARCHIDEV_WANT_FILES_PANE', '1') == '1'

local CONSULTANT_ARGV = split_argv(os.getenv 'ARCHIDEV_CONSULTANT_ARGV')
local DEVELOPER_ARGV = split_argv(os.getenv 'ARCHIDEV_DEVELOPER_ARGV')
local FILES_ARGV = split_argv(os.getenv 'ARCHIDEV_FILES_ARGV')

-- Every pane runs `bash lib/archidev-pane <role> [argv...]`. Passing an argv
-- array (never a command string) means paths with spaces or shell
-- metacharacters are impossible to misinterpret: no shell is involved.
local function pane_args(role, argv)
  local args = { '/usr/bin/env', 'bash', PANE_RUNNER, role }
  for _, a in ipairs(argv) do
    args[#args + 1] = a
  end
  return args
end

-- ---------------------------------------------------------------------------
-- Pane identity and routing
-- ---------------------------------------------------------------------------
--
-- Routing must never paste into the wrong pane. Two mechanisms, in order:
--
--   1. The ARCHIDEV_ROLE user var, emitted by lib/archidev-pane at startup
--      (OSC 1337 SetUserVar). Survives config reloads, pane resizes, the AI
--      process exiting, and tells us the truth about what a pane really is.
--   2. A registry of pane ids recorded by the gui-startup handler, used only
--      if a pane has not reported its user var yet.
--
-- Both lookups are scoped to the *current mux window*, so a second
-- archidev-flow launch, an extra tab, or an unrelated pane can never receive
-- text meant for this window.

local registry = {} -- [mux_window_id] = { consultant = id, developer = id, files = id }

local function remember(window_id, role, pane)
  registry[window_id] = registry[window_id] or {}
  registry[window_id][role] = pane:pane_id()
end

local function find_pane(gui_window, role)
  local mux_window = gui_window:mux_window()
  if not mux_window then
    return nil
  end

  local by_id = nil
  local wanted_id = (registry[mux_window:window_id()] or {})[role]

  for _, tab in ipairs(mux_window:tabs()) do
    for _, p in ipairs(tab:panes()) do
      local ok, vars = pcall(function()
        return p:get_user_vars()
      end)
      if ok and vars and vars.ARCHIDEV_ROLE == role then
        return p
      end
      if wanted_id and p:pane_id() == wanted_id then
        by_id = p
      end
    end
  end

  return by_id
end

-- Timestamp of the last notification, so the periodic status repaint does not
-- wipe a message the moment it appears.
local last_notice_at = 0
local NOTICE_SECONDS = 5

local function notify(gui_window, message)
  wezterm.log_info('archidev: ' .. message)
  last_notice_at = os.time()
  gui_window:set_right_status(wezterm.format {
    { Foreground = { AnsiColor = 'Yellow' } },
    { Text = ' archidev: ' .. message .. ' ' },
  })
  -- Toasts need a notification daemon; the status bar above is the reliable
  -- channel, so a failure here is ignored on purpose.
  pcall(function()
    gui_window:toast_notification('ArchiDev-Flow', message, nil, 3000)
  end)
end

-- Trailing newlines are stripped before any transfer. A selection that ends
-- with a line break would otherwise act as Enter in a REPL that does not
-- implement bracketed paste. Interior newlines are preserved.
local function strip_trailing_newlines(text)
  return (string.gsub(text, '[\r\n]+$', ''))
end

-- Insert text into a pane with paste semantics and no execution keystroke.
-- pane:send_paste uses bracketed paste when the receiving application has
-- enabled it; otherwise WezTerm sends the bytes plainly. If the running
-- WezTerm is too old to expose send_paste we fall back to send_text, which
-- still never appends Enter, and say so in the status bar.
local function insert_text(gui_window, pane, text)
  -- Probing a method on WezTerm userdata is done inside pcall: depending on
  -- the build, an unknown key may return nil or raise, and neither should
  -- take the keybinding down.
  local has_paste = pcall(function()
    return type(pane.send_paste) == 'function' and pane.send_paste or error('no')
  end)
  if has_paste then
    local ok = pcall(function()
      pane:send_paste(text)
    end)
    if ok then
      return true, 'paste'
    end
  end
  local ok = pcall(function()
    pane:send_text(text)
  end)
  if ok then
    return true, 'raw'
  end
  notify(gui_window, 'this WezTerm build cannot send text to another pane')
  return false, nil
end

-- Selection routing: read the selection from the *source* pane, send it to
-- the destination, clear the source selection, focus the destination.
local function send_selection(gui_window, source_pane, role)
  local text = gui_window:get_selection_text_for_pane(source_pane)
  if not text or text == '' then
    notify(gui_window, 'nothing selected (use the mouse, or Alt+Shift+' ..
      (role == 'consultant' and 'C' or 'D') .. ' to send the clipboard)')
    return
  end
  text = strip_trailing_newlines(text)
  if text == '' then
    notify(gui_window, 'selection was only whitespace')
    return
  end

  local dest = find_pane(gui_window, role)
  if not dest then
    notify(gui_window, 'no ' .. role .. ' pane in this window')
    return
  end
  if dest:pane_id() == source_pane:pane_id() then
    notify(gui_window, 'source and ' .. role .. ' pane are the same pane')
    return
  end

  local ok, mode = insert_text(gui_window, dest, text)
  if not ok then
    return
  end

  -- Clear the source selection only after a successful transfer.
  pcall(function()
    gui_window:perform_action(act.ClearSelection, source_pane)
  end)
  pcall(function()
    dest:activate()
  end)

  notify(gui_window, string.format('sent %d bytes to %s (%s, not submitted)',
    #text, role, mode))
end

-- Clipboard routing.
--
-- WezTerm's Lua API can write the clipboard but cannot read it, so we cannot
-- inspect or trim clipboard text the way we do a selection. Instead we focus
-- the destination pane and let WezTerm perform its own PasteFrom action
-- there, which is the same code path as Ctrl+Shift+V and therefore uses
-- bracketed paste whenever the receiving application enables it.
--
-- Safety check: we verify the destination really is focused before pasting.
-- If activation did not take effect we abort rather than paste into whatever
-- pane happens to be active.
local function send_clipboard(gui_window, source_pane, role)
  local dest = find_pane(gui_window, role)
  if not dest then
    notify(gui_window, 'no ' .. role .. ' pane in this window')
    return
  end

  local activated = pcall(function()
    dest:activate()
  end)
  if not activated then
    notify(gui_window, 'could not focus the ' .. role .. ' pane; nothing pasted')
    return
  end

  local active = gui_window:active_pane()
  if not active or active:pane_id() ~= dest:pane_id() then
    notify(gui_window, 'destination is not focused; nothing pasted (safety check)')
    return
  end

  gui_window:perform_action(act.PasteFrom 'Clipboard', active)
  pcall(function()
    gui_window:perform_action(act.ClearSelection, source_pane)
  end)
  notify(gui_window, 'pasted clipboard into ' .. role .. ' (not submitted)')
end

local function focus_role(gui_window, _pane, role)
  local dest = find_pane(gui_window, role)
  if not dest then
    notify(gui_window, 'no ' .. role .. ' pane in this window')
    return
  end
  dest:activate()
  notify(gui_window, 'focus ' .. role)
end

-- ---------------------------------------------------------------------------
-- Events
-- ---------------------------------------------------------------------------

wezterm.on('archidev-send-selection-consultant', function(w, p) send_selection(w, p, 'consultant') end)
wezterm.on('archidev-send-selection-developer', function(w, p) send_selection(w, p, 'developer') end)
wezterm.on('archidev-send-clipboard-consultant', function(w, p) send_clipboard(w, p, 'consultant') end)
wezterm.on('archidev-send-clipboard-developer', function(w, p) send_clipboard(w, p, 'developer') end)
wezterm.on('archidev-focus-consultant', function(w, p) focus_role(w, p, 'consultant') end)
wezterm.on('archidev-focus-developer', function(w, p) focus_role(w, p, 'developer') end)
wezterm.on('archidev-focus-files', function(w, p) focus_role(w, p, 'files') end)

wezterm.on('archidev-help', function(gui_window, _pane)
  notify(gui_window,
    'Alt+c/d/e focus | Ctrl+Alt+c/d send selection | Alt+Shift+C/D send clipboard | Alt+z zoom')
end)

-- The layout is built here rather than after startup: gui-startup is the
-- deterministic hook WezTerm provides for exactly this, so there is no
-- sleeping, polling or racing against shell startup. If this handler spawns a
-- window, WezTerm does not spawn its own default one.
wezterm.on('gui-startup', function(_cmd)
  local tab, consultant_pane, window = mux.spawn_window {
    workspace = WORKSPACE,
    cwd = CONSULTANT_DIR,
    args = pane_args('consultant', CONSULTANT_ARGV),
  }
  local window_id = window:window_id()
  remember(window_id, 'consultant', consultant_pane)

  -- Developer on the right, half the width.
  local ok, developer_pane = pcall(function()
    return consultant_pane:split {
      direction = 'Right',
      size = 0.5,
      cwd = PROJECT_DIR,
      args = pane_args('developer', DEVELOPER_ARGV),
    }
  end)
  if ok and developer_pane then
    remember(window_id, 'developer', developer_pane)
  else
    wezterm.log_error('archidev: could not create developer pane: ' .. tostring(developer_pane))
  end

  -- Files across the bottom. top_level splits the whole tab rather than just
  -- the pane we call it on, which is what gives the full-width bottom row.
  if WANT_FILES_PANE then
    local base = (ok and developer_pane) or consultant_pane
    local fok, files_pane = pcall(function()
      return base:split {
        direction = 'Bottom',
        size = 0.3,
        top_level = true,
        cwd = PROJECT_DIR,
        args = pane_args('files', FILES_ARGV),
      }
    end)
    if fok and files_pane then
      remember(window_id, 'files', files_pane)
    else
      wezterm.log_error('archidev: could not create files pane: ' .. tostring(files_pane))
    end
  end

  tab:set_title('ArchiDev: ' .. (string.match(PROJECT_DIR, '[^/]+$') or PROJECT_DIR))
  consultant_pane:activate()
  window:gui_window():maximize()
end)

wezterm.on('format-window-title', function()
  return 'ArchiDev-Flow - ' .. PROJECT_DIR
end)

wezterm.on('update-right-status', function(gui_window, _pane)
  -- Leave a recent notification on screen instead of repainting over it.
  if os.time() - last_notice_at < NOTICE_SECONDS then
    return
  end
  gui_window:set_right_status(wezterm.format {
    { Foreground = { AnsiColor = 'Silver' } },
    { Text = ' Alt+c/d/e focus  C-A-c/d selection→  A-S-c/d clipboard→  Alt+z zoom  Alt+h help ' },
  })
end)

-- ---------------------------------------------------------------------------
-- Appearance and keys
-- ---------------------------------------------------------------------------

config.window_close_confirmation = 'AlwaysPrompt'
config.status_update_interval = 2000
config.pane_focus_follows_mouse = false
config.scrollback_lines = 10000
config.window_padding = { left = 4, right = 4, top = 4, bottom = 4 }
config.inactive_pane_hsb = { saturation = 0.85, brightness = 0.75 }
config.use_fancy_tab_bar = true
config.hide_tab_bar_if_only_one_tab = true
-- WezTerm's default mouse bindings already complete a selection into both the
-- clipboard and the primary selection, so the clipboard fallback needs no
-- extra step after a mouse drag.
config.selection_word_boundary = ' \t\n{}[]()"\'`,;:@'

local function emit(name)
  return act.EmitEvent(name)
end

config.keys = {
  -- Routing: selection
  { key = 'c', mods = 'CTRL|ALT', action = emit 'archidev-send-selection-consultant' },
  { key = 'd', mods = 'CTRL|ALT', action = emit 'archidev-send-selection-developer' },
  -- Routing: clipboard (the dependable fallback)
  { key = 'C', mods = 'ALT|SHIFT', action = emit 'archidev-send-clipboard-consultant' },
  { key = 'D', mods = 'ALT|SHIFT', action = emit 'archidev-send-clipboard-developer' },
  -- Focus
  { key = 'c', mods = 'ALT', action = emit 'archidev-focus-consultant' },
  { key = 'd', mods = 'ALT', action = emit 'archidev-focus-developer' },
  { key = 'e', mods = 'ALT', action = emit 'archidev-focus-files' },
  { key = 'h', mods = 'ALT', action = emit 'archidev-help' },
  -- Panes
  { key = 'z', mods = 'ALT', action = act.TogglePaneZoomState },
  { key = 'LeftArrow', mods = 'ALT', action = act.ActivatePaneDirection 'Left' },
  { key = 'RightArrow', mods = 'ALT', action = act.ActivatePaneDirection 'Right' },
  { key = 'UpArrow', mods = 'ALT', action = act.ActivatePaneDirection 'Up' },
  { key = 'DownArrow', mods = 'ALT', action = act.ActivatePaneDirection 'Down' },
  -- Copy/paste stay on the WezTerm defaults (Ctrl+Shift+C / Ctrl+Shift+V).
}

return config
