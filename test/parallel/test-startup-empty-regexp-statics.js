'use strict';

const common = require('../common');

if (common.isInsideDirWithUnusualChars) {
  common.skip('expected failure');
}

const assert = require('node:assert');
const { spawnSync, spawn } = require('node:child_process');

assert.strictEqual(RegExp.$_, '');
assert.strictEqual(RegExp.$0, undefined);
assert.strictEqual(RegExp.$1, '');
assert.strictEqual(RegExp.$2, '');
assert.strictEqual(RegExp.$3, '');
assert.strictEqual(RegExp.$4, '');
assert.strictEqual(RegExp.$5, '');
assert.strictEqual(RegExp.$6, '');
assert.strictEqual(RegExp.$7, '');
assert.strictEqual(RegExp.$8, '');
assert.strictEqual(RegExp.$9, '');
assert.strictEqual(RegExp.input, '');
assert.strictEqual(RegExp.lastMatch, '');
assert.strictEqual(RegExp.lastParen, '');
assert.strictEqual(RegExp.leftContext, '');
assert.strictEqual(RegExp.rightContext, '');
assert.strictEqual(RegExp['$&'], '');
assert.strictEqual(RegExp['$`'], '');
assert.strictEqual(RegExp['$+'], '');
assert.strictEqual(RegExp["$'"], '');

const allRegExpStatics =
    'RegExp.$_ + RegExp["$&"] + RegExp["$`"] + RegExp["$+"] + RegExp["$\'"] + ' +
    'RegExp.input + RegExp.lastMatch + RegExp.lastParen + ' +
    'RegExp.leftContext + RegExp.rightContext + ' +
    Array.from({ length: 10 }, (_, i) => `RegExp.$${i}`).join(' + ');

{
  const child = spawnSync(process.execPath,
                          [ '-p', allRegExpStatics ],
                          { stdio: ['inherit', 'pipe', 'inherit'] });
  assert.match(child.stdout.toString(), /^undefined\r?\n$/);
  assert.strictEqual(child.status, 0);
  assert.strictEqual(child.signal, null);
}

{
  const child = spawnSync(process.execPath,
                          [ '--expose-internals', '-p', `const {
                            SideEffectFreeRegExpPrototypeExec,
                            SideEffectFreeRegExpPrototypeSymbolReplace,
                            SideEffectFreeRegExpPrototypeSymbolSplit,
                          } = require("internal/util");
                          SideEffectFreeRegExpPrototypeExec(/foo/, "foo");
                          SideEffectFreeRegExpPrototypeSymbolReplace(/o/, "foo", "a");
                          SideEffectFreeRegExpPrototypeSymbolSplit(/o/, "foo");
                          ${allRegExpStatics}` ],
                          { stdio: ['inherit', 'pipe', 'inherit'] });
  assert.match(child.stdout.toString(), /^undefined\r?\n$/);
  assert.strictEqual(child.status, 0);
  assert.strictEqual(child.signal, null);
}

{
  const child = spawnSync(process.execPath,
                          [ '-e', `console.log(${allRegExpStatics})`, '--input-type=module' ],
                          { stdio: ['inherit', 'pipe', 'inherit'] });
  assert.match(child.stdout.toString(), /^undefined\r?\n$/);
  assert.strictEqual(child.status, 0);
  assert.strictEqual(child.signal, null);
}

if (!common.isInsideDirWithUnusualChars) {
  const child = spawn(process.execPath, [], { stdio: ['pipe', 'pipe', 'inherit'], encoding: 'utf8' });

  let stdout = '';
  child.stdout.on('data', (chunk) => {
    stdout += chunk;
  });

  child.on('exit', common.mustCall((status, signal) => {
    assert.match(stdout, /^undefined\r?\n$/);
    assert.strictEqual(status, 0);
    assert.strictEqual(signal, null);
  }));
  child.on('error', common.mustNotCall());

  child.stdin.end(`console.log(${allRegExpStatics});\n`);
}
