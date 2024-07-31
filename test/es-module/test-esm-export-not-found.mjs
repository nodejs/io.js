import { spawnPromisified } from '../common/index.mjs';
import * as fixtures from '../common/fixtures.mjs';
import assert from 'node:assert';
import { execPath } from 'node:process';
import { describe, it } from 'node:test';


const importStatement = 'import { foo, notfound } from \'./module-named-exports.mjs\';';
const importStatementMultiline = `import {
  foo,
  notfound
} from './module-named-exports.mjs';
`;

describe('ESM: nonexistent exports', { concurrency: !process.env.TEST_PARALLEL }, () => {
  for (
    const { name, input }
    of [
      {
        input: importStatement,
        name: 'single-line import',
      },
      {
        input: importStatementMultiline,
        name: 'multi-line import',
      },
    ]
  ) {
    it(`should throw for nonexistent exports via ${name}`, async () => {
      const { code, stderr } = await spawnPromisified(execPath, [
        '--input-type=module',
        '--eval',
        input,
      ], {
        cwd: fixtures.path('es-module-loaders'),
      });

      assert.notStrictEqual(code, 0);

      // SyntaxError: The requested module './module-named-exports.mjs'
      // does not provide an export named 'notfound'
      assert.match(stderr, /SyntaxError:/);
      // The quotes ensure that the path starts with ./ and not ../
      assert.match(stderr, /'\.\/module-named-exports\.mjs'/);
      assert.match(stderr, /notfound/);
    });
  }
});
