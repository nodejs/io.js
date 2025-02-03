'use strict';

// This file is a modified version of the fs-extra's copy method.

const {
  ArrayPrototypeEvery,
  ArrayPrototypeFilter,
  Boolean,
  PromisePrototypeThen,
  PromiseReject,
  SafePromiseAll,
  StringPrototypeSplit,
} = primordials;
const {
  codes: {
    ERR_FS_CP_DIR_TO_NON_DIR,
    ERR_FS_CP_EEXIST,
    ERR_FS_CP_EINVAL,
    ERR_FS_CP_FIFO_PIPE,
    ERR_FS_CP_NON_DIR_TO_DIR,
    ERR_FS_CP_SOCKET,
    ERR_FS_CP_SYMLINK_TO_SUBDIRECTORY,
    ERR_FS_CP_UNKNOWN,
    ERR_FS_EISDIR,
  },
} = require('internal/errors');
const {
  os: {
    errno: {
      EEXIST,
      EISDIR,
      EINVAL,
      ENOTDIR,
    },
  },
} = internalBinding('constants');
const {
  chmod,
  copyFile,
  lstat,
  mkdir,
  opendir,
  readlink,
  stat,
  symlink,
  unlink,
  utimes,
} = require('fs/promises');
const {
  dirname,
  isAbsolute,
  join,
  parse,
  resolve,
  sep,
} = require('path');

async function cpFn(src, dest, opts) {
  // Warn about using preserveTimestamps on 32-bit node
  if (opts.preserveTimestamps && process.arch === 'ia32') {
    const warning = 'Using the preserveTimestamps option in 32-bit ' +
      'node is not recommended';
    process.emitWarning(warning, 'TimestampPrecisionWarning');
  }
  const stats = await checkPaths(src, dest, opts);
  const { srcStat, destStat, skipped } = stats;
  if (skipped) return;
  await checkParentPaths(src, srcStat, dest);
  return checkParentDir(destStat, src, dest, opts);
}

async function checkPaths(src, dest, opts) {
  if (opts.filter && !(await opts.filter(src, dest))) {
    return { __proto__: null, skipped: true };
  }
  const { 0: srcStat, 1: destStat } = await getStats(src, dest, opts);
  if (destStat) {
    if (areIdentical(srcStat, destStat)) {
      throw new ERR_FS_CP_EINVAL({
        message: 'src and dest cannot be the same',
        path: dest,
        syscall: 'cp',
        errno: EINVAL,
        code: 'EINVAL',
      });
    }
    if (srcStat.isDirectory() && !destStat.isDirectory()) {
      throw new ERR_FS_CP_DIR_TO_NON_DIR({
        message: `cannot overwrite non-directory ${dest} ` +
            `with directory ${src}`,
        path: dest,
        syscall: 'cp',
        errno: EISDIR,
        code: 'EISDIR',
      });
    }
    if (!srcStat.isDirectory() && destStat.isDirectory()) {
      throw new ERR_FS_CP_NON_DIR_TO_DIR({
        message: `cannot overwrite directory ${dest} ` +
            `with non-directory ${src}`,
        path: dest,
        syscall: 'cp',
        errno: ENOTDIR,
        code: 'ENOTDIR',
      });
    }
  }

  if (srcStat.isDirectory() && isSrcSubdir(src, dest)) {
    throw new ERR_FS_CP_EINVAL({
      message: `cannot copy ${src} to a subdirectory of self ${dest}`,
      path: dest,
      syscall: 'cp',
      errno: EINVAL,
      code: 'EINVAL',
    });
  }
  return { __proto__: null, srcStat, destStat, skipped: false };
}

function areIdentical(srcStat, destStat) {
  return destStat.ino && destStat.dev && destStat.ino === srcStat.ino &&
    destStat.dev === srcStat.dev;
}

function getStats(src, dest, opts) {
  const statFunc = opts.dereference ?
    (file) => stat(file, { bigint: true }) :
    (file) => lstat(file, { bigint: true });
  return SafePromiseAll([
    statFunc(src),
    PromisePrototypeThen(statFunc(dest), undefined, (err) => {
      if (err.code === 'ENOENT') return null;
      throw err;
    }),
  ]);
}

async function checkParentDir(destStat, src, dest, opts) {
  const destParent = dirname(dest);
  const dirExists = await pathExists(destParent);
  if (dirExists) return getStatsForCopy(destStat, src, dest, opts);
  await mkdir(destParent, { recursive: true });
  return getStatsForCopy(destStat, src, dest, opts);
}

function pathExists(dest) {
  return PromisePrototypeThen(
    stat(dest),
    () => true,
    (err) => (err.code === 'ENOENT' ? false : PromiseReject(err)));
}

// Recursively check if dest parent is a subdirectory of src.
// It works for all file types including symlinks since it
// checks the src and dest inodes. It starts from the deepest
// parent and stops once it reaches the src parent or the root path.
async function checkParentPaths(src, srcStat, dest) {
  const srcParent = resolve(dirname(src));
  const destParent = resolve(dirname(dest));
  if (destParent === srcParent || destParent === parse(destParent).root) {
    return;
  }
  let destStat;
  try {
    destStat = await stat(destParent, { bigint: true });
  } catch (err) {
    if (err.code === 'ENOENT') return;
    throw err;
  }
  if (areIdentical(srcStat, destStat)) {
    throw new ERR_FS_CP_EINVAL({
      message: `cannot copy ${src} to a subdirectory of self ${dest}`,
      path: dest,
      syscall: 'cp',
      errno: EINVAL,
      code: 'EINVAL',
    });
  }
  return checkParentPaths(src, srcStat, destParent);
}

const normalizePathToArray = (path) =>
  ArrayPrototypeFilter(StringPrototypeSplit(resolve(path), sep), Boolean);

