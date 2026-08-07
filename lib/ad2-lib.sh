#!/usr/bin/env bash
# shellcheck shell=bash
#
# ad2-lib.sh - shared helpers for ArchiDev2.
#
# Sourced by bin/archidev2, scripts/check-dependencies and tests/run-tests.
# Nothing in here executes work on its own; it only defines functions and
# defaults so that the same logic can be unit-tested without launching a GUI.
#
# Rules observed throughout this file:
#   * no `eval`, ever (config values are data, never code)
#   * every path is quoted
#   * no hardcoded home directory; XDG with documented fallbacks

# ---------------------------------------------------------------------------
# Defaults
# ---------------------------------------------------------------------------

AD2_DEFAULT_CONSULTANT_COMMAND='codex'
AD2_DEFAULT_DEVELOPER_COMMAND='claude'
AD2_DEFAULT_FILE_MANAGER_COMMAND='yazi'
AD2_DEFAULT_EDITOR_COMMAND='micro'

# Key bindings. Format is "MODS key" (see ad2_valid_keyspec).
#
# Chosen to avoid desktop-environment collisions: Cinnamon/GNOME take
# Ctrl+Alt+<letter> (Ctrl+Alt+D is "show desktop" on Mint), and Ctrl+Shift+C/V
# belong to WezTerm's own copy/paste, which we never override.
AD2_DEFAULT_KEY_SEND_SELECTION_CONSULTANT='ALT|SHIFT c'
AD2_DEFAULT_KEY_SEND_SELECTION_DEVELOPER='ALT|SHIFT d'
AD2_DEFAULT_KEY_SEND_CLIPBOARD_CONSULTANT='CTRL|ALT|SHIFT c'
AD2_DEFAULT_KEY_SEND_CLIPBOARD_DEVELOPER='CTRL|ALT|SHIFT d'
AD2_DEFAULT_KEY_FOCUS_CONSULTANT='ALT c'
AD2_DEFAULT_KEY_FOCUS_DEVELOPER='ALT d'
AD2_DEFAULT_KEY_FOCUS_FILES='ALT f'
AD2_DEFAULT_KEY_ACTION_MENU='ALT Enter'
AD2_DEFAULT_KEY_HELP='ALT h'
AD2_DEFAULT_KEY_ZOOM='ALT z'

AD2_DEFAULT_MOUSE_MENU_BUTTON='Right'
AD2_DEFAULT_INACTIVE_PANE_BRIGHTNESS='0.60'
AD2_DEFAULT_STARTUP_HINT='on'

# Keys accepted in the user config file. Anything else is ignored with a
# warning, so a typo can never silently change behaviour.
AD2_CONFIG_KEYS=(
  CONSULTANT_DIR
  CONSULTANT_COMMAND
  DEVELOPER_COMMAND
  FILE_MANAGER_COMMAND
  EDITOR_COMMAND
  KEY_SEND_SELECTION_CONSULTANT
  KEY_SEND_SELECTION_DEVELOPER
  KEY_SEND_CLIPBOARD_CONSULTANT
  KEY_SEND_CLIPBOARD_DEVELOPER
  KEY_FOCUS_CONSULTANT
  KEY_FOCUS_DEVELOPER
  KEY_FOCUS_FILES
  KEY_ACTION_MENU
  KEY_HELP
  KEY_ZOOM
  MOUSE_MENU_BUTTON
  INACTIVE_PANE_BRIGHTNESS
  STARTUP_HINT
)

# Config keys that hold a key binding, in the order they are shown in help.
AD2_KEY_BINDING_KEYS=(
  KEY_SEND_SELECTION_CONSULTANT
  KEY_SEND_SELECTION_DEVELOPER
  KEY_SEND_CLIPBOARD_CONSULTANT
  KEY_SEND_CLIPBOARD_DEVELOPER
  KEY_FOCUS_CONSULTANT
  KEY_FOCUS_DEVELOPER
  KEY_FOCUS_FILES
  KEY_ACTION_MENU
  KEY_HELP
  KEY_ZOOM
)

# ---------------------------------------------------------------------------
# Output helpers (all diagnostics go to stderr so --dry-run stdout stays clean)
# ---------------------------------------------------------------------------

ad2_msg()  { printf '%s\n' "$*" >&2; }
ad2_warn() { printf 'archidev2: warning: %s\n' "$*" >&2; }
ad2_err()  { printf 'archidev2: error: %s\n' "$*" >&2; }

ad2_die() {
  local code=1
  if [[ ${1:-} =~ ^[0-9]+$ ]]; then code=$1; shift; fi
  ad2_err "$*"
  exit "$code"
}

