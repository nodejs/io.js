'use strict';

// This file is a modified version of the fs-extra's copySync method.

const fsBinding = internalBinding('fs');
const { isSrcSubdir } = require('internal/fs/cp/cp');
const { codes: {
  ERR_FS_CP_EEXIST,
  ERR_FS_CP_EINVAL,
  ERR_FS_CP_SYMLINK_TO_SUBDIRECTORY,
  ERR_INVALID_RETURN_VALUE,
} } = require('internal/errors');
const {
  os: {
    errno: {
      EEXIST,
      EINVAL,
    },
  },
} = internalBinding('constants');
const {
  chmodSync,
  copyFileSync,
  lstatSync,
  mkdirSync,
  opendirSync,
  readlinkSync,
  statSync,
  symlinkSync,
  unlinkSync,
  utimesSync,
} = require('fs');
const {
  dirname,
  isAbsolute,
  join,
  resolve,
} = require('path');
const { isPromise } = require('util/types');
let assert;

function cpSyncFn(src, dest, opts) {
  // Warn about using preserveTimestamps on 32-bit node
  if (opts.preserveTimestamps && process.arch === 'ia32') {
    const warning = 'Using the preserveTimestamps option in 32-bit ' +
      'node is not recommended';
    process.emitWarning(warning, 'TimestampPrecisionWarning');
  }
  if (opts.filter) {
    const shouldCopy = opts.filter(src, dest);
    if (isPromise(shouldCopy)) {
      throw new ERR_INVALID_RETURN_VALUE('boolean', 'filter', shouldCopy);
    }
    if (!shouldCopy) return;
  }

  // TODO: Performance optimization
  // Return a single value that holds multiple fields. Preferably using bitmap.
  // This would avoid constructing an array.
  const {
    0: destExists,
    1: shouldCopyDirectory,
    2: shouldCopyFile,
    3: shouldCopySymlink,
  } = fsBinding.cpSyncCheckPaths(src, dest, opts.dereference, opts.recursive);

  // We conditionally call statSyncFn because we don't need it in symlink path.
  const statSyncFn = opts.dereference ? statSync : lstatSync;

  if (shouldCopyDirectory) {
    // srcStat is only needed to get the mode of the file.
    // TODO: Return the mode from `cpSyncCheckPaths` to avoid stat call.
    const srcStat = statSyncFn(src);
    return onDir(srcStat, destExists, src, dest, opts);
  } else if (shouldCopyFile) {
    // srcStat is actually needed here.
    const srcStat = statSyncFn(src);
    return onFile(srcStat, destExists, src, dest, opts);
  } else if (shouldCopySymlink) {
    return onLink(destExists, src, dest, opts.verbatimSymlinks);
  }

  // It is not possible to get here because all possible cases are handled above.
  assert ??= require('internal/assert');
  assert.fail('Unreachable code');
}

function getStats(src, dest, opts) {
  // TODO(@anonrig): Avoid making two stat calls.
  const statSyncFn = opts.dereference ? statSync : lstatSync;
  const srcStat = statSyncFn(src);

  // Optimization opportunity:
  // TODO: destStat is only used for checking if `dest` path exists or not.
  // We don't need a whole Stats object here.
  const destStat = statSyncFn(dest, { bigint: true, throwIfNoEntry: false });

  if (srcStat.isDirectory() && opts.recursive) {
    return onDir(srcStat, destStat, src, dest, opts);
  } else if (srcStat.isFile() ||
           srcStat.isCharacterDevice() ||
           srcStat.isBlockDevice()) {
    return onFile(srcStat, destStat, src, dest, opts);
  } else if (srcStat.isSymbolicLink()) {
    return onLink(destStat, src, dest, opts.verbatimSymlinks);
  }

  // It is not possible to get here because all possible cases are handled above.
  assert ??= require('internal/assert');
  assert.fail('Unreachable code');
}

/**
 * @param {fs.Stats} srcStat
 * @param {boolean} destExists
 * @param {string} src
 * @param {string} dest
 * @param {Record<string, unknown>} opts
 */
function onFile(srcStat, destExists, src, dest, opts) {
  if (!destExists) return copyFile(srcStat, src, dest, opts);
  return mayCopyFile(srcStat, src, dest, opts);
}

// TODO(@anonrig): Move this function to C++.
function mayCopyFile(srcStat, src, dest, opts) {
  if (opts.force) {
    unlinkSync(dest);
    return copyFile(srcStat, src, dest, opts);
  } else if (opts.errorOnExist) {
    throw new ERR_FS_CP_EEXIST({
      message: `${dest} already exists`,
      path: dest,
      syscall: 'cp',
      errno: EEXIST,
      code: 'EEXIST',
    });
  }
}

