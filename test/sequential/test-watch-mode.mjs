import * as common from '../common/index.mjs';
import tmpdir from '../common/tmpdir.js';
import assert from 'node:assert';
import path from 'node:path';
import { execPath } from 'node:process';
import { describe, it } from 'node:test';
import { spawn } from 'node:child_process';
import { writeFileSync, readFileSync, mkdirSync } from 'node:fs';
import { inspect } from 'node:util';
import { pathToFileURL } from 'node:url';
import { once } from 'node:events';
import { createInterface } from 'node:readline';

if (common.isIBMi)
  common.skip('IBMi does not support `fs.watch()`');

const supportsRecursive = common.isMacOS || common.isWindows;

function restart(file, content = readFileSync(file)) {
  // To avoid flakiness, we save the file repeatedly until test is done
  writeFileSync(file, content);
  const timer = setInterval(() => writeFileSync(file, content), common.platformTimeout(2500));
  return () => clearInterval(timer);
}

let tmpFiles = 0;
function createTmpFile(content = 'console.log("running");', ext = '.js', basename = tmpdir.path) {
  const file = path.join(basename, `${tmpFiles++}${ext}`);
  writeFileSync(file, content);
  return file;
}

function runInBackground({ args = [], options = {}, completed = 'Completed running', shouldFail = false }) {
  let future = Promise.withResolvers();
  let child;
  let stderr = '';
  let stdout = [];

  const run = () => {
    args.unshift('--no-warnings');
    child = spawn(execPath, args, { encoding: 'utf8', stdio: 'pipe', ...options });

    child.stderr.on('data', (data) => {
      stderr += data;
    });

    const rl = createInterface({ input: child.stdout });
    rl.on('line', (data) => {
      if (!data.startsWith('Waiting for graceful termination') && !data.startsWith('Gracefully restarted')) {
        stdout.push(data);
        if (data.startsWith(completed)) {
          future.resolve({ stderr, stdout });
          future = Promise.withResolvers();
          stdout = [];
          stderr = '';
        } else if (data.startsWith('Failed running')) {
          if (shouldFail) {
            future.resolve({ stderr, stdout });
          } else {
            future.reject({ stderr, stdout });
          }
          future = Promise.withResolvers();
          stdout = [];
          stderr = '';
        }
      }
    });
  };

  return {
    async done() {
      child?.kill();
      future.resolve();
      return { stdout, stderr };
    },
    restart(timeout = 1000) {
      if (!child) {
        run();
      }
      const timer = setTimeout(() => {
        if (!future.resolved) {
          child.kill();
          future.reject(new Error('Timed out waiting for restart'));
        }
      }, timeout);
      return future.promise.finally(() => {
        clearTimeout(timer);
      });
    }
  };
}

async function runWriteSucceed({
  file,
  watchedFile,
  watchFlag = '--watch',
  args = [file],
  completed = 'Completed running',
  restarts = 2,
  options = {},
  shouldFail = false
}) {
  args.unshift('--no-warnings');
  if (watchFlag !== null) args.unshift(watchFlag);
  const child = spawn(execPath, args, { encoding: 'utf8', stdio: 'pipe', ...options });
  let completes = 0;
  let cancelRestarts = () => {};
  let stderr = '';
  const stdout = [];

  child.stderr.on('data', (data) => {
    stderr += data;
  });

  try {
    // Break the chunks into lines
    for await (const data of createInterface({ input: child.stdout })) {
      if (!data.startsWith('Waiting for graceful termination') && !data.startsWith('Gracefully restarted')) {
        stdout.push(data);
      }
      if (data.startsWith(completed)) {
        completes++;
        if (completes === restarts) {
          break;
        }
        if (completes === 1) {
          cancelRestarts = restart(watchedFile);
        }
      }

      if (!shouldFail && data.startsWith('Failed running')) {
        break;
      }
    }
  } finally {
    child.kill();
    cancelRestarts();
  }
  return { stdout, stderr, pid: child.pid };
}

