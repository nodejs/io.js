import { mock } from 'node:test';

try {
  mock.module?.('Whatever, this is not significant', { namedExports: {} });
} catch {}

const { string } = await import('./basic-esm-without-extension');
console.log(`Found string: ${string}`); // prints 'original esm string'