function copyFile(srcStat, src, dest, opts) {
  copyFileSync(src, dest, opts.mode);
  if (opts.preserveTimestamps) handleTimestamps(srcStat.mode, src, dest);
  return setDestMode(dest, srcStat.mode);
}

function handleTimestamps(srcMode, src, dest) {
  // Make sure the file is writable before setting the timestamp
  // otherwise open fails with EPERM when invoked with 'r+'
  // (through utimes call)
  if (fileIsNotWritable(srcMode)) makeFileWritable(dest, srcMode);
  return setDestTimestamps(src, dest);
}

function fileIsNotWritable(srcMode) {
  return (srcMode & 0o200) === 0;
}

function makeFileWritable(dest, srcMode) {
  return setDestMode(dest, srcMode | 0o200);
}

function setDestMode(dest, srcMode) {
  return chmodSync(dest, srcMode);
}

function setDestTimestamps(src, dest) {
  // The initial srcStat.atime cannot be trusted
  // because it is modified by the read(2) system call
  // (See https://nodejs.org/api/fs.html#fs_stat_time_values)
  const updatedSrcStat = statSync(src);
  return utimesSync(dest, updatedSrcStat.atime, updatedSrcStat.mtime);
}

/**
 * @param {fs.Stats} srcStat
 * @param {boolean} destExists
 * @param {string} src
 * @param {string} dest
 * @param {Record<string, unknown>} opts
 */
function onDir(srcStat, destExists, src, dest, opts) {
  if (!destExists) {
    // TODO: Optimization opportunity
    // This can be moved to C++ and can significantly improve the performance.
    mkdirSync(dest);
    copyDir(src, dest, opts);
    return setDestMode(dest, srcStat.mode);
  }
  return copyDir(src, dest, opts);
}

// TODO(@anonrig): Move this function to C++.
function copyDir(src, dest, opts) {
  const dir = opendirSync(src);

  try {
    let dirent;

    while ((dirent = dir.readSync()) !== null) {
      const { name } = dirent;
      const srcItem = join(src, name);
      const destItem = join(dest, name);
      let shouldCopy = true;

      if (opts.filter) {
        shouldCopy = opts.filter(srcItem, destItem);
        if (isPromise(shouldCopy)) {
          throw new ERR_INVALID_RETURN_VALUE('boolean', 'filter', shouldCopy);
        }
      }

      if (shouldCopy) {
        getStats(srcItem, destItem, opts);
      }
    }
  } finally {
    dir.closeSync();
  }
}

/**
 * TODO(@anonrig): Move this function to C++.
 * @param {boolean} destExists
 * @param {string} src
 * @param {string} dest
 * @param {boolean} verbatimSymlinks
 */
function onLink(destExists, src, dest, verbatimSymlinks) {
  let resolvedSrc = readlinkSync(src);
  if (!verbatimSymlinks && !isAbsolute(resolvedSrc)) {
    resolvedSrc = resolve(dirname(src), resolvedSrc);
  }
  if (!destExists) {
    return symlinkSync(resolvedSrc, dest);
  }
  let resolvedDest;
  try {
    resolvedDest = readlinkSync(dest);
  } catch (err) {
    // Dest exists and is a regular file or directory,
    // Windows may throw UNKNOWN error. If dest already exists,
    // fs throws error anyway, so no need to guard against it here.
    if (err.code === 'EINVAL' || err.code === 'UNKNOWN') {
      return symlinkSync(resolvedSrc, dest);
    }
    throw err;
  }
  if (!isAbsolute(resolvedDest)) {
    resolvedDest = resolve(dirname(dest), resolvedDest);
  }
  if (isSrcSubdir(resolvedSrc, resolvedDest)) {
    throw new ERR_FS_CP_EINVAL({
      message: `cannot copy ${resolvedSrc} to a subdirectory of self ` +
          `${resolvedDest}`,
      path: dest,
      syscall: 'cp',
      errno: EINVAL,
      code: 'EINVAL',
    });
  }
  // Prevent copy if src is a subdir of dest since unlinking
  // dest in this case would result in removing src contents
  // and therefore a broken symlink would be created.
  if (statSync(dest).isDirectory() && isSrcSubdir(resolvedDest, resolvedSrc)) {
    throw new ERR_FS_CP_SYMLINK_TO_SUBDIRECTORY({
      message: `cannot overwrite ${resolvedDest} with ${resolvedSrc}`,
      path: dest,
      syscall: 'cp',
      errno: EINVAL,
      code: 'EINVAL',
    });
  }
  return copyLink(resolvedSrc, dest);
}

function copyLink(resolvedSrc, dest) {
  unlinkSync(dest);
  return symlinkSync(resolvedSrc, dest);
}

module.exports = { cpSyncFn };
