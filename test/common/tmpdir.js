'use strict';

const { spawnSync } = require('child_process');
const fs = require('fs');
const path = require('path');
const { pathToFileURL } = require('url');
const { isMainThread } = require('worker_threads');
const isUnix = process.platform === 'darwin' || process.platform === 'linux';

function rmSync(pathname, useSpawn) {
  if (useSpawn) {
    if (isUnix) {
      for (let i = 0; i < 3; i++) {
        const { status } = spawnSync(`rm -rf ${pathname}`);
        if (status === 0) {
          break;
        }
      }
    } else {
      const escapedPath = pathname.replaceAll('\\', '\\\\');
      spawnSync(
        process.execPath,
        [
          '-e',
          `require("fs").rmSync("${escapedPath}", { maxRetries: 3, recursive: true, force: true });`,
        ],
      );
    }
  } else {
    fs.rmSync(pathname, { maxRetries: 3, recursive: true, force: true });
  }
}

const testRoot = process.env.NODE_TEST_DIR ?
  fs.realpathSync(process.env.NODE_TEST_DIR) : path.resolve(__dirname, '..');

// Using a `.` prefixed name, which is the convention for "hidden" on POSIX,
// gets tools to ignore it by default or by simple rules, especially eslint.
const tmpdirName = '.tmp.' +
  (process.env.TEST_SERIAL_ID || process.env.TEST_THREAD_ID || '0');
const tmpPath = path.join(testRoot, tmpdirName);

let firstRefresh = true;
function refresh(useSpawn = false) {
  rmSync(tmpPath, useSpawn);
  fs.mkdirSync(tmpPath);

  if (firstRefresh) {
    firstRefresh = false;
    // Clean only when a test uses refresh. This allows for child processes to
    // use the tmpdir and only the parent will clean on exit.
    process.on('exit', () => {
      return onexit(useSpawn);
    });
  }
}

function onexit(useSpawn) {
  // Change directory to avoid possible EBUSY
  if (isMainThread)
    process.chdir(testRoot);

  try {
    rmSync(tmpPath, useSpawn);
  } catch (e) {
    console.error('Can\'t clean tmpdir:', tmpPath);

    const files = fs.readdirSync(tmpPath);
    console.error('Files blocking:', files);

    if (files.some((f) => f.startsWith('.nfs'))) {
      // Warn about NFS "silly rename"
      console.error('Note: ".nfs*" might be files that were open and ' +
                    'unlinked but not closed.');
      console.error('See http://nfs.sourceforge.net/#faq_d2 for details.');
    }

    console.error();
    throw e;
  }
}

function resolve(...paths) {
  return path.resolve(tmpPath, ...paths);
}

function hasEnoughSpace(size) {
  const { bavail, bsize } = fs.statfsSync(tmpPath);
  return bavail >= Math.ceil(size / bsize);
}

function fileURL(...paths) {
  // When called without arguments, add explicit trailing slash
  const fullPath = path.resolve(tmpPath + path.sep, ...paths);

  return pathToFileURL(fullPath);
}

module.exports = {
  fileURL,
  hasEnoughSpace,
  path: tmpPath,
  refresh,
  resolve,
};
