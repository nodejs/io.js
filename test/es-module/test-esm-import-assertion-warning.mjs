import { spawnPromisified } from '../common/index.mjs';
import assert from 'node:assert';
import { execPath } from 'node:process';

await Promise.all([
  // Using importAssertions in the resolve hook should warn but still work.
  `data:text/javascript,export ${encodeURIComponent(function resolve() {
    return { shortCircuit: true, url: 'data:application/json,1', importAssertions: { type: 'json' } };
  })}`,
  // Using importAssertions on the context object of the resolve hook should warn but still work.
  `data:text/javascript,export ${encodeURIComponent(function resolve(s, c, n) {
    const type = c.importAssertions.type;
    return { shortCircuit: true, url: 'data:application/json,1', importAttributes: { type: type ?? 'json' } };
  })}`,
  // Setting importAssertions on the context object of the load hook should warn but still work.
  `data:text/javascript,export ${encodeURIComponent(function load(u, c, n) {
    c.importAssertions = { type: 'json' };
    return n('data:application/json,1', c);
  })}`,
  // Creating a new context object with importAssertions in the load hook should warn but still work.
  `data:text/javascript,export ${encodeURIComponent(function load(u, c, n) {
    return n('data:application/json,1', { importAssertions: { type: 'json' } });
  })}`,
].map(async (loaderURL) => {
  const { stdout, stderr, code } = await spawnPromisified(execPath, [
    '--input-type=module',
    '--eval', `
    import assert from 'node:assert';
    import { register } from 'node:module';

    register(${JSON.stringify(loaderURL)});

    assert.deepStrictEqual(
      { ...await import('data:') },
      { default: 1 }
      );`,
  ]);

  assert.match(stderr, /Use `importAttributes` instead of `importAssertions`/);
  assert.strictEqual(stdout, '');
  assert.strictEqual(code, 0);
}));
