// Flags: --experimental-import-meta-resolve
import { spawnPromisified } from '../common/index.mjs';
import { fileURL as fixturesFileURL } from '../common/fixtures.mjs';
import assert from 'assert';
import { spawn } from 'child_process';
import { execPath } from 'process';

const fixtures = `${fixturesFileURL()}/`;

assert.strictEqual(import.meta.resolve('./test-esm-import-meta.mjs'),
                   new URL('./test-esm-import-meta.mjs', import.meta.url).href);
assert.strictEqual(import.meta.resolve('./notfound.mjs'), new URL('./notfound.mjs', import.meta.url).href);
assert.strictEqual(import.meta.resolve('./asset'), new URL('./asset', import.meta.url).href);
assert.throws(() => {
  import.meta.resolve('does-not-exist');
}, {
  code: 'ERR_MODULE_NOT_FOUND',
});
assert.strictEqual(
  import.meta.resolve('../fixtures/empty-with-bom.txt'),
  fixtures + 'empty-with-bom.txt');
assert.strictEqual(import.meta.resolve('../fixtures/'), fixtures);
assert.strictEqual(
  import.meta.resolve('../fixtures/', import.meta.url),
  fixtures);
assert.strictEqual(
  import.meta.resolve('../fixtures/', new URL(import.meta.url)),
  fixtures);
[[], {}, Symbol(), 0, 1, 1n, 1.1, () => {}, true, false].map((arg) =>
  assert.throws(() => import.meta.resolve('../fixtures/', arg), {
    code: 'ERR_INVALID_ARG_TYPE',
  })
);
assert.strictEqual(import.meta.resolve('http://some-absolute/url'), 'http://some-absolute/url');
assert.strictEqual(import.meta.resolve('some://weird/protocol'), 'some://weird/protocol');
assert.strictEqual(import.meta.resolve('baz/', fixtures),
                   fixtures + 'node_modules/baz/');
assert.deepStrictEqual(
  { ...await import('data:text/javascript,export default import.meta.resolve("http://some-absolute/url")') },
  { default: 'http://some-absolute/url' },
);
assert.deepStrictEqual(
  { ...await import('data:text/javascript,export default import.meta.resolve("some://weird/protocol")') },
  { default: 'some://weird/protocol' },
);
assert.deepStrictEqual(
  { ...await import(`data:text/javascript,export default import.meta.resolve("baz/", ${encodeURIComponent(JSON.stringify(fixtures))})`) },
  { default: fixtures + 'node_modules/baz/' },
);
assert.deepStrictEqual(
  { ...await import('data:text/javascript,export default import.meta.resolve("fs")') },
  { default: 'node:fs' },
);
await assert.rejects(import('data:text/javascript,export default import.meta.resolve("does-not-exist")'), {
  code: 'ERR_UNSUPPORTED_RESOLVE_REQUEST',
});
await assert.rejects(import('data:text/javascript,export default import.meta.resolve("./relative")'), {
  code: 'ERR_UNSUPPORTED_RESOLVE_REQUEST',
});

{
  const { stdout } = await spawnPromisified(execPath, [
    '--input-type=module',
    '--eval', 'console.log(typeof import.meta.resolve)',
  ]);
  assert.match(stdout, /^function\r?\n$/);
}

{
  const cp = spawn(execPath, [
    '--input-type=module',
  ]);
  cp.stdin.end('console.log(typeof import.meta.resolve)');
  assert.match((await cp.stdout.toArray()).toString(), /^function\r?\n$/);
}

{
  const { stdout } = await spawnPromisified(execPath, [
    '--input-type=module',
    '--eval', 'import "data:text/javascript,console.log(import.meta.resolve(%22node:os%22))"',
  ]);
  assert.match(stdout, /^node:os\r?\n$/);
}

{
  const cp = spawn(execPath, [
    '--input-type=module',
  ]);
  cp.stdin.end('import "data:text/javascript,console.log(import.meta.resolve(%22node:os%22))"');
  assert.match((await cp.stdout.toArray()).toString(), /^node:os\r?\n$/);
}

{
  const result = await spawnPromisified(execPath, [
    '--no-warnings',
    '--input-type=module',
    '--import', 'data:text/javascript,import{register}from"node:module";register("data:text/javascript,")',
    '--eval',
    'console.log(import.meta.resolve(new URL("http://example.com")))',
  ]);

  assert.deepStrictEqual(result, {
    code: 0,
    signal: null,
    stderr: '',
    stdout: 'http://example.com/\n',
  });
}

{
  const result = await spawnPromisified(execPath, [
    '--no-warnings',
    '--experimental-import-meta-resolve',
    '--eval',
    'import.meta.resolve("foo", "http://example.com/bar.js")',
  ]);
  assert.match(result.stderr, /ERR_INVALID_URL/);
  assert.strictEqual(result.stdout, '');
  assert.strictEqual(result.code, 1);
}
