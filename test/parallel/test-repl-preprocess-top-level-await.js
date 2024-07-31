'use strict';

require('../common');
const assert = require('assert');
const { processTopLevelAwait } = require('internal/repl/await');

// Flags: --expose-internals

// This test was created based on
// https://cs.chromium.org/chromium/src/third_party/WebKit/LayoutTests/http/tests/inspector-unit/preprocess-top-level-awaits.js?rcl=358caaba5e763e71c4abb9ada2d9cd8b1188cac9

const surrogate = (
  '"\u{1F601}\u{1f468}\u200d\u{1f469}\u200d\u{1f467}\u200d\u{1f466}"'
);

const testCases = [
  [ '0',
    null ],
  [ 'await 0',
    '(async () => { return { value: (await 0) } })()' ],
  [ `await ${surrogate}`,
    `(async () => { return { value: (await ${surrogate}) } })()` ],
  [ 'await 0;',
    '(async () => { return { value: (await 0) }; })()' ],
  [ 'await 0;;;',
    '(async () => { return { value: (await 0) };;; })()' ],
  [ `await ${surrogate};`,
    `(async () => { return { value: (await ${surrogate}) }; })()` ],
  [ `await ${surrogate};`,
    `(async () => { return { value: (await ${surrogate}) }; })()` ],
  [ '(await 0)',
    '(async () => { return ({ value: (await 0) }) })()' ],
  [ `(await ${surrogate})`,
    `(async () => { return ({ value: (await ${surrogate}) }) })()` ],
  [ '(await 0);',
    '(async () => { return ({ value: (await 0) }); })()' ],
  [ `(await ${surrogate});`,
    `(async () => { return ({ value: (await ${surrogate}) }); })()` ],
  [ 'async function foo() { await 0; }',
    null ],
  [ 'async () => await 0',
    null ],
  [ 'class A { async method() { await 0 } }',
    null ],
  [ 'await 0; return 0;',
    null ],
  [ `await ${surrogate}; await ${surrogate};`,
    `(async () => { await ${surrogate}; return { value: (await ${surrogate}) }; })()` ],
  [ 'var a = await 1',
    'var a; (async () => { void (a = await 1) })()' ],
  [ `var a = await ${surrogate}`,
    `var a; (async () => { void (a = await ${surrogate}) })()` ],
  [ 'let a = await 1',
    'let a; (async () => { void (a = await 1) })()' ],
  [ 'const a = await 1',
    'let a; (async () => { void (a = await 1) })()' ],
  [ 'for (var i = 0; i < 1; ++i) { await i }',
    'var i; (async () => { for (void (i = 0); i < 1; ++i) { await i } })()' ],
  [ 'for (let i = 0; i < 1; ++i) { await i }',
    '(async () => { for (let i = 0; i < 1; ++i) { await i } })()' ],
  [ 'var {a} = {a:1}, [b] = [1], {c:{d}} = {c:{d: await 1}}',
    'var a, b, d; (async () => { void ( ({a} = {a:1}), ([b] = [1]), ' +
        '({c:{d}} = {c:{d: await 1}})) })()' ],
  [ 'let [a, b, c] = await ([1, 2, 3])',
    'let a, b, c; (async () => { void ([a, b, c] = await ([1, 2, 3])) })()'],
  [ 'let {a,b,c} = await ({a: 1, b: 2, c: 3})',
    'let a, b, c; (async () => { void ({a,b,c} = ' +
        'await ({a: 1, b: 2, c: 3})) })()'],
  [ 'let {a: [b]} = {a: [await 1]}, [{d}] = [{d: 3}]',
    'let b, d; (async () => { void ( ({a: [b]} = {a: [await 1]}),' +
        ' ([{d}] = [{d: 3}])) })()'],
  /* eslint-disable no-template-curly-in-string */
  [ 'console.log(`${(await { a: 1 }).a}`)',
    '(async () => { return { value: (console.log(`${(await { a: 1 }).a}`)) } })()' ],
  /* eslint-enable no-template-curly-in-string */
  [ 'await 0; function foo() {}',
    'var foo; (async () => { await 0; this.foo = foo; function foo() {} })()' ],
  [ 'await 0; class Foo {}',
    'let Foo; (async () => { await 0; Foo=class Foo {} })()' ],
  [ 'if (await true) { function foo() {} }',
    'var foo; (async () => { ' +
      'if (await true) { this.foo = foo; function foo() {} } })()' ],
  [ 'if (await true) { class Foo{} }',
    '(async () => { if (await true) { class Foo{} } })()' ],
  [ 'if (await true) { var a = 1; }',
    'var a; (async () => { if (await true) { void (a = 1); } })()' ],
  [ 'if (await true) { let a = 1; }',
    '(async () => { if (await true) { let a = 1; } })()' ],
  [ 'var a = await 1; let b = 2; const c = 3;',
    'var a; let b; let c; (async () => { void (a = await 1); void (b = 2);' +
        ' void (c = 3); })()' ],
  [ 'let o = await 1, p',
    'let o, p; (async () => { void ( (o = await 1), (p=undefined)) })()' ],
  [ 'await (async () => { let p = await 1; return p; })()',
    '(async () => { return { value: (await (async () => ' +
      '{ let p = await 1; return p; })()) } })()' ],
  [ '{ let p = await 1; }',
    '(async () => { { let p = await 1; } })()' ],
  [ 'var p = await 1',
    'var p; (async () => { void (p = await 1) })()' ],
  [ 'await (async () => { var p = await 1; return p; })()',
    '(async () => { return { value: (await (async () => ' +
      '{ var p = await 1; return p; })()) } })()' ],
  [ '{ var p = await 1; }',
    'var p; (async () => { { void (p = await 1); } })()' ],
  [ 'for await (var i of asyncIterable) { i; }',
    'var i; (async () => { for await (i of asyncIterable) { i; } })()'],
  [ 'for await (var [i] of asyncIterable) { i; }',
    'var i; (async () => { for await ([i] of asyncIterable) { i; } })()'],
  [ 'for await (var {i} of asyncIterable) { i; }',
    'var i; (async () => { for await ({i} of asyncIterable) { i; } })()'],
  [ 'for await (var [{i}, [j]] of asyncIterable) { i; }',
    'var i, j; (async () => { for await ([{i}, [j]] of asyncIterable)' +
      ' { i; } })()'],
  [ 'for await (let i of asyncIterable) { i; }',
    '(async () => { for await (let i of asyncIterable) { i; } })()'],
  [ 'for await (const i of asyncIterable) { i; }',
    '(async () => { for await (const i of asyncIterable) { i; } })()'],
  [ 'for (var i of [1,2,3]) { await 1; }',
    'var i; (async () => { for (i of [1,2,3]) { await 1; } })()'],
  [ 'for (var [i] of [[1], [2]]) { await 1; }',
    'var i; (async () => { for ([i] of [[1], [2]]) { await 1; } })()'],
  [ 'for (var {i} of [{i: 1}, {i: 2}]) { await 1; }',
    'var i; (async () => { for ({i} of [{i: 1}, {i: 2}]) { await 1; } })()'],
  [ 'for (var [{i}, [j]] of [[{i: 1}, [2]]]) { await 1; }',
    'var i, j; (async () => { for ([{i}, [j]] of [[{i: 1}, [2]]])' +
      ' { await 1; } })()'],
  [ 'for (let i of [1,2,3]) { await 1; }',
    '(async () => { for (let i of [1,2,3]) { await 1; } })()'],
  [ 'for (const i of [1,2,3]) { await 1; }',
    '(async () => { for (const i of [1,2,3]) { await 1; } })()'],
  [ 'for (var i in {x:1}) { await 1 }',
    'var i; (async () => { for (i in {x:1}) { await 1 } })()'],
  [ 'for (var [a,b] in {xy:1}) { await 1 }',
    'var a, b; (async () => { for ([a,b] in {xy:1}) { await 1 } })()'],
  [ 'for (let i in {x:1}) { await 1 }',
    '(async () => { for (let i in {x:1}) { await 1 } })()'],
  [ 'for (const i in {x:1}) { await 1 }',
    '(async () => { for (const i in {x:1}) { await 1 } })()'],
  [ 'var x = await foo(); async function foo() { return Promise.resolve(1);}',
    'var x; var foo; (async () => { void (x = await foo()); this.foo = foo; ' +
      'async function foo() { return Promise.resolve(1);} })()'],
  [ '(await x).y',
    '(async () => { return { value: ((await x).y) } })()'],
  [ 'await (await x).y',
    '(async () => { return { value: (await (await x).y) } })()'],
  [ 'var { ...rest } = await {}',
    'var rest; (async () => { void ({ ...rest } = await {}) })()',
  ],
];

for (const [input, expected] of testCases) {
  assert.strictEqual(processTopLevelAwait(input), expected);
}
