import { spawnPromisified } from '../common/index.mjs';
import { fileURL } from '../common/fixtures.mjs';
import { match, strictEqual } from 'node:assert';
import { execPath } from 'node:process';
import { describe, it } from 'node:test';


describe('ESM: non-js extensions fail', { concurrency: !process.env.TEST_PARALLEL }, () => {
  it(async () => {
    const { code, stderr, signal } = await spawnPromisified(execPath, [
      '--input-type=module',
      '--eval',
      `import ${JSON.stringify(fileURL('es-modules', 'file.unknown'))}`,
    ]);

    match(stderr, /ERR_UNKNOWN_FILE_EXTENSION/);
    strictEqual(code, 1);
    strictEqual(signal, null);
  });
});
