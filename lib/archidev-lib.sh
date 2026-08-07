#!/usr/bin/env bash
# shellcheck shell=bash
#
# archidev-lib.sh - shared helpers for ArchiDev-Flow.
#
# Sourced by bin/archidev-flow, scripts/check-dependencies and tests/run-tests.
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

ADF_DEFAULT_CONSULTANT_COMMAND='codex'
ADF_DEFAULT_DEVELOPER_COMMAND='claude'
ADF_DEFAULT_FILE_MANAGER_COMMAND='yazi'
ADF_DEFAULT_EDITOR_COMMAND='micro'

# Keys accepted in the user config file. Anything else is ignored with a
# warning, so a typo can never silently change behaviour.
ADF_CONFIG_KEYS=(
  CONSULTANT_DIR
  CONSULTANT_COMMAND
  DEVELOPER_COMMAND
  FILE_MANAGER_COMMAND
  EDITOR_COMMAND
)

# ---------------------------------------------------------------------------
# Output helpers (all diagnostics go to stderr so --dry-run stdout stays clean)
# ---------------------------------------------------------------------------

adf_msg()  { printf '%s\n' "$*" >&2; }
adf_warn() { printf 'archidev-flow: warning: %s\n' "$*" >&2; }
adf_err()  { printf 'archidev-flow: error: %s\n' "$*" >&2; }

adf_die() {
  local code=1
  if [[ ${1:-} =~ ^[0-9]+$ ]]; then code=$1; shift; fi
  adf_err "$*"
  exit "$code"
}

# ---------------------------------------------------------------------------
# XDG locations
# ---------------------------------------------------------------------------

adf_config_home() {
  local base=${XDG_CONFIG_HOME:-}
  # XDG says relative values must be ignored.
  if [[ -z $base || $base != /* ]]; then
    base=${HOME:?HOME is not set}/.config
  fi
  printf '%s\n' "$base/archidev-flow"
}

adf_config_file() {
  printf '%s\n' "$(adf_config_home)/config"
}

# ---------------------------------------------------------------------------
# String helpers
# ---------------------------------------------------------------------------

adf_trim() {
  local s=$1
  s=${s#"${s%%[![:space:]]*}"}   # leading
  s=${s%"${s##*[![:space:]]}"}   # trailing
  printf '%s' "$s"
}

# Strip one layer of matching surrounding quotes. The inside is taken
# literally: no expansion, no backslash escapes, no globbing.
adf_unquote() {
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
adf_quote_for_config() {
  local v=$1
  if [[ $v != *"'"* ]]; then
    printf "'%s'" "$v"
  elif [[ $v != *'"'* ]]; then
    printf '"%s"' "$v"
  else
    # Contains both quote characters: write it bare. Only leading/trailing
    # whitespace would be lost on re-read, which we strip here anyway.
    printf '%s' "$(adf_trim "$v")"
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
adf_split_command() {
  local -n _out=$1
  local raw=$2
  # `read -a` word-splits on IFS without globbing or expansion.
  read -r -a _out <<<"$raw"
}

# ---------------------------------------------------------------------------
# Config loading
# ---------------------------------------------------------------------------

declare -gA ADF_CONFIG=()

adf_key_allowed() {
  local key=$1 k
  for k in "${ADF_CONFIG_KEYS[@]}"; do
    [[ $k == "$key" ]] && return 0
  done
  return 1
}

# adf_config_load <file>
# Populates ADF_CONFIG. Missing file is not an error (first run).
# Returns 0 always unless the file exists but cannot be read.
adf_config_load() {
  local file=$1
  ADF_CONFIG=()
  [[ -e $file ]] || return 0
  if [[ ! -r $file ]]; then
    adf_err "config file is not readable: $file"
    return 1
  fi

  local line key value lineno=0
  while IFS= read -r line || [[ -n $line ]]; do
    lineno=$((lineno + 1))
    line=$(adf_trim "$line")
    [[ -z $line || ${line:0:1} == '#' ]] && continue
    if [[ $line != *=* ]]; then
      adf_warn "$file:$lineno: ignoring line without '=': $line"
      continue
    fi
    key=$(adf_trim "${line%%=*}")
    value=${line#*=}
    value=$(adf_trim "$value")
    value=$(adf_unquote "$value")
    if [[ ! $key =~ ^[A-Za-z_][A-Za-z0-9_]*$ ]]; then
      adf_warn "$file:$lineno: ignoring invalid key name: $key"
      continue
    fi
    if ! adf_key_allowed "$key"; then
      adf_warn "$file:$lineno: ignoring unknown key: $key"
      continue
    fi
    ADF_CONFIG[$key]=$value
  done <"$file"
  return 0
}

# adf_config_get <key> [default]
adf_config_get() {
  local key=$1 default=${2-}
  if [[ -n ${ADF_CONFIG[$key]+x} && -n ${ADF_CONFIG[$key]} ]]; then
    printf '%s' "${ADF_CONFIG[$key]}"
  else
    printf '%s' "$default"
  fi
}

# adf_config_write <file>  - writes the current ADF_CONFIG, 0600, atomically.
adf_config_write() {
  local file=$1 dir key tmp
  dir=$(dirname -- "$file")
  mkdir -p -m 0700 -- "$dir" || return 1
  tmp=$(mktemp -- "$file.XXXXXX") || return 1
  chmod 0600 -- "$tmp" || { rm -f -- "$tmp"; return 1; }
  {
    printf '# ArchiDev-Flow configuration\n'
    printf '# KEY=VALUE, one per line. Values are literal (never expanded).\n'
    printf '# Commands are whitespace-separated argv lists, no quoting inside.\n'
    for key in "${ADF_CONFIG_KEYS[@]}"; do
      if [[ -n ${ADF_CONFIG[$key]+x} && -n ${ADF_CONFIG[$key]} ]]; then
        printf '%s=%s\n' "$key" "$(adf_quote_for_config "${ADF_CONFIG[$key]}")"
      fi
    done
  } >"$tmp" || { rm -f -- "$tmp"; return 1; }
  mv -f -- "$tmp" "$file"
}

# ---------------------------------------------------------------------------
# Path handling
# ---------------------------------------------------------------------------
# adf_resolve_dir <path>
# Prints the canonical absolute path on success.
# Exit codes:  2 empty  3 missing  4 not a directory  5 not accessible
adf_resolve_dir() {
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

adf_resolve_dir_error() {
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

adf_have() { command -v -- "$1" >/dev/null 2>&1; }

# Repository root, derived from this file's location. No hardcoded paths.
adf_repo_root() {
  local src=${BASH_SOURCE[0]} dir
  dir=$(CDPATH= cd -P -- "$(dirname -- "$src")/.." && pwd -P)
  printf '%s' "$dir"
}
