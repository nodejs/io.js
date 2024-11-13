// https://github.com/nodejs/node/issues/54457
const { text } = await import('./test-require.ts');

console.log(text);
