import { spawnPromisified } from '../common/index.mjs';
import assert from 'node:assert';
import { execPath } from 'node:process';
import { describe, it } from 'node:test';


describe('--print with a promise', { concurrency: !process.env.TEST_PARALLEL }, () => {
  it('should handle directly-fulfilled promises', async () => {
    const result = await spawnPromisified(execPath, [
      '--print',
      'Promise.resolve(42)',
    ]);

    assert.deepStrictEqual(result, {
      code: 0,
      signal: null,
      stderr: '',
      stdout: 'Promise { 42 }\n',
    });
  });

  it('should handle promises fulfilled after one tick', async () => {
    const result = await spawnPromisified(execPath, [
      '--print',
      'Promise.resolve().then(()=>42)',
    ]);

    assert.deepStrictEqual(result, {
      code: 0,
      signal: null,
      stderr: '',
      stdout: 'Promise { 42 }\n',
    });
  });

  it('should handle promise that never settles', async () => {
    const result = await spawnPromisified(execPath, [
      '--print',
      'new Promise(()=>{})',
    ]);

    assert.deepStrictEqual(result, {
      code: 0,
      signal: null,
      stderr: '',
      stdout: 'Promise { <pending> }\n',
    });
  });

  it('should output something if process exits before promise settles', async () => {
    const result = await spawnPromisified(execPath, [
      '--print',
      'setTimeout(process.exit, 100, 0);timers.promises.setTimeout(200)',
    ]);

    assert.deepStrictEqual(result, {
      code: 0,
      signal: null,
      stderr: '',
      stdout: 'Promise { <pending> }\n',
    });
  });

  it('should respect exit code when process exits before promise settles', async () => {
    const result = await spawnPromisified(execPath, [
      '--print',
      'setTimeout(process.exit, 100, 42);timers.promises.setTimeout(200)',
    ]);

    assert.deepStrictEqual(result, {
      code: 42,
      signal: null,
      stderr: '',
      stdout: 'Promise { <pending> }\n',
    });
  });

  it('should handle rejected promises', async () => {
    const result = await spawnPromisified(execPath, [
      '--unhandled-rejections=none',
      '--print',
      'Promise.reject(1)',
    ]);

    assert.deepStrictEqual(result, {
      code: 0,
      signal: null,
      stderr: '',
      stdout: 'Promise { <rejected> 1 }\n',
    });
  });

  it('should handle promises that reject after one tick', async () => {
    const result = await spawnPromisified(execPath, [
      '--unhandled-rejections=none',
      '--print',
      'Promise.resolve().then(()=>Promise.reject(1))',
    ]);

    assert.deepStrictEqual(result, {
      code: 0,
      signal: null,
      stderr: '',
      stdout: 'Promise { <rejected> 1 }\n',
    });
  });

  it('should handle thenable that resolves', async () => {
    const result = await spawnPromisified(execPath, [
      '--unhandled-rejections=none',
      '--print',
      '({ then(r) { r(42) } })',
    ]);

    assert.deepStrictEqual(result, {
      code: 0,
      signal: null,
      stderr: '',
      stdout: '{ then: [Function: then] }\n',
    });
  });

  it('should handle thenable that rejects', async () => {
    const result = await spawnPromisified(execPath, [
      '--unhandled-rejections=none',
      '--print',
      '({ then(_, r) { r(42) } })',
    ]);

    assert.deepStrictEqual(result, {
      code: 0,
      signal: null,
      stderr: '',
      stdout: '{ then: [Function: then] }\n',
    });
  });

  it('should handle Promise.prototype', async () => {
    const result = await spawnPromisified(execPath, [
      '--unhandled-rejections=none',
      '--print',
      'Promise.prototype',
    ]);

    assert.deepStrictEqual(result, {
      code: 0,
      signal: null,
      stderr: '',
      stdout: 'Object [Promise] {}\n',
    });
  });
});
