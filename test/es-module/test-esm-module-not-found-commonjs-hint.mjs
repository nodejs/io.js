import { spawnPromisified } from '../common/index.mjs';
import { fixturesDir, fileURL as fixtureSubDir } from '../common/fixtures.mjs';
import { match, notStrictEqual } from 'node:assert';
import { execPath } from 'node:process';
import { describe, it } from 'node:test';


describe('ESM: module not found hint', { concurrency: !process.env.TEST_PARALLEL }, () => {
  for (
    const { input, expected, cwd = fixturesDir }
    of [
      {
        input: 'import "./print-error-message"',
        // Did you mean to import "./print-error-message.js"?
        expected: / "\.\/print-error-message\.js"\?/,
      },
      {
        input: 'import "./es-modules/folder%25with percentage#/index.js"',
        // Did you mean to import "./es-modules/folder%2525with%20percentage%23/index.js"?
        expected: / "\.\/es-modules\/folder%2525with%20percentage%23\/index\.js"\?/,
      },
      {
        input: 'import "../folder%25with percentage#/index.js"',
        // Did you mean to import "../es-modules/folder%2525with%20percentage%23/index.js"?
        expected: / "\.\.\/folder%2525with%20percentage%23\/index\.js"\?/,
        cwd: fixtureSubDir('es-modules/tla/'),
      },
      {
        input: 'import obj from "some_module/obj"',
        expected: / "some_module\/obj\.js"\?/,
      },
      {
        input: 'import obj from "some_module/folder%25with percentage#/index.js"',
        expected: / "some_module\/folder%2525with%20percentage%23\/index\.js"\?/,
      },
      {
        input: 'import "@nodejsscope/pkg/index"',
        expected: / "@nodejsscope\/pkg\/index\.js"\?/,
      },
      {
        input: 'import obj from "lone_file.js"',
        expected: /node_modules\/lone_file\.js"\?/,
      },
    ]
  ) it('should cite a variant form', async () => {
    const { code, stderr } = await spawnPromisified(execPath, [
      '--input-type=module',
      '--eval',
      input,
    ], { cwd });

    match(stderr, expected);
    notStrictEqual(code, 0);
  });
});
