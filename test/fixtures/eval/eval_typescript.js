'use strict';

require('../../common');

const spawnSync = require('child_process').spawnSync;

const queue = [
    'enum Foo{};',
    'throw new SyntaxError("hello")',
    'const foo;',
    'let x: number = 100;x;',
    'const foo: string = 10;',
    'function foo(){};foo<Number>(1);',
    'interface Foo{};const foo;',
    'function foo(){ await Promise.resolve(1)};',
];

for (const cmd of queue) {
    const args = ['--disable-warning=ExperimentalWarning', '-p', cmd];
    const result = spawnSync(process.execPath, args, {
        stdio: 'pipe'
    });
    process.stdout.write(result.stdout);
    process.stdout.write(result.stderr);
}
