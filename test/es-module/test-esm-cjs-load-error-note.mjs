import { spawnPromisified } from '../common/index.mjs';
import * as fixtures from '../common/fixtures.mjs';
import assert from 'node:assert';
import { execPath } from 'node:process';
import { describe, it } from 'node:test';

// Expect note to be included in the error output
// Don't match the following sentence because it can change as features are
// added.
const expectedNote = 'Failed to load the ES module';

const mustIncludeMessage = {
  getMessage: (stderr) => `${expectedNote} not found in ${stderr}`,
  includeNote: true,
};
const mustNotIncludeMessage = {
  getMessage: (stderr) => `${expectedNote} must not be included in ${stderr}`,
  includeNote: false,
};

describe('ESM: Errors for unexpected exports', { concurrency: !process.env.TEST_PARALLEL }, () => {
  for (
    const { errorNeedle, filePath, getMessage, includeNote }
    of [
      {
        // name: '',
        filePath: fixtures.path('/es-modules/es-note-unexpected-export-1.cjs'),
        ...mustIncludeMessage,
      },
      {
        // name: '',
        filePath: fixtures.path('/es-modules/es-note-unexpected-import-1.cjs'),
        ...mustIncludeMessage,
      },
      {
        // name: '',
        filePath: fixtures.path('/es-modules/es-note-promiserej-import-2.cjs'),
        ...mustNotIncludeMessage,
      },
      {
        // name: '',
        filePath: fixtures.path('/es-modules/es-note-unexpected-import-3.cjs'),
        ...mustIncludeMessage,
      },
      {
        // name: '',
        filePath: fixtures.path('/es-modules/es-note-unexpected-import-4.cjs'),
        ...mustIncludeMessage,
      },
      {
        // name: '',
        filePath: fixtures.path('/es-modules/es-note-unexpected-import-5.cjs'),
        ...mustNotIncludeMessage,
      },
      {
        // name: '',
        filePath: fixtures.path('/es-modules/es-note-error-1.mjs'),
        ...mustNotIncludeMessage,
        errorNeedle: /Error: some error/,
      },
      {
        // name: '',
        filePath: fixtures.path('/es-modules/es-note-error-2.mjs'),
        ...mustNotIncludeMessage,
        errorNeedle: /string/,
      },
      {
        // name: '',
        filePath: fixtures.path('/es-modules/es-note-error-3.mjs'),
        ...mustNotIncludeMessage,
        errorNeedle: /null/,
      },
      {
        // name: '',
        filePath: fixtures.path('/es-modules/es-note-error-4.mjs'),
        ...mustNotIncludeMessage,
        errorNeedle: /undefined/,
      },
    ]
  ) {
    it(`should ${includeNote ? '' : 'NOT'} include note`, async () => {
      const { code, stderr } = await spawnPromisified(execPath, [filePath]);

      assert.strictEqual(code, 1);

      if (errorNeedle != null) assert.match(stderr, errorNeedle);

      const shouldIncludeNote = stderr.includes(expectedNote);
      assert.ok(
        includeNote ? shouldIncludeNote : !shouldIncludeNote,
        `${filePath} ${getMessage(stderr)}`,
      );
    });
  }
});