// Return true if dest is a subdir of src, otherwise false.
// It only checks the path strings.
function isSrcSubdir(src, dest) {
  const srcArr = normalizePathToArray(src);
  const destArr = normalizePathToArray(dest);
  return ArrayPrototypeEvery(srcArr, (cur, i) => destArr[i] === cur);
}

async function getStatsForCopy(destStat, src, dest, opts) {
  const statFn = opts.dereference ? stat : lstat;
  const srcStat = await statFn(src);
  if (srcStat.isDirectory() && opts.recursive) {
    return onDir(srcStat, destStat, src, dest, opts);
  } else if (srcStat.isDirectory()) {
    throw new ERR_FS_EISDIR({
      message: `${src} is a directory (not copied)`,
      path: src,
      syscall: 'cp',
      errno: EISDIR,
      code: 'EISDIR',
    });
  } else if (srcStat.isFile() ||
            srcStat.isCharacterDevice() ||
            srcStat.isBlockDevice()) {
    return onFile(srcStat, destStat, src, dest, opts);
  } else if (srcStat.isSymbolicLink()) {
    return onLink(destStat, src, dest, opts);
  } else if (srcStat.isSocket()) {
    throw new ERR_FS_CP_SOCKET({
      message: `cannot copy a socket file: ${dest}`,
      path: dest,
      syscall: 'cp',
      errno: EINVAL,
      code: 'EINVAL',
    });
  } else if (srcStat.isFIFO()) {
    throw new ERR_FS_CP_FIFO_PIPE({
      message: `cannot copy a FIFO pipe: ${dest}`,
      path: dest,
      syscall: 'cp',
      errno: EINVAL,
      code: 'EINVAL',
    });
  }
  throw new ERR_FS_CP_UNKNOWN({
    message: `cannot copy an unknown file type: ${dest}`,
    path: dest,
    syscall: 'cp',
    errno: EINVAL,
    code: 'EINVAL',
  });
}

function onFile(srcStat, destStat, src, dest, opts) {
  if (!destStat) return _copyFile(srcStat, src, dest, opts);
  return mayCopyFile(srcStat, src, dest, opts);
}

async function mayCopyFile(srcStat, src, dest, opts) {
  if (opts.force) {
    await unlink(dest);
    return _copyFile(srcStat, src, dest, opts);
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

async function _copyFile(srcStat, src, dest, opts) {
  await copyFile(src, dest, opts.mode);
  if (opts.preserveTimestamps) {
    return handleTimestampsAndMode(srcStat.mode, src, dest);
  }
  return setDestMode(dest, srcStat.mode);
}

async function handleTimestampsAndMode(srcMode, src, dest) {
  // Make sure the file is writable before setting the timestamp
  // otherwise open fails with EPERM when invoked with 'r+'
  // (through utimes call)
  if (fileIsNotWritable(srcMode)) {
    await makeFileWritable(dest, srcMode);
  }
  return setDestTimestampsAndMode(srcMode, src, dest);
}

function fileIsNotWritable(srcMode) {
  return (srcMode & 0o200) === 0;
}

function makeFileWritable(dest, srcMode) {
  return setDestMode(dest, srcMode | 0o200);
}

async function setDestTimestampsAndMode(srcMode, src, dest) {
  await setDestTimestamps(src, dest);
  return setDestMode(dest, srcMode);
}

function setDestMode(dest, srcMode) {
  return chmod(dest, srcMode);
}

async function setDestTimestamps(src, dest) {
  // The initial srcStat.atime cannot be trusted
  // because it is modified by the read(2) system call
  // (See https://nodejs.org/api/fs.html#fs_stat_time_values)
  const updatedSrcStat = await stat(src);
  return utimes(dest, updatedSrcStat.atime, updatedSrcStat.mtime);
}

function onDir(srcStat, destStat, src, dest, opts) {
  if (!destStat) return mkDirAndCopy(srcStat.mode, src, dest, opts);
  return copyDir(src, dest, opts);
}

async function mkDirAndCopy(srcMode, src, dest, opts) {
  await mkdir(dest);
  await copyDir(src, dest, opts);
  return setDestMode(dest, srcMode);
}

async function copyDir(src, dest, opts) {
  const dir = await opendir(src);

  for await (const { name } of dir) {
    const srcItem = join(src, name);
    const destItem = join(dest, name);
    const { destStat, skipped } = await checkPaths(srcItem, destItem, opts);
    if (!skipped) await getStatsForCopy(destStat, srcItem, destItem, opts);
  }
}

async function onLink(destStat, src, dest, opts) {
  let resolvedSrc = await readlink(src);
  if (!opts.verbatimSymlinks && !isAbsolute(resolvedSrc)) {
    resolvedSrc = resolve(dirname(src), resolvedSrc);
  }
  if (!destStat) {
    return symlink(resolvedSrc, dest);
  }
  let resolvedDest;
  try {
    resolvedDest = await readlink(dest);
  } catch (err) {
    // Dest exists and is a regular file or directory,
    // Windows may throw UNKNOWN error. If dest already exists,
    // fs throws error anyway, so no need to guard against it here.
    if (err.code === 'EINVAL' || err.code === 'UNKNOWN') {
      return symlink(resolvedSrc, dest);
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
  // Do not copy if src is a subdir of dest since unlinking
  // dest in this case would result in removing src contents
  // and therefore a broken symlink would be created.
  const srcStat = await stat(src);
  if (srcStat.isDirectory() && isSrcSubdir(resolvedDest, resolvedSrc)) {
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

async function copyLink(resolvedSrc, dest) {
  await unlink(dest);
  return symlink(resolvedSrc, dest);
}

module.exports = {
  areIdentical,
  cpFn,
  isSrcSubdir,
};
