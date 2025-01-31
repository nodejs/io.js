'use strict';

require('../../common');

const spawn = require('child_process').spawn;

function run(cmd, strict, cb) {
    const args = ['--disable-warning=ExperimentalWarning'];
    if (strict) args.push('--use_strict');
    args.push('-p');
    const child = spawn(process.execPath, args);
    child.stdout.pipe(process.stdout);
    child.stderr.pipe(process.stdout);
    child.stdin.end(cmd);
    child.on('close', cb);
}

const queue =
    [
        'enum Foo{};',
        'throw new SyntaxError("hello")',
        'const foo;',
        'let x: number = 100;x;',
        'const foo: string = 10;',
        'function foo(){};foo<Number>(1);',
        'interface Foo{};const foo;',
        'function foo(){ await Promise.resolve(1)};',
    ];

function go() {
    const c = queue.shift();
    if (!c) return console.log('done');
    run(c, false, function () {
        run(c, true, go);
    });
}

go();
