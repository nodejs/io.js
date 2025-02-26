'use strict';
const common = require('.');
const path = require('node:path');
const test = require('node:test');
const fs = require('node:fs/promises');
const assert = require('node:assert/strict');

const stackFramesRegexp = /(?<=\n)(\s+)((.+?)\s+\()?(?:\(?(.+?):(\d+)(?::(\d+))?)\)?(\s+\{)?(\[\d+m)?(\n|$)/g;
const windowNewlineRegexp = /\r/g;

function replaceExperimentalWarning(str) {
  return str.replace(/\(node:\d+\) ExperimentalWarning: (.*)\n/g, '')
    .replace('(Use `node --trace-warnings ...` to show where the warning was created)\n', '');
}

function replaceNodeVersion(str) {
  return str.replaceAll(process.version, '*');
}

function replaceStackTrace(str, replacement = '$1*$7$8\n') {
  return str.replace(stackFramesRegexp, replacement);
}

function replaceInternalStackTrace(str) {
  return str.replaceAll(/(\W+).*node:internal.*/g, '$1*');
}

function replaceWindowsLineEndings(str) {
  return str.replace(windowNewlineRegexp, '');
}

function replaceWindowsPaths(str) {
  return common.isWindows ? str.replaceAll(path.win32.sep, path.posix.sep) : str;
}

function replaceWindowsDriveLetter(str) {
  if (!common.isWindows) {
    return str;
  }
  const currentDriveLetter = path.parse(process.cwd()).root.substring(0, 1).toLowerCase();
  const regex = new RegExp(`${currentDriveLetter}:`, 'gi');
  return str.replaceAll('\\\'', "'").replaceAll(regex, '');
}

function transformCwd(replacement = '') {
  const cwd = process.cwd();
  return (str) => {
    return str.replaceAll('\\\'', "'").replaceAll(cwd, replacement);
  };
}

function transform(...args) {
  return (str) => args.reduce((acc, fn) => fn(acc), str);
}

function getSnapshotPath(filename) {
  const { name, dir } = path.parse(filename);
  return path.resolve(dir, `${name}.snapshot`);
}

async function assertSnapshot(actual, filename = process.argv[1]) {
  const snapshot = getSnapshotPath(filename);
  if (process.env.NODE_REGENERATE_SNAPSHOTS) {
    await fs.writeFile(snapshot, actual);
  } else {
    let expected;
    try {
      expected = await fs.readFile(snapshot, 'utf8');
    } catch (e) {
      if (e.code === 'ENOENT') {
        console.log(
          'Snapshot file does not exist. You can create a new one by running the test with NODE_REGENERATE_SNAPSHOTS=1',
        );
      }
      throw e;
    }
    assert.strictEqual(actual, replaceWindowsLineEndings(expected));
  }
}

/**
 * Spawn a process and assert its output against a snapshot.
 * if you want to automatically update the snapshot, run tests with NODE_REGENERATE_SNAPSHOTS=1
 * transform is a function that takes the output and returns a string that will be compared against the snapshot
 * this is useful for normalizing output such as stack traces
 * there are some predefined transforms in this file such as replaceStackTrace and replaceWindowsLineEndings
 * both of which can be used as an example for writing your own
 * compose multiple transforms by passing them as arguments to the transform function:
 * assertSnapshot.transform(assertSnapshot.replaceStackTrace, assertSnapshot.replaceWindowsLineEndings)
 * @param {string} filename
 * @param {function(string): string} [transform]
 * @param {object} [options] - control how the child process is spawned
 * @param {boolean} [options.tty] - whether to spawn the process in a pseudo-tty
 * @returns {Promise<void>}
 */
async function spawnAndAssert(filename, transform = (x) => x, { tty = false, ...options } = {}) {
  if (tty && common.isWindows) {
    test({ skip: 'Skipping pseudo-tty tests, as pseudo terminals are not available on Windows.' });
    return;
  }
  let flags = common.parseTestFlags(filename);
  if (options.flags) {
    flags = [...options.flags, ...flags];
  }

  const executable = tty ? (process.env.PYTHON || 'python3') : process.execPath;
  const args =
    tty ?
      [path.join(__dirname, '../..', 'tools/pseudo-tty.py'), process.execPath, ...flags, filename] :
      [...flags, filename];
  const { stdout, stderr } = await common.spawnPromisified(executable, args, options);
  await assertSnapshot(transform(`${stdout}${stderr}`), filename);
}

module.exports = {
  assertSnapshot,
  getSnapshotPath,
  replaceExperimentalWarning,
  replaceNodeVersion,
  replaceStackTrace,
  replaceInternalStackTrace,
  replaceWindowsLineEndings,
  replaceWindowsPaths,
  replaceWindowsDriveLetter,
  spawnAndAssert,
  transform,
  transformCwd,
};
