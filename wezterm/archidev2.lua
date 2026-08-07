-- archidev2.lua - project-owned WezTerm configuration for ArchiDev2.
--
-- This file is NEVER installed over the user's own ~/.wezterm.lua. The
-- launcher runs:
--
--     wezterm --config-file <repo>/wezterm/archidev2.lua start
--
-- which makes WezTerm ignore the user's config entirely for this process, so
-- the workspace behaves the same regardless of what the user has configured.
--
-- Everything the layout needs arrives through ARCHIDEV2_* environment
-- variables set by bin/archidev2 (panes inherit the GUI process environment).

local wezterm = require 'wezterm'
local mux = wezterm.mux
local act = wezterm.action

-- config_builder gives better error messages; fall back for old versions.
local config = wezterm.config_builder and wezterm.config_builder() or {}

-- ---------------------------------------------------------------------------
-- Environment
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

local ARCHIDEV2_HOME = env('ARCHIDEV2_HOME', wezterm.config_dir .. '/..')
local PANE_RUNNER = env('ARCHIDEV2_PANE_RUNNER', ARCHIDEV2_HOME .. '/lib/ad2-pane')
local PROJECT_DIR = env('ARCHIDEV2_PROJECT_DIR', wezterm.home_dir)
local CONSULTANT_DIR = env('ARCHIDEV2_CONSULTANT_DIR', PROJECT_DIR)
local WORKSPACE = env('ARCHIDEV2_WORKSPACE', 'archidev2')
local WANT_FILES_PANE = env('ARCHIDEV2_WANT_FILES_PANE', '1') == '1'
local MOUSE_MENU_BUTTON = env('ARCHIDEV2_MOUSE_MENU_BUTTON', 'Right')
local INACTIVE_BRIGHTNESS = tonumber(env('ARCHIDEV2_INACTIVE_BRIGHTNESS', '0.60')) or 0.60
local STARTUP_HINT = env('ARCHIDEV2_STARTUP_HINT', 'on') == 'on'

local CONSULTANT_ARGV = split_argv(os.getenv 'ARCHIDEV2_CONSULTANT_ARGV')
local DEVELOPER_ARGV = split_argv(os.getenv 'ARCHIDEV2_DEVELOPER_ARGV')
local FILES_ARGV = split_argv(os.getenv 'ARCHIDEV2_FILES_ARGV')

-- Key bindings arrive as "NAME<TAB>MODS<TAB>key" lines. bin/archidev2 has
-- already validated every field against a strict grammar, so nothing here can
-- become arbitrary Lua.
local function parse_keys(s)
  local out = {}
  if not s or s == '' then
    return out
  end
  for line in string.gmatch(s, '[^\n]+') do
    local name, mods, key = string.match(line, '^([^\t]+)\t([^\t]*)\t(.+)$')
    if name and key then
      out[name] = { mods = mods, key = key }
    end
  end
  return out
end

local KEYS = parse_keys(os.getenv 'ARCHIDEV2_KEYS')

local KEY_DEFAULTS = {
  KEY_SEND_SELECTION_CONSULTANT = { mods = 'ALT|SHIFT', key = 'c' },
  KEY_SEND_SELECTION_DEVELOPER = { mods = 'ALT|SHIFT', key = 'd' },
  KEY_SEND_CLIPBOARD_CONSULTANT = { mods = 'CTRL|ALT|SHIFT', key = 'c' },
  KEY_SEND_CLIPBOARD_DEVELOPER = { mods = 'CTRL|ALT|SHIFT', key = 'd' },
  KEY_FOCUS_CONSULTANT = { mods = 'ALT', key = 'c' },
  KEY_FOCUS_DEVELOPER = { mods = 'ALT', key = 'd' },
  KEY_FOCUS_FILES = { mods = 'ALT', key = 'f' },
  KEY_ACTION_MENU = { mods = 'ALT', key = 'Enter' },
  KEY_HELP = { mods = 'ALT', key = 'h' },
  KEY_ZOOM = { mods = 'ALT', key = 'z' },
}

local function binding(name)
  return KEYS[name] or KEY_DEFAULTS[name]
end