async function failWriteSucceed({ file, watchedFile }) {
  const child = spawn(execPath, ['--watch', '--no-warnings', file], { encoding: 'utf8', stdio: 'pipe' });
  let cancelRestarts = () => {};

  try {
    // Break the chunks into lines
    for await (const data of createInterface({ input: child.stdout })) {
      if (data.startsWith('Completed running')) {
        break;
      }
      if (data.startsWith('Failed running')) {
        cancelRestarts = restart(watchedFile, 'console.log("test has ran");');
      }
    }
  } finally {
    child.kill();
    cancelRestarts();
  }
}

tmpdir.refresh();

describe('watch mode', { concurrency: !process.env.TEST_PARALLEL, timeout: 60_000 }, () => {
  it('should watch changes to a file', async () => {
    const file = createTmpFile();
    const { stderr, stdout } = await runWriteSucceed({ file, watchedFile: file, watchFlag: '--watch=true', options: {
      timeout: 10000
    } });

    assert.strictEqual(stderr, '');
    assert.deepStrictEqual(stdout, [
      'running',
      `Completed running ${inspect(file)}`,
      `Restarting ${inspect(file)}`,
      'running',
      `Completed running ${inspect(file)}`,
    ]);
  });

  it('should watch changes to a file - event loop ended', async () => {
    const file = createTmpFile();
    const { stderr, stdout } = await runWriteSucceed({ file, watchedFile: file });

    assert.strictEqual(stderr, '');
    assert.deepStrictEqual(stdout, [
      'running',
      `Completed running ${inspect(file)}`,
      `Restarting ${inspect(file)}`,
      'running',
      `Completed running ${inspect(file)}`,
    ]);
  });

  it('should reload env variables when --env-file changes', async () => {
    const envKey = `TEST_ENV_${Date.now()}`;
    const jsFile = createTmpFile(`console.log('ENV: ' + process.env.${envKey});`);
    const envFile = createTmpFile(`${envKey}=value1`, '.env');
    const { done, restart } = runInBackground({ args: ['--watch', `--env-file=${envFile}`, jsFile] });

    try {
      await restart();
      writeFileSync(envFile, `${envKey}=value2`);

      // Second restart, after env change
      const { stdout, stderr } = await restart();

      assert.strictEqual(stderr, '');
      assert.deepStrictEqual(stdout, [
        `Restarting ${inspect(jsFile)}`,
        'ENV: value2',
        `Completed running ${inspect(jsFile)}`,
      ]);
    } finally {
      await done();
    }
  });

  it('should load new env variables when --env-file changes', async () => {
    const envKey = `TEST_ENV_${Date.now()}`;
    const envKey2 = `TEST_ENV_2_${Date.now()}`;
    const jsFile = createTmpFile(`console.log('ENV: ' + process.env.${envKey} + '\\n' + 'ENV2: ' + process.env.${envKey2});`);
    const envFile = createTmpFile(`${envKey}=value1`, '.env');
    const { done, restart } = runInBackground({ args: ['--watch', `--env-file=${envFile}`, jsFile] });

    try {
      await restart();
      writeFileSync(envFile, `${envKey}=value1\n${envKey2}=newValue`);

      // Second restart, after env change
      const { stderr, stdout } = await restart();

      assert.strictEqual(stderr, '');
      assert.deepStrictEqual(stdout, [
        `Restarting ${inspect(jsFile)}`,
        'ENV: value1',
        'ENV2: newValue',
        `Completed running ${inspect(jsFile)}`,
      ]);
    } finally {
      await done();
    }
  });

  it('should watch changes to a failing file', async () => {
    const file = createTmpFile('throw new Error("fails");');
    const { stderr, stdout } = await runWriteSucceed({
      file,
      watchedFile: file,
      completed: 'Failed running',
      shouldFail: true
    });

    assert.match(stderr, /Error: fails\r?\n/);
    assert.deepStrictEqual(stdout, [
      `Failed running ${inspect(file)}`,
      `Restarting ${inspect(file)}`,
      `Failed running ${inspect(file)}`,
    ]);
  });

  it('should watch changes to a file with watch-path', {
    skip: !supportsRecursive,
  }, async () => {
    const dir = tmpdir.resolve('subdir1');
    mkdirSync(dir);
    const file = createTmpFile();
    const watchedFile = createTmpFile('', '.js', dir);
    const args = ['--watch-path', dir, file];
    const { stderr, stdout } = await runWriteSucceed({ file, watchedFile, args });

    assert.strictEqual(stderr, '');
    assert.deepStrictEqual(stdout, [
      'running',
      `Completed running ${inspect(file)}`,
      `Restarting ${inspect(file)}`,
      'running',
      `Completed running ${inspect(file)}`,
    ]);
    assert.strictEqual(stderr, '');
  });

  it('should watch when running an non-existing file - when specified under --watch-path', {
    skip: !supportsRecursive
  }, async () => {
    const dir = tmpdir.resolve('subdir2');
    mkdirSync(dir);
    const file = path.join(dir, 'non-existing.js');
    const watchedFile = createTmpFile('', '.js', dir);
    const args = ['--watch-path', dir, file];
    const { stderr, stdout } = await runWriteSucceed({
      file,
      watchedFile,
      args,
      completed: 'Failed running',
      shouldFail: true
    });

    assert.match(stderr, /Error: Cannot find module/g);
    assert.deepStrictEqual(stdout, [
      `Failed running ${inspect(file)}`,
      `Restarting ${inspect(file)}`,
      `Failed running ${inspect(file)}`,
    ]);
  });

  it('should watch when running an non-existing file - when specified under --watch-path with equals', {
    skip: !supportsRecursive
  }, async () => {
    const dir = tmpdir.resolve('subdir3');
    mkdirSync(dir);
    const file = path.join(dir, 'non-existing.js');
    const watchedFile = createTmpFile('', '.js', dir);
    const args = [`--watch-path=${dir}`, file];
    const { stderr, stdout } = await runWriteSucceed({
      file,
      watchedFile,
      args,
      completed: 'Failed running',
      shouldFail: true
    });

    assert.match(stderr, /Error: Cannot find module/g);
    assert.deepStrictEqual(stdout, [
      `Failed running ${inspect(file)}`,
      `Restarting ${inspect(file)}`,
      `Failed running ${inspect(file)}`,
    ]);
  });

  it('should watch changes to a file - event loop blocked', { timeout: 10_000 }, async () => {
    const file = createTmpFile(`
console.log("running");
Atomics.wait(new Int32Array(new SharedArrayBuffer(4)), 0, 0);
console.log("don't show me");`);
    const { stderr, stdout } = await runWriteSucceed({ file, watchedFile: file, completed: 'running' });

    assert.strictEqual(stderr, '');
    assert.deepStrictEqual(stdout, [
      'running',
      `Restarting ${inspect(file)}`,
      'running',
    ]);
  });

  it('should watch changes to dependencies - cjs', async () => {
    const dependency = createTmpFile('module.exports = {};');
    const file = createTmpFile(`
const dependency = require(${JSON.stringify(dependency)});
console.log(dependency);
`);
    const { stderr, stdout } = await runWriteSucceed({ file, watchedFile: dependency });

    assert.strictEqual(stderr, '');
    assert.deepStrictEqual(stdout, [
      '{}',
      `Completed running ${inspect(file)}`,
      `Restarting ${inspect(file)}`,
      '{}',
      `Completed running ${inspect(file)}`,
    ]);
  });

  it('should watch changes to dependencies - esm', async () => {
    const dependency = createTmpFile('module.exports = {};');
    const file = createTmpFile(`
import dependency from ${JSON.stringify(pathToFileURL(dependency))};
console.log(dependency);
`, '.mjs');
    const { stderr, stdout } = await runWriteSucceed({ file, watchedFile: dependency });

    assert.strictEqual(stderr, '');
    assert.deepStrictEqual(stdout, [
      '{}',
      `Completed running ${inspect(file)}`,
      `Restarting ${inspect(file)}`,
      '{}',
      `Completed running ${inspect(file)}`,
    ]);
  });

  it('should restart multiple times', async () => {
    const file = createTmpFile();
    const { stderr, stdout } = await runWriteSucceed({ file, watchedFile: file, restarts: 3 });

    assert.strictEqual(stderr, '');
    assert.deepStrictEqual(stdout, [
      'running',
      `Completed running ${inspect(file)}`,
      `Restarting ${inspect(file)}`,
      'running',
      `Completed running ${inspect(file)}`,
      `Restarting ${inspect(file)}`,
      'running',
      `Completed running ${inspect(file)}`,
    ]);
  });

  it('should pass arguments to file', async () => {
    const file = createTmpFile(`
const { parseArgs } = require('node:util');
const { values } = parseArgs({ options: { random: { type: 'string' } } });
console.log(values.random);
    `);
    const random = Date.now().toString();
    const args = [file, '--random', random];
    const { stderr, stdout } = await runWriteSucceed({ file, watchedFile: file, args });

    assert.strictEqual(stderr, '');
    assert.deepStrictEqual(stdout, [
      random,
      `Completed running ${inspect(`${file} --random ${random}`)}`,
      `Restarting ${inspect(`${file} --random ${random}`)}`,
      random,
      `Completed running ${inspect(`${file} --random ${random}`)}`,
    ]);
  });

  it('should load --require modules in the watched process, and not in the orchestrator process', async () => {
    const file = createTmpFile();
    const required = createTmpFile('process._rawDebug(\'pid\', process.pid);');
    const args = ['--require', required, file];
    const { stdout, pid, stderr } = await runWriteSucceed({ file, watchedFile: file, args });

    const importPid = parseInt(stderr[0].split(' ')[1], 10);
    assert.notStrictEqual(pid, importPid);
    assert.deepStrictEqual(stdout, [
      'running',
      `Completed running ${inspect(file)}`,
      `Restarting ${inspect(file)}`,
      'running',
      `Completed running ${inspect(file)}`,
    ]);
  });

  it('should load --import modules in the watched process, and not in the orchestrator process', async () => {
    const file = createTmpFile();
    const imported = "data:text/javascript,process._rawDebug('pid', process.pid);";
    const args = ['--import', imported, file];
    const { stdout, pid, stderr } = await runWriteSucceed({ file, watchedFile: file, args });

    const importPid = parseInt(stderr.split('\n', 1)[0].split(' ', 2)[1], 10);

    assert.notStrictEqual(importPid, NaN);
    assert.notStrictEqual(pid, importPid);

    assert.deepStrictEqual(stdout, [
      'running',
      `Completed running ${inspect(file)}`,
      `Restarting ${inspect(file)}`,
      'running',
      `Completed running ${inspect(file)}`,
    ]);
  });

  // TODO: Remove skip after https://github.com/nodejs/node/pull/45271 lands
  it('should not watch when running an missing file', {
    skip: !supportsRecursive
  }, async () => {
    const nonExistingfile = tmpdir.resolve(`${tmpFiles++}.js`);
    await failWriteSucceed({ file: nonExistingfile, watchedFile: nonExistingfile });
  });

  it('should not watch when running an missing mjs file', {
    skip: !supportsRecursive
  }, async () => {
    const nonExistingfile = tmpdir.resolve(`${tmpFiles++}.mjs`);
    await failWriteSucceed({ file: nonExistingfile, watchedFile: nonExistingfile });
  });

  it('should watch changes to previously missing dependency', {
    skip: !supportsRecursive
  }, async () => {
    const dependency = tmpdir.resolve(`${tmpFiles++}.js`);
    const relativeDependencyPath = `./${path.basename(dependency)}`;
    const dependant = createTmpFile(`console.log(require('${relativeDependencyPath}'))`);

    await failWriteSucceed({ file: dependant, watchedFile: dependency });
  });

  it('should watch changes to previously missing ESM dependency', {
    skip: !supportsRecursive
  }, async () => {
    const relativeDependencyPath = `./${tmpFiles++}.mjs`;
    const dependency = tmpdir.resolve(relativeDependencyPath);
    const dependant = createTmpFile(`import ${JSON.stringify(relativeDependencyPath)}`, '.mjs');

    await failWriteSucceed({ file: dependant, watchedFile: dependency });
  });

  it('should clear output between runs', async () => {
    const file = createTmpFile();
    const { stderr, stdout } = await runWriteSucceed({ file, watchedFile: file });

    assert.strictEqual(stderr, '');
    assert.deepStrictEqual(stdout, [
      'running',
      `Completed running ${inspect(file)}`,
      `Restarting ${inspect(file)}`,
      'running',
      `Completed running ${inspect(file)}`,
    ]);
  });

  it('should preserve output when --watch-preserve-output flag is passed', async () => {
    const file = createTmpFile();
    const args = ['--watch-preserve-output', file];
    const { stderr, stdout } = await runWriteSucceed({ file, watchedFile: file, args });

    assert.strictEqual(stderr, '');
    assert.deepStrictEqual(stdout, [
      'running',
      `Completed running ${inspect(file)}`,
      `Restarting ${inspect(file)}`,
      'running',
      `Completed running ${inspect(file)}`,
    ]);
  });

  it('should run when `--watch-path=./foo --require ./bar.js`', {
    skip: !supportsRecursive,
  }, async () => {
    const projectDir = tmpdir.resolve('project2');
    mkdirSync(projectDir);

    const dir = path.join(projectDir, 'watched-dir');
    mkdirSync(dir);

    writeFileSync(path.join(projectDir, 'some.js'), 'console.log(\'hello\')');

    const file = createTmpFile('console.log(\'running\');', '.js', projectDir);
    const watchedFile = createTmpFile('', '.js', dir);
    const args = [`--watch-path=${dir}`, '--require', './some.js', file];
    const { stdout, stderr } = await runWriteSucceed({
      file, watchedFile, args, options: {
        cwd: projectDir
      }
    });

    assert.strictEqual(stderr, '');
    assert.deepStrictEqual(stdout, [
      'hello',
      'running',
      `Completed running ${inspect(file)}`,
      `Restarting ${inspect(file)}`,
      'hello',
      'running',
      `Completed running ${inspect(file)}`,
    ]);
  });

  it('should run when `--watch-path=./foo --require=./bar.js`', {
    skip: !supportsRecursive,
  }, async () => {
    const projectDir = tmpdir.resolve('project3');
    mkdirSync(projectDir);

    const dir = path.join(projectDir, 'watched-dir');
    mkdirSync(dir);

    writeFileSync(path.join(projectDir, 'some.js'), "console.log('hello')");

    const file = createTmpFile("console.log('running');", '.js', projectDir);
    const watchedFile = createTmpFile('', '.js', dir);
    const args = [`--watch-path=${dir}`, '--require=./some.js', file];
    const { stdout, stderr } = await runWriteSucceed({
      file, watchedFile, args, options: {
        cwd: projectDir
      }
    });

    assert.strictEqual(stderr, '');
    assert.deepStrictEqual(stdout, [
      'hello',
      'running',
      `Completed running ${inspect(file)}`,
      `Restarting ${inspect(file)}`,
      'hello',
      'running',
      `Completed running ${inspect(file)}`,
    ]);
  });

  it('should run when `--watch-path ./foo --require ./bar.js`', {
    skip: !supportsRecursive,
  }, async () => {
    const projectDir = tmpdir.resolve('project5');
    mkdirSync(projectDir);

    const dir = path.join(projectDir, 'watched-dir');
    mkdirSync(dir);

    writeFileSync(path.join(projectDir, 'some.js'), 'console.log(\'hello\')');

    const file = createTmpFile('console.log(\'running\');', '.js', projectDir);
    const watchedFile = createTmpFile('', '.js', dir);
    const args = ['--watch-path', `${dir}`, '--require', './some.js', file];
    const { stdout, stderr } = await runWriteSucceed({
      file, watchedFile, args, options: {
        cwd: projectDir
      }
    });

    assert.strictEqual(stderr, '');
    assert.deepStrictEqual(stdout, [
      'hello',
      'running',
      `Completed running ${inspect(file)}`,
      `Restarting ${inspect(file)}`,
      'hello',
      'running',
      `Completed running ${inspect(file)}`,
    ]);
  });

  it('should run when `--watch-path=./foo --require=./bar.js`', {
    skip: !supportsRecursive,
  }, async () => {
    const projectDir = tmpdir.resolve('project6');
    mkdirSync(projectDir);

    const dir = path.join(projectDir, 'watched-dir');
    mkdirSync(dir);

    writeFileSync(path.join(projectDir, 'some.js'), "console.log('hello')");

    const file = createTmpFile("console.log('running');", '.js', projectDir);
    const watchedFile = createTmpFile('', '.js', dir);
    const args = ['--watch-path', `${dir}`, '--require=./some.js', file];
    const { stdout, stderr } = await runWriteSucceed({
      file, watchedFile, args, options: {
        cwd: projectDir
      }
    });

    assert.strictEqual(stderr, '');
    assert.deepStrictEqual(stdout, [
      'hello',
      'running',
      `Completed running ${inspect(file)}`,
      `Restarting ${inspect(file)}`,
      'hello',
      'running',
      `Completed running ${inspect(file)}`,
    ]);
  });

  it('should run when `--watch --inspect`', async () => {
    const file = createTmpFile();
    const args = ['--watch', '--inspect', file];
    const { stdout, stderr } = await runWriteSucceed({ file, watchedFile: file, watchFlag: null, args });

    assert.match(stderr, /listening on ws:\/\//);
    assert.deepStrictEqual(stdout, [
      'running',
      `Completed running ${inspect(file)}`,
      `Restarting ${inspect(file)}`,
      'running',
      `Completed running ${inspect(file)}`,
    ]);
  });

  it('should run when `--watch -r ./foo.js`', async () => {
    const projectDir = tmpdir.resolve('project7');
    mkdirSync(projectDir);

    const dir = path.join(projectDir, 'watched-dir');
    mkdirSync(dir);
    writeFileSync(path.join(projectDir, 'some.js'), "console.log('hello')");

    const file = createTmpFile("console.log('running');", '.js', projectDir);
    const args = ['--watch', '-r', './some.js', file];
    const { stdout, stderr } = await runWriteSucceed({
      file, watchedFile: file, watchFlag: null, args, options: { cwd: projectDir }
    });

    assert.strictEqual(stderr, '');
    assert.deepStrictEqual(stdout, [
      'hello',
      'running',
      `Completed running ${inspect(file)}`,
      `Restarting ${inspect(file)}`,
      'hello',
      'running',
      `Completed running ${inspect(file)}`,
    ]);
  });

  it('should pass IPC messages from a spawning parent to the child and back', async () => {
    const file = createTmpFile(`console.log('running');
process.on('message', (message) => {
  if (message === 'exit') {
    process.exit(0);
  } else {
    console.log('Received:', message);
    process.send(message);
  }
})`);

    const child = spawn(
      execPath,
      [
        '--watch',
        '--no-warnings',
        file,
      ],
      {
        encoding: 'utf8',
        stdio: ['pipe', 'pipe', 'pipe', 'ipc'],
      },
    );

    let stderr = '';
    let stdout = '';

    child.stdout.on('data', (data) => stdout += data);
    child.stderr.on('data', (data) => stderr += data);
    async function waitForEcho(msg) {
      const receivedPromise = new Promise((resolve) => {
        const fn = (message) => {
          if (message === msg) {
            child.off('message', fn);
            resolve();
          }
        };
        child.on('message', fn);
      });
      child.send(msg);
      await receivedPromise;
    }

    async function waitForText(text) {
      const seenPromise = new Promise((resolve) => {
        const fn = (data) => {
          if (data.toString().includes(text)) {
            resolve();
            child.stdout.off('data', fn);
          }
        };
        child.stdout.on('data', fn);
      });
      await seenPromise;
    }

    await waitForText('running');
    await waitForEcho('first message');
    const stopRestarts = restart(file);
    await waitForText('running');
    stopRestarts();
    await waitForEcho('second message');
    const exitedPromise = once(child, 'exit');
    child.send('exit');
    await waitForText('Completed');
    child.disconnect();
    child.kill();
    await exitedPromise;
    assert.strictEqual(stderr, '');
    const lines = stdout.split(/\r?\n/).filter(Boolean);
    assert.deepStrictEqual(lines, [
      'running',
      'Received: first message',
      `Restarting ${inspect(file)}`,
      'running',
      'Received: second message',
      `Completed running ${inspect(file)}`,
    ]);
  });
});
