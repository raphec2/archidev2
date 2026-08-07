// Run the *pure* helper functions from wezterm/archidev2.lua in a real
// Lua VM (fengari) and assert their behaviour. No wezterm needed.
//
// Optional: the repository itself has no dependencies. To enable this check,
// install the two parsers somewhere temporary and point tests/run-tests at it:
//
//   d=$(mktemp -d); (cd "$d" && npm install luaparse fengari)
//   AD2_NODE_MODULES=$d ./tests/run-tests
//
// Usage: node tests/lua-behaviour.js wezterm/archidev2.lua
const fs = require('fs');
const { lua, lauxlib, lualib, to_luastring, to_jsstring } = require('fengari');

const src = fs.readFileSync(process.argv[2], 'utf8');

// Extract the two pure functions verbatim from the config.
function grab(name) {
  const re = new RegExp(`local function ${name}\\(([\\s\\S]*?)\\nend\\n`, 'm');
  const m = src.match(re);
  if (!m) throw new Error('could not find function ' + name);
  return `local function ${name}(${m[1]}\nend\n`;
}

const prelude = grab('split_argv') + grab('strip_trailing_newlines');

const tests = `
local function eq(a, b, msg)
  if a ~= b then error(msg .. ': expected ' .. string.format('%q', tostring(b))
    .. ' got ' .. string.format('%q', tostring(a))) end
  print('  ok   ' .. msg)
end
local function join(t) return table.concat(t, '|') end

eq(join(split_argv('codex')), 'codex', 'split_argv: single word')
eq(#split_argv('codex'), 1, 'split_argv: single word arity')
eq(join(split_argv('codex\\n--model\\ngpt-5')), 'codex|--model|gpt-5', 'split_argv: three words')
eq(#split_argv(''), 0, 'split_argv: empty string')
eq(#split_argv(nil), 0, 'split_argv: nil')
eq(join(split_argv('a\\n\\n\\nb')), 'a|b', 'split_argv: blank lines dropped')
eq(join(split_argv('with space\\nx')), 'with space|x', 'split_argv: spaces kept inside a word')

eq(strip_trailing_newlines('hello\\n'), 'hello', 'strip: single trailing LF')
eq(strip_trailing_newlines('hello\\n\\n\\n'), 'hello', 'strip: many trailing LFs')
eq(strip_trailing_newlines('hello\\r\\n'), 'hello', 'strip: trailing CRLF')
eq(strip_trailing_newlines('a\\nb\\nc\\n'), 'a\\nb\\nc', 'strip: interior newlines preserved')
eq(strip_trailing_newlines('hello'), 'hello', 'strip: no trailing newline')
eq(strip_trailing_newlines('\\n'), '', 'strip: newline only')
eq(strip_trailing_newlines(''), '', 'strip: empty')
eq(strip_trailing_newlines('trail  \\n'), 'trail  ', 'strip: spaces before LF kept')
print('LUA_BEHAVIOUR_OK')
`;

const L = lauxlib.luaL_newstate();
lualib.luaL_openlibs(L);
if (lauxlib.luaL_dostring(L, to_luastring(prelude + tests)) !== lua.LUA_OK) {
  console.error('LUA ERROR: ' + to_jsstring(lua.lua_tostring(L, -1)));
  process.exit(1);
}