-- 'ALT|SHIFT' + 'c'  ->  'Alt+Shift+C' for human-readable help text.
local function key_label(name)
  local b = binding(name)
  if not b then
    return '(unbound)'
  end
  local out = ''
  if b.mods and b.mods ~= 'NONE' then
    for m in string.gmatch(b.mods, '[^|]+') do
      if m == 'CTRL' then out = out .. 'Ctrl+'
      elseif m == 'ALT' then out = out .. 'Alt+'
      elseif m == 'SHIFT' then out = out .. 'Shift+'
      elseif m == 'SUPER' then out = out .. 'Super+'
      end
    end
  end
  local k = b.key
  if #k == 1 then
    k = string.upper(k)
  end
  return out .. k
end

-- Every pane runs `bash lib/ad2-pane <role> [argv...]`. Passing an argv array
-- (never a command string) means paths with spaces or shell metacharacters
-- are impossible to misinterpret: no shell is involved.
local function pane_args(role, argv)
  local args = { '/usr/bin/env', 'bash', PANE_RUNNER, role }
  for _, a in ipairs(argv) do
    args[#args + 1] = a
  end
  return args
end

-- ---------------------------------------------------------------------------
-- Roles
-- ---------------------------------------------------------------------------

local ROLES = { 'consultant', 'developer', 'files' }
local ROLE_LABEL = { consultant = 'CONSULTANT', developer = 'DEVELOPER', files = 'FILES' }
local ROLE_COLOR = { consultant = '#7aa2f7', developer = '#9ece6a', files = '#e0af68' }

-- ---------------------------------------------------------------------------
-- Pane identity and routing
-- ---------------------------------------------------------------------------
--
-- Routing must never paste into the wrong pane. Two mechanisms, in order:
--
--   1. The ARCHIDEV2_ROLE user var, emitted by lib/ad2-pane at startup
--      (OSC 1337 SetUserVar). Survives config reloads, pane resizes, the AI
--      process exiting, and tells us the truth about what a pane really is.
--   2. A registry of pane ids recorded by the gui-startup handler, used only
--      if a pane has not reported its user var yet.
--
-- Both lookups are scoped to the *current mux window*, so a second archidev2
-- launch, an extra tab, or an unrelated pane can never receive text meant for
-- this window.

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
      if ok and vars and vars.ARCHIDEV2_ROLE == role then
        return p
      end
      if wanted_id and p:pane_id() == wanted_id then
        by_id = p
      end
    end
  end

  return by_id
end

-- ---------------------------------------------------------------------------
-- Status bar: role chips (left) and legend / confirmations (right)
-- ---------------------------------------------------------------------------
--
-- WezTerm has no per-pane title bar (upstream issue #1970), so the role
-- labels live in the tab bar, which is pinned to the bottom of the window and
-- always visible. The active role is drawn as a filled chip; the others are
-- dim outlines. This is the closest supported equivalent to labelling each
-- pane, and unlike printing into the panes it cannot contaminate an AI
-- session or its scrollback.

local notices = {} -- [window key] = { text = ..., at = os.time(), seconds = n }

local function win_key(gui_window)
  local ok, id = pcall(function()
    return gui_window:mux_window():window_id()
  end)
  if ok and id then
    return 'mux:' .. tostring(id)
  end
  return 'gui:' .. tostring(gui_window:window_id())
end

local function set_notice(gui_window, text, seconds)
  notices[win_key(gui_window)] = { text = text, at = os.time(), seconds = seconds or 6 }
end

local function notify(gui_window, message)
  wezterm.log_info('archidev2: ' .. message)
  set_notice(gui_window, message, 6)
  -- Repaint immediately rather than waiting for the next status tick.
  pcall(function()
    gui_window:set_right_status(wezterm.format {
      { Foreground = { Color = '#1a1b26' } },
      { Background = { Color = '#e0af68' } },
      { Attribute = { Intensity = 'Bold' } },
      { Text = ' ' .. message .. ' ' },
    })
  end)
end

local function active_notice(gui_window)
  local n = notices[win_key(gui_window)]
  if not n then
    return nil
  end
  if os.time() - n.at >= n.seconds then
    notices[win_key(gui_window)] = nil
    return nil
  end
  return n.text
end

-- Role chips, built from PaneInformation (which carries user_vars and
-- is_active) for the panes of the active tab.
local function role_chips(gui_window)
  local present, active_role = {}, nil
  local ok = pcall(function()
    for _, p in ipairs(gui_window:active_tab():panes_with_info()) do
      local role = p.user_vars and p.user_vars.ARCHIDEV2_ROLE
      if role then
        present[role] = true
        if p.is_active then
          active_role = role
        end
      end
    end
  end)
  if not ok then
    return {}
  end

  local elems = {}
  for _, role in ipairs(ROLES) do
    if present[role] then
      if role == active_role then
        elems[#elems + 1] = { Background = { Color = ROLE_COLOR[role] } }
        elems[#elems + 1] = { Foreground = { Color = '#1a1b26' } }
        elems[#elems + 1] = { Attribute = { Intensity = 'Bold' } }
        elems[#elems + 1] = { Text = ' \u{25b6} ' .. ROLE_LABEL[role] .. ' ' }
      else
        elems[#elems + 1] = { Background = { Color = '#1a1b26' } }
        elems[#elems + 1] = { Foreground = { Color = '#565f89' } }
        elems[#elems + 1] = { Attribute = { Intensity = 'Normal' } }
        elems[#elems + 1] = { Text = '   ' .. ROLE_LABEL[role] .. ' ' }
      end
      elems[#elems + 1] = { Background = { Color = '#1a1b26' } }
      elems[#elems + 1] = { Foreground = { Color = '#3b4261' } }
      elems[#elems + 1] = { Text = '|' }
    end
  end
  return elems
end

-- Deliberately worded, not compressed notation, and short enough to sit on
-- one row next to the chips.
local function legend_text()
  return string.format(
    'Send selection: %s Consultant / %s Developer   Focus: %s / %s / %s   %s Help',
    key_label 'KEY_SEND_SELECTION_CONSULTANT',
    key_label 'KEY_SEND_SELECTION_DEVELOPER',
    key_label 'KEY_FOCUS_CONSULTANT',
    key_label 'KEY_FOCUS_DEVELOPER',
    key_label 'KEY_FOCUS_FILES',
    key_label 'KEY_HELP'
  )
end

-- Wrapped in pcall end to end: the status bar is decoration, and no problem
-- drawing it should ever be able to disturb the panes.
local update_status
wezterm.on('update-status', function(gui_window, _pane)
  local ok, err = pcall(function()
    update_status(gui_window)
  end)
  if not ok then
    wezterm.log_error('archidev2: status bar: ' .. tostring(err))
  end
end)

update_status = function(gui_window)
  gui_window:set_left_status(wezterm.format(role_chips(gui_window)))

  local notice = active_notice(gui_window)
  if notice then
    -- A confirmation is showing: leave it alone. The periodic repaint must
    -- not wipe the very message the user just triggered.
    gui_window:set_right_status(wezterm.format {
      { Foreground = { Color = '#1a1b26' } },
      { Background = { Color = '#e0af68' } },
      { Attribute = { Intensity = 'Bold' } },
      { Text = ' ' .. notice .. ' ' },
    })
    return
  end

  gui_window:set_right_status(wezterm.format {
    { Background = { Color = '#1a1b26' } },
    { Foreground = { Color = '#787c99' } },
    { Attribute = { Intensity = 'Normal' } },
    { Text = ' ' .. legend_text() .. ' ' },
  })
end

-- ---------------------------------------------------------------------------
-- Text transfer
-- ---------------------------------------------------------------------------

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

-- Route already-captured text to a role. Used by both the keyboard shortcuts
-- and the mouse action menu, so they cannot drift apart.
local function route_text(gui_window, source_pane, role, text)
  text = strip_trailing_newlines(text or '')
  if text == '' then
    notify(gui_window, 'Nothing to send - the selection was empty')
    return false
  end

  local dest = find_pane(gui_window, role)
  if not dest then
    notify(gui_window, 'No ' .. ROLE_LABEL[role] .. ' pane in this window')
    return false
  end
  if source_pane and dest:pane_id() == source_pane:pane_id() then
    notify(gui_window, 'Source and ' .. ROLE_LABEL[role] .. ' are the same pane')
    return false
  end

  local ok, mode = insert_text(gui_window, dest, text)
  if not ok then
    return false
  end

  -- Clear the source selection only after a successful transfer.
  if source_pane then
    pcall(function()
      gui_window:perform_action(act.ClearSelection, source_pane)
    end)
  end
  pcall(function()
    dest:activate()
  end)

  local suffix = (mode == 'raw') and ' (plain send: no bracketed paste)' or ''
  notify(gui_window, 'Sent selection to ' .. ROLE_LABEL[role] .. ' \u{2014} focus moved' .. suffix)
  return true
end

local function send_selection(gui_window, source_pane, role)
  local text = gui_window:get_selection_text_for_pane(source_pane)
  if not text or strip_trailing_newlines(text) == '' then
    notify(gui_window, 'Nothing selected - drag with the mouse in a terminal pane, or copy and use a Send clipboard action')
    return
  end
  route_text(gui_window, source_pane, role, text)
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
    notify(gui_window, 'No ' .. ROLE_LABEL[role] .. ' pane in this window')
    return
  end

  local activated = pcall(function()
    dest:activate()
  end)
  if not activated then
    notify(gui_window, 'Could not focus ' .. ROLE_LABEL[role] .. ' - nothing pasted')
    return
  end

  local active = gui_window:active_pane()
  if not active or active:pane_id() ~= dest:pane_id() then
    notify(gui_window, 'Destination is not focused - nothing pasted (safety check)')
    return
  end

  gui_window:perform_action(act.PasteFrom 'Clipboard', active)
  if source_pane then
    pcall(function()
      gui_window:perform_action(act.ClearSelection, source_pane)
    end)
  end
  notify(gui_window, 'Sent clipboard to ' .. ROLE_LABEL[role] .. ' \u{2014} focus moved')
end

local function focus_role(gui_window, _pane, role)
  local dest = find_pane(gui_window, role)
  if not dest then
    notify(gui_window, 'No ' .. ROLE_LABEL[role] .. ' pane in this window')
    return
  end
  dest:activate()
  notify(gui_window, 'Focus: ' .. ROLE_LABEL[role])
end

-- ---------------------------------------------------------------------------
-- Action menu and help overlay (InputSelector)
-- ---------------------------------------------------------------------------
--
-- InputSelector is WezTerm's built-in modal chooser: rows can be picked with
-- the arrow keys, with the shortcut character shown against each row, or by
-- clicking with the mouse. It is the only supported way to offer clickable
-- controls - WezTerm has no toolbar or clickable-widget API - and it needs no
-- extra process, daemon or screen scraping.
--
-- The selected text is captured *before* the overlay opens and stashed here,
-- because opening the overlay changes the active pane and may drop the
-- selection. One pre-registered callback is reused for every invocation:
-- calling wezterm.action_callback() on each right-click would register a new
-- event every time and leak them over a long session.

local pending = {} -- [window key] = { text = <selection>, pane_id = n }

local function stash_selection(gui_window, pane, text)
  pending[win_key(gui_window)] = { text = text, pane_id = pane:pane_id() }
end

local function take_selection(gui_window)
  local key = win_key(gui_window)
  local v = pending[key]
  pending[key] = nil
  return v
end

local menu_callback = wezterm.action_callback(function(gui_window, pane, id, _label)
  local stashed = take_selection(gui_window)
  local text = stashed and stashed.text or ''

  if not id or id == 'cancel' or id == 'noop' then
    return -- Escape, or an informational row: do nothing at all.
  elseif id == 'sel-consultant' then
    route_text(gui_window, pane, 'consultant', text)
  elseif id == 'sel-developer' then
    route_text(gui_window, pane, 'developer', text)
  elseif id == 'copy' then
    -- Ordinary WezTerm copy, from the text we captured.
    gui_window:copy_to_clipboard(text, 'ClipboardAndPrimarySelection')
    pcall(function()
      gui_window:perform_action(act.ClearSelection, pane)
    end)
    notify(gui_window, 'Copied selection to the clipboard')
  elseif id == 'clip-consultant' then
    send_clipboard(gui_window, pane, 'consultant')
  elseif id == 'clip-developer' then
    send_clipboard(gui_window, pane, 'developer')
  elseif id == 'paste-here' then
    gui_window:perform_action(act.PasteFrom 'Clipboard', pane)
  elseif id == 'help' then
    gui_window:perform_action(act.EmitEvent 'archidev2-help', pane)
  end
end)

local function open_action_menu(gui_window, pane)
  local text = gui_window:get_selection_text_for_pane(pane) or ''
  text = strip_trailing_newlines(text)
  stash_selection(gui_window, pane, text)

  local choices = {}
  local function add(id, label)
    choices[#choices + 1] = { id = id, label = label }
  end

  local description
  if text ~= '' then
    local preview = string.gsub(string.sub(text, 1, 48), '[\r\n]+', ' ')
    if #text > 48 then
      preview = preview .. '...'
    end
    description = string.format('%d bytes selected: "%s"   -- pick a row (click it, or press its number)',
      #text, preview)
    add('sel-consultant', 'Send selection to CONSULTANT   (never submitted)')
    add('sel-developer', 'Send selection to DEVELOPER    (never submitted)')
    add('copy', 'Copy selection to the clipboard (normal WezTerm copy)')
  else
    description = 'Nothing WezTerm can read is selected. Drag with the mouse in a terminal pane, or copy the text (Ctrl+C in Micro) and use a Send clipboard action below.'
    add('noop', 'No text selected - in Micro, copy with Ctrl+C and use Send clipboard below')
  end

  add('clip-consultant', 'Send clipboard to CONSULTANT')
  add('clip-developer', 'Send clipboard to DEVELOPER')
  add('paste-here', 'Paste clipboard into this pane')
  add('help', 'Show all ArchiDev2 shortcuts')
  add('cancel', 'Cancel')

  gui_window:perform_action(
    act.InputSelector {
      title = 'ArchiDev2 actions',
      description = description,
      choices = choices,
      alphabet = '123456789',
      action = menu_callback,
    },
    pane
  )
end

local help_callback = wezterm.action_callback(function(gui_window, pane, id, _label)
  if not id or id == 'close' or id == 'noop' then
    return
  elseif id == 'menu' then
    open_action_menu(gui_window, pane)
  elseif id == 'focus-consultant' then
    focus_role(gui_window, pane, 'consultant')
  elseif id == 'focus-developer' then
    focus_role(gui_window, pane, 'developer')
  elseif id == 'focus-files' then
    focus_role(gui_window, pane, 'files')
  end
end)

local function open_help(gui_window, pane)
  local mouse_hint = (MOUSE_MENU_BUTTON == 'off')
      and 'the action menu is disabled (MOUSE_MENU_BUTTON=off)'
      or (MOUSE_MENU_BUTTON .. '-click opens the action menu')

  local choices = {
    { id = 'noop', label = 'SENDING TEXT  --  nothing you send is ever submitted for you' },
    { id = 'noop', label = '   Selection actions work on text dragged in CONSULTANT or DEVELOPER.' },
    { id = 'noop', label = '   From Micro or another app, copy first and use a clipboard action.' },
    { id = 'noop', label = '   ' .. key_label 'KEY_SEND_SELECTION_CONSULTANT' .. '   Send the selected text to CONSULTANT' },
    { id = 'noop', label = '   ' .. key_label 'KEY_SEND_SELECTION_DEVELOPER' .. '   Send the selected text to DEVELOPER' },
    { id = 'noop', label = '   ' .. key_label 'KEY_SEND_CLIPBOARD_CONSULTANT' .. '   Send the clipboard to CONSULTANT' },
    { id = 'noop', label = '   ' .. key_label 'KEY_SEND_CLIPBOARD_DEVELOPER' .. '   Send the clipboard to DEVELOPER' },
    { id = 'menu', label = '   ' .. key_label 'KEY_ACTION_MENU' .. ' or ' .. mouse_hint .. '  ->  open it now' },
    { id = 'noop', label = '' },
    { id = 'noop', label = 'MOVING AROUND' },
    { id = 'focus-consultant', label = '   ' .. key_label 'KEY_FOCUS_CONSULTANT' .. '   Focus CONSULTANT  ->  go there now' },
    { id = 'focus-developer', label = '   ' .. key_label 'KEY_FOCUS_DEVELOPER' .. '   Focus DEVELOPER  ->  go there now' },
    { id = 'focus-files', label = '   ' .. key_label 'KEY_FOCUS_FILES' .. '   Focus FILES  ->  go there now' },
    { id = 'noop', label = '   ' .. key_label 'KEY_ZOOM' .. '   Maximise / restore the active pane' },
    { id = 'noop', label = '   Ctrl+Shift+C / Ctrl+Shift+V   WezTerm copy and paste (unchanged)' },
    { id = 'noop', label = '' },
    { id = 'noop', label = 'THINGS THAT CATCH PEOPLE OUT' },
    { id = 'noop', label = '   Micro keeps its selection to itself: WezTerm cannot read it, so ArchiDev2' },
    { id = 'noop', label = '   cannot send it directly. Copy in Micro with Ctrl+C, then use a Send' },
    { id = 'noop', label = '   clipboard action. Focus still moves to the destination.' },
    { id = 'noop', label = '   Clipboard text ending in a newline may be submitted by a program that' },
    { id = 'noop', label = '   does not support bracketed paste. Selection sends always strip it.' },
    { id = 'noop', label = '   Routing only finds panes in THIS window, never another ArchiDev2 window.' },
    { id = 'noop', label = '' },
    { id = 'close', label = 'Close this help' },
  }

  gui_window:perform_action(
    act.InputSelector {
      title = 'ArchiDev2 help',
      description = 'Arrow keys or the mouse to move, Enter or click to pick, Escape to close.',
      choices = choices,
      alphabet = '',
      action = help_callback,
    },
    pane
  )
end

-- ---------------------------------------------------------------------------
-- Events
-- ---------------------------------------------------------------------------

wezterm.on('archidev2-send-selection-consultant', function(w, p) send_selection(w, p, 'consultant') end)
wezterm.on('archidev2-send-selection-developer', function(w, p) send_selection(w, p, 'developer') end)
wezterm.on('archidev2-send-clipboard-consultant', function(w, p) send_clipboard(w, p, 'consultant') end)
wezterm.on('archidev2-send-clipboard-developer', function(w, p) send_clipboard(w, p, 'developer') end)
wezterm.on('archidev2-focus-consultant', function(w, p) focus_role(w, p, 'consultant') end)
wezterm.on('archidev2-focus-developer', function(w, p) focus_role(w, p, 'developer') end)
wezterm.on('archidev2-focus-files', function(w, p) focus_role(w, p, 'files') end)
wezterm.on('archidev2-action-menu', function(w, p) open_action_menu(w, p) end)
wezterm.on('archidev2-help', function(w, p) open_help(w, p) end)

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
    wezterm.log_error('archidev2: could not create developer pane: ' .. tostring(developer_pane))
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
      wezterm.log_error('archidev2: could not create files pane: ' .. tostring(files_pane))
    end
  end

  tab:set_title('ArchiDev2')
  consultant_pane:activate()

  local gui = window:gui_window()
  if gui then
    gui:maximize()
    if STARTUP_HINT then
      -- Shown in the status bar, not written into a pane: an AI TUI would
      -- wipe it on its first redraw and it would pollute the scrollback.
      set_notice(gui,
        'Select text, then ' .. key_label 'KEY_SEND_SELECTION_CONSULTANT' ..
        ' / ' .. key_label 'KEY_SEND_SELECTION_DEVELOPER' ..
        ' to send it. ' .. key_label 'KEY_HELP' .. ' for help.', 20)
    end
  end
end)

wezterm.on('format-window-title', function()
  return 'ArchiDev2 - ' .. PROJECT_DIR
end)

wezterm.on('format-tab-title', function(tab)
  local title = tab.tab_title
  if title == nil or title == '' then
    title = 'ArchiDev2'
  end
  return ' ' .. title .. ' '
end)

-- ---------------------------------------------------------------------------
-- Appearance
-- ---------------------------------------------------------------------------

config.window_close_confirmation = 'AlwaysPrompt'
config.status_update_interval = 1000
config.pane_focus_follows_mouse = false
config.scrollback_lines = 10000
config.window_padding = { left = 4, right = 4, top = 4, bottom = 0 }

-- Focus made obvious: inactive panes are dimmed but still readable, and the
-- divider between panes is a bright accent rather than a hairline.
config.inactive_pane_hsb = { saturation = 0.75, brightness = INACTIVE_BRIGHTNESS }
config.colors = {
  split = '#7aa2f7',
  tab_bar = {
    background = '#1a1b26',
    active_tab = { bg_color = '#1a1b26', fg_color = '#c0caf5' },
    inactive_tab = { bg_color = '#1a1b26', fg_color = '#565f89' },
    new_tab = { bg_color = '#1a1b26', fg_color = '#565f89' },
  },
}

-- The tab bar is the status bar: it carries the role chips and the legend, so
-- it must always be visible. At the bottom it reads as a status line and stays
-- out of the way of the panes.
config.use_fancy_tab_bar = false
config.hide_tab_bar_if_only_one_tab = false
config.tab_bar_at_bottom = true
config.show_new_tab_button_in_tab_bar = false
config.selection_word_boundary = ' \t\n{}[]()"\'`,;:@'

-- ---------------------------------------------------------------------------
-- Key bindings
-- ---------------------------------------------------------------------------

local keys = {}

local function add_key(mods, key, action)
  keys[#keys + 1] = { key = key, mods = mods, action = action }
  -- For a shifted letter, register both cases. Depending on the keyboard
  -- layout WezTerm may report either 'c' with SHIFT or 'C', and we want the
  -- binding to fire in both situations.
  if #key == 1 and string.match(key, '%a') and string.find(mods, 'SHIFT', 1, true) then
    local other = (key == string.lower(key)) and string.upper(key) or string.lower(key)
    keys[#keys + 1] = { key = other, mods = mods, action = action }
  end
end

local function bind(name, action)
  local b = binding(name)
  if b then
    add_key(b.mods, b.key, action)
  end
end

bind('KEY_SEND_SELECTION_CONSULTANT', act.EmitEvent 'archidev2-send-selection-consultant')
bind('KEY_SEND_SELECTION_DEVELOPER', act.EmitEvent 'archidev2-send-selection-developer')
bind('KEY_SEND_CLIPBOARD_CONSULTANT', act.EmitEvent 'archidev2-send-clipboard-consultant')
bind('KEY_SEND_CLIPBOARD_DEVELOPER', act.EmitEvent 'archidev2-send-clipboard-developer')
bind('KEY_FOCUS_CONSULTANT', act.EmitEvent 'archidev2-focus-consultant')
bind('KEY_FOCUS_DEVELOPER', act.EmitEvent 'archidev2-focus-developer')
bind('KEY_FOCUS_FILES', act.EmitEvent 'archidev2-focus-files')
bind('KEY_ACTION_MENU', act.EmitEvent 'archidev2-action-menu')
bind('KEY_HELP', act.EmitEvent 'archidev2-help')
bind('KEY_ZOOM', act.TogglePaneZoomState)

-- Compatibility aliases from the first prototype. Ctrl+Alt+D in particular is
-- "show desktop" on Cinnamon and may never reach WezTerm, which is why the
-- defaults moved to Alt+Shift; these remain for muscle memory.
add_key('CTRL|ALT', 'c', act.EmitEvent 'archidev2-send-selection-consultant')
add_key('CTRL|ALT', 'd', act.EmitEvent 'archidev2-send-selection-developer')
add_key('ALT', 'e', act.EmitEvent 'archidev2-focus-files')

-- Pane navigation.
add_key('ALT', 'LeftArrow', act.ActivatePaneDirection 'Left')
add_key('ALT', 'RightArrow', act.ActivatePaneDirection 'Right')
add_key('ALT', 'UpArrow', act.ActivatePaneDirection 'Up')
add_key('ALT', 'DownArrow', act.ActivatePaneDirection 'Down')

config.keys = keys

-- ---------------------------------------------------------------------------
-- Mouse bindings
-- ---------------------------------------------------------------------------
--
-- WezTerm has no default right-button assignment, so claiming it costs the
-- user nothing. When an application has enabled mouse reporting (Micro, Yazi)
-- WezTerm delivers the event to the application instead - holding SHIFT
-- bypasses that and matches this binding as though SHIFT were not pressed, so
-- Shift+Right-click opens the menu inside those TUIs. We deliberately do not
-- set mouse_reporting=true, which would steal the button from them entirely.
--
-- The menu opens on Up, not Down. Binding Down opened the overlay while the
-- button was still held, and the release that followed landed on the freshly
-- opened InputSelector and dismissed it immediately - reliably so on a
-- trackpad, where press and release are a single physical tap. Acting on the
-- completed click leaves nothing in flight to close it.
--
-- Because Down no longer carries the menu, it has to be neutralised for the
-- chosen button, or WezTerm's own default for that button would now fire
-- alongside it: middle-click otherwise pastes the primary selection on Down
-- and then opens the menu on Up. Right has no default, so the Nop simply
-- keeps the two buttons behaving identically. Neither entry sets
-- mouse_reporting, so Micro and Yazi still receive their own mouse events.

if MOUSE_MENU_BUTTON ~= 'off' then
  config.mouse_bindings = {
    {
      event = { Down = { streak = 1, button = MOUSE_MENU_BUTTON } },
      mods = 'NONE',
      action = act.Nop,
    },
    {
      event = { Up = { streak = 1, button = MOUSE_MENU_BUTTON } },
      mods = 'NONE',
      action = act.EmitEvent 'archidev2-action-menu',
    },
  }
end

return config
