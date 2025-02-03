const foo = 'FOO' in process.env;
const bar = Object.hasOwn(process.env, 'BAR');
const baz = process.env.hasOwnProperty('BAZ');