# ---------------------------------------------------------------------------
# XDG locations
# ---------------------------------------------------------------------------

ad2_config_home() {
  local base=${XDG_CONFIG_HOME:-}
  # XDG says relative values must be ignored.
  if [[ -z $base || $base != /* ]]; then
    base=${HOME:?HOME is not set}/.config
  fi
  printf '%s\n' "$base/archidev2"
}

ad2_config_file() {
  printf '%s\n' "$(ad2_config_home)/config"
}

# The pre-rename prototype stored its config here. NOTE: that directory is
# shared with the old ArchiDev-Flow Electron application, which keeps caches,
# cookies and workspaces alongside it. We only ever read the single `config`
# file inside it, and never write to or delete anything there.
ad2_legacy_config_file() {
  local base=${XDG_CONFIG_HOME:-}
  if [[ -z $base || $base != /* ]]; then
    base=${HOME:?HOME is not set}/.config
  fi
  printf '%s\n' "$base/archidev-flow/config"
}

# ---------------------------------------------------------------------------
# String helpers
# ---------------------------------------------------------------------------

ad2_trim() {
  local s=$1
  s=${s#"${s%%[![:space:]]*}"}   # leading
  s=${s%"${s##*[![:space:]]}"}   # trailing
  printf '%s' "$s"
}

# Strip one layer of matching surrounding quotes. The inside is taken
# literally: no expansion, no backslash escapes, no globbing.
ad2_unquote() {
  local s=$1
  if (( ${#s} >= 2 )); then
    if [[ ${s:0:1} == "'" && ${s: -1} == "'" ]]; then
      printf '%s' "${s:1:${#s}-2}"; return
    fi
    if [[ ${s:0:1} == '"' && ${s: -1} == '"' ]]; then
      printf '%s' "${s:1:${#s}-2}"; return
    fi
  fi
  printf '%s' "$s"
}

# Quote a value for writing back into the config file.
# Preference order: single quotes, double quotes, bare. Values are never
# expanded on read, so double quotes here are inert - they only protect
# leading/trailing whitespace.
ad2_quote_for_config() {
  local v=$1
  if [[ $v != *"'"* ]]; then
    printf "'%s'" "$v"
  elif [[ $v != *'"'* ]]; then
    printf '"%s"' "$v"
  else
    # Contains both quote characters: write it bare. Only leading/trailing
    # whitespace would be lost on re-read, which we strip here anyway.
    printf '%s' "$(ad2_trim "$v")"
  fi
}

# ---------------------------------------------------------------------------
# Command format
# ---------------------------------------------------------------------------
# A configured command is a whitespace-separated argv list:
#     CONSULTANT_COMMAND='codex --model o3'   ->  argv = (codex --model o3)
# There is no quoting, escaping, globbing or variable expansion *inside* a
# command value. The first word must be an executable found on PATH (or an
# absolute path). This is deliberately dumb: it cannot be turned into a shell
# injection, and it is the same rule in Bash and in the Lua config.
ad2_split_command() {
  local -n _out=$1
  local raw=$2
  # `read -a` word-splits on IFS without globbing or expansion.
  read -r -a _out <<<"$raw"
}

# ---------------------------------------------------------------------------
# Key binding format
# ---------------------------------------------------------------------------
# "MODS key", e.g.  'ALT|SHIFT c'  'CTRL|ALT|SHIFT d'  'NONE F5'  'ALT Enter'
#
# MODS is one or more of CTRL ALT SHIFT SUPER joined by '|', or NONE.
# key is a single alphanumeric character or a WezTerm key name such as Enter,
# Tab, Space, F1..F24, LeftArrow, RightArrow, UpArrow, DownArrow, PageUp,
# PageDown, Home, End, Insert, Delete, Escape, Backspace.
#
# Anything outside this grammar is rejected, so a config value can never be
# injected into the generated Lua as arbitrary code.
AD2_VALID_KEY_NAMES='Enter|Tab|Space|Escape|Backspace|Delete|Insert|Home|End|PageUp|PageDown|LeftArrow|RightArrow|UpArrow|DownArrow|F1|F2|F3|F4|F5|F6|F7|F8|F9|F10|F11|F12|F13|F14|F15|F16|F17|F18|F19|F20|F21|F22|F23|F24'

ad2_valid_keyspec() {
  local spec=$1 mods key m
  [[ $spec == *' '* ]] || return 1
  mods=${spec%% *}
  key=${spec#* }
  key=$(ad2_trim "$key")
  [[ -n $mods && -n $key ]] || return 1

  if [[ $mods != 'NONE' ]]; then
    local IFS='|'
    # shellcheck disable=SC2206  # deliberate split on '|'
    local parts=($mods)
    ((${#parts[@]})) || return 1
    for m in "${parts[@]}"; do
      case $m in
        CTRL|ALT|SHIFT|SUPER) ;;
        *) return 1 ;;
      esac
    done
  fi

  # A single alphanumeric character, or one of the named keys.
  [[ $key =~ ^[A-Za-z0-9]$ ]] && return 0
  [[ $key =~ ^($AD2_VALID_KEY_NAMES)$ ]] && return 0
  return 1
}

# Human-readable rendering used in help text and the status bar:
#   'ALT|SHIFT c' -> 'Alt+Shift+C'
ad2_keyspec_label() {
  local spec=$1 mods key out='' m
  mods=${spec%% *}
  key=$(ad2_trim "${spec#* }")
  if [[ $mods != 'NONE' ]]; then
    local IFS='|'
    # shellcheck disable=SC2206
    local parts=($mods)
    unset IFS
    for m in "${parts[@]}"; do
      case $m in
        CTRL)  out+='Ctrl+' ;;
        ALT)   out+='Alt+' ;;
        SHIFT) out+='Shift+' ;;
        SUPER) out+='Super+' ;;
      esac
    done
  fi
  if [[ $key =~ ^[a-z]$ ]]; then
    key=${key^^}
  fi
  printf '%s%s' "$out" "$key"
}

ad2_valid_mouse_button() {
  case $1 in
    Right|Middle|off) return 0 ;;
    *) return 1 ;;
  esac
}

# 0.20 .. 1.00, two decimals or fewer.
ad2_valid_brightness() {
  local v=$1
  [[ $v =~ ^(0?\.[0-9]{1,2}|1(\.0{1,2})?|0)$ ]] || return 1
  # Reject anything below 0.2: unreadable panes are not a feature.
  awk -v x="$v" 'BEGIN { exit !(x >= 0.2 && x <= 1.0) }'
}

# ---------------------------------------------------------------------------
# Config loading
# ---------------------------------------------------------------------------

declare -gA AD2_CONFIG=()

ad2_key_allowed() {
  local key=$1 k
  for k in "${AD2_CONFIG_KEYS[@]}"; do
    [[ $k == "$key" ]] && return 0
  done
  return 1
}

# ad2_config_load <file>
# Populates AD2_CONFIG. Missing file is not an error (first run).
# Returns 0 always unless the file exists but cannot be read.
ad2_config_load() {
  local file=$1
  AD2_CONFIG=()
  [[ -e $file ]] || return 0
  if [[ ! -r $file ]]; then
    ad2_err "config file is not readable: $file"
    return 1
  fi

  local line key value lineno=0
  while IFS= read -r line || [[ -n $line ]]; do
    lineno=$((lineno + 1))
    line=$(ad2_trim "$line")
    [[ -z $line || ${line:0:1} == '#' ]] && continue
    if [[ $line != *=* ]]; then
      ad2_warn "$file:$lineno: ignoring line without '=': $line"
      continue
    fi
    key=$(ad2_trim "${line%%=*}")
    value=${line#*=}
    value=$(ad2_trim "$value")
    value=$(ad2_unquote "$value")
    if [[ ! $key =~ ^[A-Za-z_][A-Za-z0-9_]*$ ]]; then
      ad2_warn "$file:$lineno: ignoring invalid key name: $key"
      continue
    fi
    if ! ad2_key_allowed "$key"; then
      ad2_warn "$file:$lineno: ignoring unknown key: $key"
      continue
    fi
    AD2_CONFIG[$key]=$value
  done <"$file"
  return 0
}

# ad2_config_get <key> [default]
ad2_config_get() {
  local key=$1 default=${2-}
  if [[ -n ${AD2_CONFIG[$key]+x} && -n ${AD2_CONFIG[$key]} ]]; then
    printf '%s' "${AD2_CONFIG[$key]}"
  else
    printf '%s' "$default"
  fi
}

# ad2_config_write <file> [mode]
# Writes the current AD2_CONFIG atomically. Mode defaults to 0600.
ad2_config_write() {
  local file=$1 mode=${2:-0600} dir key tmp
  dir=$(dirname -- "$file")
  mkdir -p -m 0700 -- "$dir" || return 1
  tmp=$(mktemp -- "$file.XXXXXX") || return 1
  chmod "$mode" -- "$tmp" || { rm -f -- "$tmp"; return 1; }
  {
    printf '# ArchiDev2 configuration\n'
    printf '# KEY=VALUE, one per line. Values are literal (never expanded).\n'
    printf '# Commands are whitespace-separated argv lists, no quoting inside.\n'
    for key in "${AD2_CONFIG_KEYS[@]}"; do
      if [[ -n ${AD2_CONFIG[$key]+x} && -n ${AD2_CONFIG[$key]} ]]; then
        printf '%s=%s\n' "$key" "$(ad2_quote_for_config "${AD2_CONFIG[$key]}")"
      fi
    done
  } >"$tmp" || { rm -f -- "$tmp"; return 1; }
  mv -f -- "$tmp" "$file"
}

# ---------------------------------------------------------------------------
# Legacy config migration
# ---------------------------------------------------------------------------
# The pre-rename prototype wrote ~/.config/archidev-flow/config. That directory
# also belongs to the old ArchiDev-Flow Electron app, so this is deliberately
# narrow: read one file, write one file, delete nothing.
#
# ad2_migrate_legacy_config <new-file> <legacy-file>
# Exit codes:
#   0  migrated (prints a report to stderr)
#   1  nothing to do (new config already exists, or no legacy config)
#   2  legacy config found but unusable / write failed
ad2_migrate_legacy_config() {
  local new_file=$1 legacy=$2 mode key migrated=()

  # Never overwrite an existing ArchiDev2 config, whatever else is true.
  [[ -e $new_file ]] && return 1
  [[ -f $legacy && -r $legacy ]] || return 1

  # Parse with our own reader so only recognised keys travel across and the
  # new file gets ArchiDev2's header rather than the old product's.
  if ! ad2_config_load "$legacy" 2>/dev/null; then
    ad2_warn "found a legacy config at $legacy but could not read it; starting fresh"
    return 2
  fi
  for key in "${AD2_CONFIG_KEYS[@]}"; do
    [[ -n ${AD2_CONFIG[$key]+x} && -n ${AD2_CONFIG[$key]} ]] && migrated+=("$key")
  done
  if ((${#migrated[@]} == 0)); then
    AD2_CONFIG=()
    return 1
  fi

  # Preserve the old file's permissions on the new one.
  mode=$(stat -c '%a' -- "$legacy" 2>/dev/null) || mode=600
  [[ $mode =~ ^[0-7]{3,4}$ ]] || mode=600

  if ! ad2_config_write "$new_file" "$mode"; then
    ad2_err "could not write $new_file while migrating from $legacy"
    AD2_CONFIG=()
    return 2
  fi

  ad2_msg "archidev2: migrated settings from the previous prototype:"
  ad2_msg "  from: $legacy"
  ad2_msg "  to:   $new_file  (mode $mode, preserved)"
  ad2_msg "  keys: ${migrated[*]}"
  ad2_msg "The old file was left untouched; nothing else in that directory was read."
  ad2_msg ""
  return 0
}

# ---------------------------------------------------------------------------
# Path handling
# ---------------------------------------------------------------------------
# ad2_resolve_dir <path>
# Prints the canonical absolute path on success.
# Exit codes:  2 empty  3 missing  4 not a directory  5 not accessible
ad2_resolve_dir() {
  local p=$1
  [[ -n $p ]] || return 2
  # `-e` on a dangling symlink is false, which is the behaviour we want.
  if [[ ! -e $p ]]; then return 3; fi
  if [[ ! -d $p ]]; then return 4; fi
  if [[ ! -r $p || ! -x $p ]]; then return 5; fi
  # CDPATH must be neutralised or `cd foo` could land somewhere else entirely.
  # `--` protects paths beginning with a dash. `pwd -P` resolves symlinks.
  ( CDPATH= cd -P -- "$p" >/dev/null 2>&1 && pwd -P ) || return 5
}

ad2_resolve_dir_error() {
  case $1 in
    2) printf '%s' 'empty path' ;;
    3) printf '%s' 'no such file or directory' ;;
    4) printf '%s' 'not a directory' ;;
    5) printf '%s' 'not accessible (needs read+execute permission)' ;;
    *) printf '%s' 'unknown error' ;;
  esac
}

# ---------------------------------------------------------------------------
# Misc
# ---------------------------------------------------------------------------

ad2_have() { command -v -- "$1" >/dev/null 2>&1; }

# Repository root, derived from this file's location. No hardcoded paths.
ad2_repo_root() {
  local src=${BASH_SOURCE[0]} dir
  dir=$(CDPATH= cd -P -- "$(dirname -- "$src")/.." && pwd -P)
  printf '%s' "$dir"
}
