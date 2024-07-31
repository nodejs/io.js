import { spawnPromisified } from '../common/index.mjs';
import fixtures from '../common/fixtures.js';
import assert from 'node:assert';
import http from 'node:http';
import path from 'node:path';
import { execPath } from 'node:process';
import { promisify } from 'node:util';
import { describe, it, after } from 'node:test';


const files = {
  'main.mjs': 'export * from "./lib.mjs";',
  'lib.mjs': 'export { sum } from "./sum.mjs";',
  'sum.mjs': 'export function sum(a, b) { return a + b }',
  'console.mjs': `
    import { sum } from './sum.mjs';
    globalThis.sum = sum;
    console.log("loaded over http");
  `,
};

const requestListener = ({ url }, rsp) => {
  const filename = path.basename(url);
  const content = files[filename];

  if (content) {
    return rsp
      .writeHead(200, { 'Content-Type': 'application/javascript' })
      .end(content);
  }

  return rsp
    .writeHead(404)
    .end();
};

const server = http.createServer(requestListener);

await promisify(server.listen.bind(server))({
  host: '127.0.0.1',
  port: 0,
});

const {
  address: host,
  port,
} = server.address();

describe('ESM: http import via loader', { concurrency: !process.env.TEST_PARALLEL }, () => {
  it('should load using --import flag', async () => {
    // ! MUST NOT use spawnSync to avoid blocking the event loop
    const { code, signal, stderr, stdout } = await spawnPromisified(
      execPath,
      [
        '--no-warnings',
        '--loader',
        fixtures.fileURL('es-module-loaders', 'http-loader.mjs'),
        '--import', `http://${host}:${port}/console.mjs`,
        '--eval',
        'console.log(sum(1, 2))',
      ]
    );
    assert.strictEqual(stderr, '');
    assert.match(stdout, /loaded over http\r?\n3/);
    assert.strictEqual(code, 0);
    assert.strictEqual(signal, null);
  });
  it('should load using import inside --eval code', async () => {
    // ! MUST NOT use spawnSync to avoid blocking the event loop
    const { code, signal, stderr, stdout } = await spawnPromisified(
      execPath,
      [
        '--no-warnings',
        '--loader',
        fixtures.fileURL('es-module-loaders', 'http-loader.mjs'),
        '--input-type=module',
        '--eval',
        `import * as main from 'http://${host}:${port}/main.mjs'; console.log(main)`,
      ]
    );

    assert.strictEqual(stderr, '');
    assert.strictEqual(stdout, '[Module: null prototype] { sum: [Function: sum] }\n');
    assert.strictEqual(code, 0);
    assert.strictEqual(signal, null);
  });

  after(() => server.close());
});
