import * as common from '../common/index.mjs';
import * as fixtures from '../common/fixtures.mjs';
import { describe, it, run } from 'node:test';
import assert from 'node:assert';
import { sep } from 'node:path';

const files = [fixtures.path('test-runner', 'coverage.js')];
const abortedSignal = AbortSignal.abort();

describe('require(\'node:test\').run coverage settings', { concurrency: true }, async () => {
  await describe('validation', async () => {
    await it('should only allow boolean in options.coverage', async () => {
      [Symbol(), {}, () => {}, 0, 1, 0n, 1n, '', '1', Promise.resolve(true), []]
        .forEach((coverage) => assert.throws(() => run({ coverage }), {
          code: 'ERR_INVALID_ARG_TYPE'
        }));
    });

    await it('should only allow string|string[] in options.coverageExcludeGlobs', async () => {
      [Symbol(), {}, () => {}, 0, 1, 0n, 1n, Promise.resolve([]), true, false]
        .forEach((coverageExcludeGlobs) => {
          assert.throws(() => run({ coverage: true, coverageExcludeGlobs }), {
            code: 'ERR_INVALID_ARG_TYPE'
          });
          assert.throws(() => run({ coverage: true, coverageExcludeGlobs: [coverageExcludeGlobs] }), {
            code: 'ERR_INVALID_ARG_TYPE'
          });
        });
      run({ files: [], signal: abortedSignal, coverage: true, coverageExcludeGlobs: [''] });
      run({ files: [], signal: abortedSignal, coverage: true, coverageExcludeGlobs: '' });
    });

    await it('should only allow string|string[] in options.coverageIncludeGlobs', async () => {
      [Symbol(), {}, () => {}, 0, 1, 0n, 1n, Promise.resolve([]), true, false]
        .forEach((coverageIncludeGlobs) => {
          assert.throws(() => run({ coverage: true, coverageIncludeGlobs }), {
            code: 'ERR_INVALID_ARG_TYPE'
          });
          assert.throws(() => run({ coverage: true, coverageIncludeGlobs: [coverageIncludeGlobs] }), {
            code: 'ERR_INVALID_ARG_TYPE'
          });
        });

      run({ files: [], signal: abortedSignal, coverage: true, coverageIncludeGlobs: [''] });
      run({ files: [], signal: abortedSignal, coverage: true, coverageIncludeGlobs: '' });
    });

    await it('should only allow an int within range in options.lineCoverage', async () => {
      [Symbol(), {}, () => {}, [], 0n, 1n, Promise.resolve([]), true, false]
        .forEach((lineCoverage) => {
          assert.throws(() => run({ coverage: true, lineCoverage }), {
            code: 'ERR_INVALID_ARG_TYPE'
          });
          assert.throws(() => run({ coverage: true, lineCoverage: [lineCoverage] }), {
            code: 'ERR_INVALID_ARG_TYPE'
          });
        });
      assert.throws(() => run({ coverage: true, lineCoverage: -1 }), { code: 'ERR_OUT_OF_RANGE' });
      assert.throws(() => run({ coverage: true, lineCoverage: 101 }), { code: 'ERR_OUT_OF_RANGE' });

      run({ files: [], signal: abortedSignal, coverage: true, lineCoverage: 0 });
    });

    await it('should only allow an int within range in options.branchCoverage', async () => {
      [Symbol(), {}, () => {}, [], 0n, 1n, Promise.resolve([]), true, false]
        .forEach((branchCoverage) => {
          assert.throws(() => run({ coverage: true, branchCoverage }), {
            code: 'ERR_INVALID_ARG_TYPE'
          });
          assert.throws(() => run({ coverage: true, branchCoverage: [branchCoverage] }), {
            code: 'ERR_INVALID_ARG_TYPE'
          });
        });

      assert.throws(() => run({ coverage: true, branchCoverage: -1 }), { code: 'ERR_OUT_OF_RANGE' });
      assert.throws(() => run({ coverage: true, branchCoverage: 101 }), { code: 'ERR_OUT_OF_RANGE' });

      run({ files: [], signal: abortedSignal, coverage: true, branchCoverage: 0 });
    });

    await it('should only allow an int within range in options.functionCoverage', async () => {
      [Symbol(), {}, () => {}, [], 0n, 1n, Promise.resolve([]), true, false]
        .forEach((functionCoverage) => {
          assert.throws(() => run({ coverage: true, functionCoverage }), {
            code: 'ERR_INVALID_ARG_TYPE'
          });
          assert.throws(() => run({ coverage: true, functionCoverage: [functionCoverage] }), {
            code: 'ERR_INVALID_ARG_TYPE'
          });
        });

      assert.throws(() => run({ coverage: true, functionCoverage: -1 }), { code: 'ERR_OUT_OF_RANGE' });
      assert.throws(() => run({ coverage: true, functionCoverage: 101 }), { code: 'ERR_OUT_OF_RANGE' });

      run({ files: [], signal: abortedSignal, coverage: true, functionCoverage: 0 });
    });
  });

  const options = { concurrency: false, skip: !process.features.inspector ? 'inspector disabled' : false };
  await describe('run with coverage', options, async () => {
    await it('should run with coverage', async () => {
      const stream = run({ files, coverage: true });
      stream.on('test:fail', common.mustNotCall());
      stream.on('test:pass', common.mustCall());
      stream.on('test:coverage', common.mustCall());
      // eslint-disable-next-line no-unused-vars
      for await (const _ of stream);
    });

    await it('should run with coverage and exclude by glob', async () => {
      const stream = run({ files, coverage: true, coverageExcludeGlobs: ['test/*/test-runner/invalid-tap.js'] });
      stream.on('test:fail', common.mustNotCall());
      stream.on('test:pass', common.mustCall(1));
      stream.on('test:coverage', common.mustCall(({ summary: { files } }) => {
        const filesPaths = files.map(({ path }) => path);
        assert.strictEqual(filesPaths.some((path) => path.includes(`test-runner${sep}invalid-tap.js`)), false);
      }));
      // eslint-disable-next-line no-unused-vars
      for await (const _ of stream);
    });

    await it('should run with coverage and include by glob', async () => {
      const stream = run({
        files,
        coverage: true,
        coverageIncludeGlobs: ['test/fixtures/test-runner/coverage.js', 'test/*/v8-coverage/throw.js'],
      });
      stream.on('test:fail', common.mustNotCall());
      stream.on('test:pass', common.mustCall(1));
      stream.on('test:coverage', common.mustCall(({ summary: { files } }) => {
        const filesPaths = files.map(({ path }) => path);
        assert.strictEqual(filesPaths.some((path) => path.includes(`v8-coverage${sep}throw.js`)), true);
      }));
      // eslint-disable-next-line no-unused-vars
      for await (const _ of stream);
    });

    await it('should run while including and excluding globs', async () => {
      const stream = run({
        files: [...files, fixtures.path('test-runner/invalid-tap.js')],
        coverage: true,
        coverageIncludeGlobs: ['test/fixtures/test-runner/*.js'],
        coverageExcludeGlobs: ['test/fixtures/test-runner/*-tap.js']
      });
      stream.on('test:fail', common.mustNotCall());
      stream.on('test:pass', common.mustCall(2));
      stream.on('test:coverage', common.mustCall(({ summary: { files } }) => {
        const filesPaths = files.map(({ path }) => path);
        assert.strictEqual(filesPaths.every((path) => !path.includes(`test-runner${sep}invalid-tap.js`)), true);
        assert.strictEqual(filesPaths.some((path) => path.includes(`test-runner${sep}coverage.js`)), true);
      }));
      // eslint-disable-next-line no-unused-vars
      for await (const _ of stream);
    });

    await it('should run with coverage and fail when below line threshold', async () => {
      const thresholdErrors = [];
      const originalExitCode = process.exitCode;
      assert.notStrictEqual(originalExitCode, 1);
      const stream = run({ files, coverage: true, lineCoverage: 99, branchCoverage: 99, functionCoverage: 99 });
      stream.on('test:fail', common.mustNotCall());
      stream.on('test:pass', common.mustCall(1));
      stream.on('test:diagnostic', ({ message }) => {
        const match = message.match(/Error: \d{2}\.\d{2}% (line|branch|function) coverage does not meet threshold of 99%/);
        if (match) {
          thresholdErrors.push(match[1]);
        }
      });
      // eslint-disable-next-line no-unused-vars
      for await (const _ of stream);
      assert.deepStrictEqual(thresholdErrors.sort(), ['branch', 'function', 'line']);
      assert.strictEqual(process.exitCode, 1);
      process.exitCode = originalExitCode;
    });
  });
});


// exitHandler doesn't run until after the tests / after hooks finish.
process.on('exit', () => {
  assert.strictEqual(process.listeners('uncaughtException').length, 0);
  assert.strictEqual(process.listeners('unhandledRejection').length, 0);
  assert.strictEqual(process.listeners('beforeExit').length, 0);
  assert.strictEqual(process.listeners('SIGINT').length, 0);
  assert.strictEqual(process.listeners('SIGTERM').length, 0);
});
