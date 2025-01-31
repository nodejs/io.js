// Copyright Joyent, Inc. and other Node contributors.
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit
// persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
// NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
// USE OR OTHER DEALINGS IN THE SOFTWARE.

// Maintainers, keep in mind that ES1-style octal literals (`0666`) are not
// allowed in strict mode. Use ES6-style octal literals instead (`0o666`).

'use strict';

const {
  ArrayFromAsync,
  ArrayPrototypePush,
  BigIntPrototypeToString,
  Boolean,
  MathMax,
  Number,
  ObjectDefineProperties,
  ObjectDefineProperty,
  Promise,
  PromiseResolve,
  ReflectApply,
  SafeMap,
  SafeSet,
  StringPrototypeCharCodeAt,
  StringPrototypeIndexOf,
  StringPrototypeSlice,
  uncurryThis,
} = primordials;

const { fs: constants } = internalBinding('constants');
const {
  S_IFIFO,
  S_IFLNK,
  S_IFMT,
  S_IFREG,
  S_IFSOCK,
  F_OK,
  R_OK,
  W_OK,
  X_OK,
  O_WRONLY,
  O_SYMLINK,
} = constants;

const pathModule = require('path');
const { isAbsolute } = pathModule;
const { isArrayBufferView } = require('internal/util/types');

const binding = internalBinding('fs');

const { createBlobFromFilePath } = require('internal/blob');

const { Buffer } = require('buffer');
const { isBuffer: BufferIsBuffer } = Buffer;
const BufferToString = uncurryThis(Buffer.prototype.toString);
const {
  AbortError,
  aggregateTwoErrors,
  codes: {
    ERR_ACCESS_DENIED,
    ERR_FS_FILE_TOO_LARGE,
    ERR_INVALID_ARG_VALUE,
  },
} = require('internal/errors');

const {
  FSReqCallback,
  statValues,
} = binding;
const { toPathIfFileURL } = require('internal/url');
const {
  customPromisifyArgs: kCustomPromisifyArgsSymbol,
  deprecate,
  emitExperimentalWarning,
  getLazy,
  kEmptyObject,
  promisify: {
    custom: kCustomPromisifiedSymbol,
  },
  SideEffectFreeRegExpPrototypeExec,
  defineLazyProperties,
  isWindows,
  isMacOS,
} = require('internal/util');
const {
  constants: {
    kIoMaxLength,
    kMaxUserId,
  },
  copyObject,
  Dirent,
  emitRecursiveRmdirWarning,
  getDirent,
  getDirents,
  getOptions,
  getValidatedFd,
  getValidatedPath,
  handleErrorFromBinding,
  preprocessSymlinkDestination,
  Stats,
  getStatFsFromBinding,
  getStatsFromBinding,
  realpathCacheKey,
  stringToFlags,
  stringToSymlinkType,
  toUnixTimestamp,
  validateBufferArray,
  validateCpOptions,
  validateOffsetLengthRead,
  validateOffsetLengthWrite,
  validatePath,
  validatePosition,
  validateRmOptions,
  validateRmOptionsSync,
  validateRmdirOptions,
  validateStringAfterArrayBufferView,
  warnOnNonPortableTemplate,
} = require('internal/fs/utils');
const {
  CHAR_FORWARD_SLASH,
  CHAR_BACKWARD_SLASH,
} = require('internal/constants');
const {
  isInt32,
  parseFileMode,
  validateBoolean,
  validateBuffer,
  validateEncoding,
  validateFunction,
  validateInteger,
  validateObject,
  validateOneOf,
  validateString,
  kValidateObjectAllowNullable,
} = require('internal/validators');

const permission = require('internal/process/permission');

let truncateWarn = true;
let fs;

// Lazy loaded
let cpFn;
let cpSyncFn;
let promises = null;
let ReadStream;
let WriteStream;
let rimraf;
let kResistStopPropagation;
let ReadFileContext;

// These have to be separate because of how graceful-fs happens to do it's
// monkeypatching.
let FileReadStream;
let FileWriteStream;

function showTruncateDeprecation() {
  if (truncateWarn) {
    process.emitWarning(
      'Using fs.truncate with a file descriptor is deprecated. Please use ' +
      'fs.ftruncate with a file descriptor instead.',
      'DeprecationWarning', 'DEP0081');
    truncateWarn = false;
  }
}

// Ensure that callbacks run in the global context. Only use this function
// for callbacks that are passed to the binding layer, callbacks that are
// invoked from JS already run in the proper scope.
function makeCallback(cb) {
  validateFunction(cb, 'cb');

  return (...args) => ReflectApply(cb, this, args);
}

// Special case of `makeCallback()` that is specific to async `*stat()` calls as
// an optimization, since the data passed back to the callback needs to be
// transformed anyway.
function makeStatsCallback(cb) {
  validateFunction(cb, 'cb');

  return (err, stats) => {
    if (err) return cb(err);
    cb(err, getStatsFromBinding(stats));
  };
}

const isFd = isInt32;

function isFileType(stats, fileType) {
  // Use stats array directly to avoid creating an fs.Stats instance just for
  // our internal use.
  let mode = stats[1];
  if (typeof mode === 'bigint')
    mode = Number(mode);
  return (mode & S_IFMT) === fileType;
}

/**
 * Tests a user's permissions for the file or directory
 * specified by `path`.
 * @param {string | Buffer | URL} path
 * @param {number} [mode]
 * @param {(err?: Error) => any} callback
 * @returns {void}
 */
function access(path, mode, callback) {
  if (typeof mode === 'function') {
    callback = mode;
    mode = F_OK;
  }

  path = getValidatedPath(path);
  callback = makeCallback(callback);

  const req = new FSReqCallback();
  req.oncomplete = callback;
  binding.access(path, mode, req);
}

/**
 * Synchronously tests a user's permissions for the file or
 * directory specified by `path`.
 * @param {string | Buffer | URL} path
 * @param {number} [mode]
 * @returns {void}
 */
function accessSync(path, mode) {
  binding.access(getValidatedPath(path), mode);
}

/**
 * Tests whether or not the given path exists.
 * @param {string | Buffer | URL} path
 * @param {(exists?: boolean) => any} callback
 * @returns {void}
 */
function exists(path, callback) {
  validateFunction(callback, 'cb');

  function suppressedCallback(err) {
    callback(!err);
  }

  try {
    fs.access(path, F_OK, suppressedCallback);
  } catch {
    return callback(false);
  }
}

ObjectDefineProperty(exists, kCustomPromisifiedSymbol, {
  __proto__: null,
  value: function exists(path) { // eslint-disable-line func-name-matching
    return new Promise((resolve) => fs.exists(path, resolve));
  },
});

let showExistsDeprecation = true;
/**
 * Synchronously tests whether or not the given path exists.
 * @param {string | Buffer | URL} path
 * @returns {boolean}
 */
function existsSync(path) {
  try {
    path = getValidatedPath(path);
  } catch {
    if (showExistsDeprecation) {
      process.emitWarning(
        'Passing invalid argument types to fs.existsSync is deprecated', 'DeprecationWarning', 'DEP0187',
      );
      showExistsDeprecation = false;
    }
    return false;
  }

  return binding.existsSync(path);
}

function readFileAfterOpen(err, fd) {
  const context = this.context;

  if (err) {
    context.callback(err);
    return;
  }

  context.fd = fd;

  const req = new FSReqCallback();
  req.oncomplete = readFileAfterStat;
  req.context = context;
  binding.fstat(fd, false, req);
}

function readFileAfterStat(err, stats) {
  const context = this.context;

  if (err)
    return context.close(err);

  // TODO(BridgeAR): Check if allocating a smaller chunk is better performance
  // wise, similar to the promise based version (less peak memory and chunked
  // stringify operations vs multiple C++/JS boundary crossings).
  const size = context.size = isFileType(stats, S_IFREG) ? stats[8] : 0;

  if (size > kIoMaxLength) {
    err = new ERR_FS_FILE_TOO_LARGE(size);
    return context.close(err);
  }

  try {
    if (size === 0) {
      // TODO(BridgeAR): If an encoding is set, use the StringDecoder to concat
      // the result and reuse the buffer instead of allocating a new one.
      context.buffers = [];
    } else {
      context.buffer = Buffer.allocUnsafeSlow(size);
    }
  } catch (err) {
    return context.close(err);
  }
  context.read();
}

function checkAborted(signal, callback) {
  if (signal?.aborted) {
    callback(new AbortError(undefined, { cause: signal.reason }));
    return true;
  }
  return false;
}

/**
 * Asynchronously reads the entire contents of a file.
 * @param {string | Buffer | URL | number} path
 * @param {{
 *   encoding?: string | null;
 *   flag?: string;
 *   signal?: AbortSignal;
 *   } | string} [options]
 * @param {(
 *   err?: Error,
 *   data?: string | Buffer
 *   ) => any} callback
 * @returns {void}
 */
function readFile(path, options, callback) {
  callback ||= options;
  validateFunction(callback, 'cb');
  options = getOptions(options, { flag: 'r' });
  ReadFileContext ??= require('internal/fs/read/context');
  const context = new ReadFileContext(callback, options.encoding);
  context.isUserFd = isFd(path); // File descriptor ownership

  if (options.signal) {
    context.signal = options.signal;
  }
  if (context.isUserFd) {
    process.nextTick(function tick(context) {
      ReflectApply(readFileAfterOpen, { context }, [null, path]);
    }, context);
    return;
  }

  if (checkAborted(options.signal, callback))
    return;

  const flagsNumber = stringToFlags(options.flag, 'options.flag');
  const req = new FSReqCallback();
  req.context = context;
  req.oncomplete = readFileAfterOpen;
  binding.open(getValidatedPath(path), flagsNumber, 0o666, req);
}

function tryStatSync(fd, isUserFd) {
  const stats = binding.fstat(fd, false, undefined, true /* shouldNotThrow */);
  if (stats === undefined && !isUserFd) {
    fs.closeSync(fd);
  }
  return stats;
}

function tryCreateBuffer(size, fd, isUserFd) {
  let threw = true;
  let buffer;
  try {
    if (size > kIoMaxLength) {
      throw new ERR_FS_FILE_TOO_LARGE(size);
    }
    buffer = Buffer.allocUnsafe(size);
    threw = false;
  } finally {
    if (threw && !isUserFd) fs.closeSync(fd);
  }
  return buffer;
}

function tryReadSync(fd, isUserFd, buffer, pos, len) {
  let threw = true;
  let bytesRead;
  try {
    bytesRead = fs.readSync(fd, buffer, pos, len);
    threw = false;
  } finally {
    if (threw && !isUserFd) fs.closeSync(fd);
  }
  return bytesRead;
}

/**
 * Synchronously reads the entire contents of a file.
 * @param {string | Buffer | URL | number} path
 * @param {{
 *   encoding?: string | null;
 *   flag?: string;
 *   }} [options]
 * @returns {string | Buffer}
 */
function readFileSync(path, options) {
  options = getOptions(options, { flag: 'r' });

  if (options.encoding === 'utf8' || options.encoding === 'utf-8') {
    if (!isInt32(path)) {
      path = getValidatedPath(path);
    }
    return binding.readFileUtf8(path, stringToFlags(options.flag));
  }

  const isUserFd = isFd(path); // File descriptor ownership
  const fd = isUserFd ? path : fs.openSync(path, options.flag, 0o666);

  const stats = tryStatSync(fd, isUserFd);
  const size = isFileType(stats, S_IFREG) ? stats[8] : 0;
  let pos = 0;
  let buffer; // Single buffer with file data
  let buffers; // List for when size is unknown

  if (size === 0) {
    buffers = [];
  } else {
    buffer = tryCreateBuffer(size, fd, isUserFd);
  }

  let bytesRead;

  if (size !== 0) {
    do {
      bytesRead = tryReadSync(fd, isUserFd, buffer, pos, size - pos);
      pos += bytesRead;
    } while (bytesRead !== 0 && pos < size);
  } else {
    do {
      // The kernel lies about many files.
      // Go ahead and try to read some bytes.
      buffer = Buffer.allocUnsafe(8192);
      bytesRead = tryReadSync(fd, isUserFd, buffer, 0, 8192);
      if (bytesRead !== 0) {
        ArrayPrototypePush(buffers, buffer.slice(0, bytesRead));
      }
      pos += bytesRead;
    } while (bytesRead !== 0);
  }

  if (!isUserFd)
    fs.closeSync(fd);

  if (size === 0) {
    // Data was collected into the buffers list.
    buffer = Buffer.concat(buffers, pos);
  } else if (pos < size) {
    buffer = buffer.slice(0, pos);
  }

  if (options.encoding) buffer = buffer.toString(options.encoding);
  return buffer;
}

function defaultCloseCallback(err) {
  if (err != null) throw err;
}

/**
 * Closes the file descriptor.
 * @param {number} fd
 * @param {(err?: Error) => any} [callback]
 * @returns {void}
 */
function close(fd, callback = defaultCloseCallback) {
  if (callback !== defaultCloseCallback)
    callback = makeCallback(callback);

  const req = new FSReqCallback();
  req.oncomplete = callback;
  binding.close(fd, req);
}

/**
 * Synchronously closes the file descriptor.
 * @param {number} fd
 * @returns {void}
 */
function closeSync(fd) {
  binding.close(fd);
}

/**
 * Asynchronously opens a file.
 * @param {string | Buffer | URL} path
 * @param {string | number} [flags]
 * @param {string | number} [mode]
 * @param {(
 *   err?: Error,
 *   fd?: number
 *   ) => any} callback
 * @returns {void}
 */
function open(path, flags, mode, callback) {
  path = getValidatedPath(path);
  if (arguments.length < 3) {
    callback = flags;
    flags = 'r';
    mode = 0o666;
  } else if (typeof mode === 'function') {
    callback = mode;
    mode = 0o666;
  } else {
    mode = parseFileMode(mode, 'mode', 0o666);
  }
  const flagsNumber = stringToFlags(flags);
  callback = makeCallback(callback);

  const req = new FSReqCallback();
  req.oncomplete = callback;

  binding.open(path, flagsNumber, mode, req);
}

/**
 * Synchronously opens a file.
 * @param {string | Buffer | URL} path
 * @param {string | number} [flags]
 * @param {string | number} [mode]
 * @returns {number}
 */
function openSync(path, flags, mode) {
  return binding.open(
    getValidatedPath(path),
    stringToFlags(flags),
    parseFileMode(mode, 'mode', 0o666),
  );
}

/**
 * @param {string | Buffer | URL } path
 * @param {{
 *   type?: string;
 *   }} [options]
 * @returns {Promise<Blob>}
 */
function openAsBlob(path, options = kEmptyObject) {
  validateObject(options, 'options');
  const type = options.type || '';
  validateString(type, 'options.type');
  // The underlying implementation here returns the Blob synchronously for now.
  // To give ourselves flexibility to maybe return the Blob asynchronously,
  // this API returns a Promise.
  path = getValidatedPath(path);
  return PromiseResolve(createBlobFromFilePath(path, { type }));
}

/**
 * Reads file from the specified `fd` (file descriptor).
 * @param {number} fd
 * @param {Buffer | TypedArray | DataView} buffer
 * @param {number | {
 *   offset?: number;
 *   length?: number;
 *   position?: number | bigint | null;
 *   }} [offsetOrOptions]
 * @param {number} length
 * @param {number | bigint | null} position
 * @param {(
 *   err?: Error,
 *   bytesRead?: number,
 *   buffer?: Buffer
 *   ) => any} callback
 * @returns {void}
 */
function read(fd, buffer, offsetOrOptions, length, position, callback) {
  fd = getValidatedFd(fd);

  let offset = offsetOrOptions;
  let params = null;
  if (arguments.length <= 4) {
    if (arguments.length === 4) {
      // This is fs.read(fd, buffer, options, callback)
      validateObject(offsetOrOptions, 'options', kValidateObjectAllowNullable);
      callback = length;
      params = offsetOrOptions;
    } else if (arguments.length === 3) {
      // This is fs.read(fd, bufferOrParams, callback)
      if (!isArrayBufferView(buffer)) {
        // This is fs.read(fd, params, callback)
        params = buffer;
        ({ buffer = Buffer.alloc(16384) } = params ?? kEmptyObject);
      }
      callback = offsetOrOptions;
    } else {
      // This is fs.read(fd, callback)
      callback = buffer;
      buffer = Buffer.alloc(16384);
    }

    if (params !== undefined) {
      validateObject(params, 'options', kValidateObjectAllowNullable);
    }
    ({
      offset = 0,
      length = buffer?.byteLength - offset,
      position = null,
    } = params ?? kEmptyObject);
  }

  validateBuffer(buffer);
  validateFunction(callback, 'cb');

  if (offset == null) {
    offset = 0;
  } else {
    validateInteger(offset, 'offset', 0);
  }

  length |= 0;

  if (length === 0) {
    return process.nextTick(function tick() {
      callback(null, 0, buffer);
    });
  }

  if (buffer.byteLength === 0) {
    throw new ERR_INVALID_ARG_VALUE('buffer', buffer,
                                    'is empty and cannot be written');
  }

  validateOffsetLengthRead(offset, length, buffer.byteLength);

  if (position == null) {
    position = -1;
  } else {
    validatePosition(position, 'position', length);
  }

  function wrapper(err, bytesRead) {
    // Retain a reference to buffer so that it can't be GC'ed too soon.
    callback(err, bytesRead || 0, buffer);
  }

  const req = new FSReqCallback();
  req.oncomplete = wrapper;

  binding.read(fd, buffer, offset, length, position, req);
}

ObjectDefineProperty(read, kCustomPromisifyArgsSymbol,
                     { __proto__: null, value: ['bytesRead', 'buffer'], enumerable: false });

/**
 * Synchronously reads the file from the
 * specified `fd` (file descriptor).
 * @param {number} fd
 * @param {Buffer | TypedArray | DataView} buffer
 * @param {number | {
 *   offset?: number;
 *   length?: number;
 *   position?: number | bigint | null;
 *   }} [offsetOrOptions]
 * @param {number} [length]
 * @param {number} [position]
 * @returns {number}
 */
function readSync(fd, buffer, offsetOrOptions, length, position) {
  fd = getValidatedFd(fd);

  validateBuffer(buffer);

  let offset = offsetOrOptions;
  if (arguments.length <= 3 || typeof offsetOrOptions === 'object') {
    if (offsetOrOptions !== undefined) {
      validateObject(offsetOrOptions, 'options', kValidateObjectAllowNullable);
    }

    ({
      offset = 0,
      length = buffer.byteLength - offset,
      position = null,
    } = offsetOrOptions ?? kEmptyObject);
  }

  if (offset === undefined) {
    offset = 0;
  } else {
    validateInteger(offset, 'offset', 0);
  }

  length |= 0;

  if (length === 0) {
    return 0;
  }

  if (buffer.byteLength === 0) {
    throw new ERR_INVALID_ARG_VALUE('buffer', buffer,
                                    'is empty and cannot be written');
  }

  validateOffsetLengthRead(offset, length, buffer.byteLength);

  if (position == null) {
    position = -1;
  } else {
    validatePosition(position, 'position', length);
  }

  return binding.read(fd, buffer, offset, length, position);
}

/**
 * Reads file from the specified `fd` (file descriptor)
 * and writes to an array of `ArrayBufferView`s.
 * @param {number} fd
 * @param {ArrayBufferView[]} buffers
 * @param {number | null} [position]
 * @param {(
 *   err?: Error,
 *   bytesRead?: number,
 *   buffers?: ArrayBufferView[]
 * ) => any} callback
 * @returns {void}
 */
function readv(fd, buffers, position, callback) {
  function wrapper(err, read) {
    callback(err, read || 0, buffers);
  }

  fd = getValidatedFd(fd);
  validateBufferArray(buffers);
  callback ||= position;
  validateFunction(callback, 'cb');

  const req = new FSReqCallback();
  req.oncomplete = wrapper;

  if (typeof position !== 'number')
    position = null;

  binding.readBuffers(fd, buffers, position, req);
}

ObjectDefineProperty(readv, kCustomPromisifyArgsSymbol,
                     { __proto__: null, value: ['bytesRead', 'buffers'], enumerable: false });

/**
 * Synchronously reads file from the
 * specified `fd` (file descriptor) and writes to an array
 * of `ArrayBufferView`s.
 * @param {number} fd
 * @param {ArrayBufferView[]} buffers
 * @param {number | null} [position]
 * @returns {number}
 */
function readvSync(fd, buffers, position) {
  fd = getValidatedFd(fd);
  validateBufferArray(buffers);

  if (typeof position !== 'number')
    position = null;

  return binding.readBuffers(fd, buffers, position);
}

/**
 * Writes `buffer` to the specified `fd` (file descriptor).
 * @param {number} fd
 * @param {Buffer | TypedArray | DataView | string} buffer
 * @param {number | object} [offsetOrOptions]
 * @param {number} [length]
 * @param {number | null} [position]
 * @param {(
 *   err?: Error,
 *   bytesWritten?: number,
 *   buffer?: Buffer | TypedArray | DataView
 * ) => any} callback
 * @returns {void}
 */
function write(fd, buffer, offsetOrOptions, length, position, callback) {
  function wrapper(err, written) {
    // Retain a reference to buffer so that it can't be GC'ed too soon.
    callback(err, written || 0, buffer);
  }

  fd = getValidatedFd(fd);

  let offset = offsetOrOptions;
  if (isArrayBufferView(buffer)) {
    callback ||= position || length || offset;
    validateFunction(callback, 'cb');

    if (typeof offset === 'object') {
      ({
        offset = 0,
        length = buffer.byteLength - offset,
        position = null,
      } = offsetOrOptions ?? kEmptyObject);
    }

    if (offset == null || typeof offset === 'function') {
      offset = 0;
    } else {
      validateInteger(offset, 'offset', 0);
    }
    if (typeof length !== 'number')
      length = buffer.byteLength - offset;
    if (typeof position !== 'number')
      position = null;
    validateOffsetLengthWrite(offset, length, buffer.byteLength);

    const req = new FSReqCallback();
    req.oncomplete = wrapper;
    binding.writeBuffer(fd, buffer, offset, length, position, req);
    return;
  }

  validateStringAfterArrayBufferView(buffer, 'buffer');

  if (typeof position !== 'function') {
    if (typeof offset === 'function') {
      position = offset;
      offset = null;
    } else {
      position = length;
    }
    length = 'utf8';
  }

  const str = buffer;
  validateEncoding(str, length);
  callback = position;
  validateFunction(callback, 'cb');

  const req = new FSReqCallback();
  req.oncomplete = wrapper;
  binding.writeString(fd, str, offset, length, req);
}

ObjectDefineProperty(write, kCustomPromisifyArgsSymbol,
                     { __proto__: null, value: ['bytesWritten', 'buffer'], enumerable: false });

/**
 * Synchronously writes `buffer` to the
 * specified `fd` (file descriptor).
 * @param {number} fd
 * @param {Buffer | TypedArray | DataView | string} buffer
 * @param {{
 *   offset?: number;
 *   length?: number;
 *   position?: number | null;
 *   }} [offsetOrOptions]
 * @param {number} [length]
 * @param {number} [position]
 * @returns {number}
 */
function writeSync(fd, buffer, offsetOrOptions, length, position) {
  fd = getValidatedFd(fd);
  const ctx = {};
  let result;

  let offset = offsetOrOptions;
  if (isArrayBufferView(buffer)) {
    if (typeof offset === 'object') {
      ({
        offset = 0,
        length = buffer.byteLength - offset,
        position = null,
      } = offsetOrOptions ?? kEmptyObject);
    }
    if (position === undefined)
      position = null;
    if (offset == null) {
      offset = 0;
    } else {
      validateInteger(offset, 'offset', 0);
    }
    if (typeof length !== 'number')
      length = buffer.byteLength - offset;
    validateOffsetLengthWrite(offset, length, buffer.byteLength);
    result = binding.writeBuffer(fd, buffer, offset, length, position,
                                 undefined, ctx);
  } else {
    validateStringAfterArrayBufferView(buffer, 'buffer');
    validateEncoding(buffer, length);

    if (offset === undefined)
      offset = null;
    result = binding.writeString(fd, buffer, offset, length,
                                 undefined, ctx);
  }
  handleErrorFromBinding(ctx);
  return result;
}

/**
 * Writes an array of `ArrayBufferView`s to the
 * specified `fd` (file descriptor).
 * @param {number} fd
 * @param {ArrayBufferView[]} buffers
 * @param {number | null} [position]
 * @param {(
 *   err?: Error,
 *   bytesWritten?: number,
 *   buffers?: ArrayBufferView[]
 *   ) => any} callback
 * @returns {void}
 */
function writev(fd, buffers, position, callback) {
  function wrapper(err, written) {
    callback(err, written || 0, buffers);
  }

  fd = getValidatedFd(fd);
  validateBufferArray(buffers);
  callback ||= position;
  validateFunction(callback, 'cb');

  if (buffers.length === 0) {
    process.nextTick(callback, null, 0, buffers);
    return;
  }

  const req = new FSReqCallback();
  req.oncomplete = wrapper;

  if (typeof position !== 'number')
    position = null;

  binding.writeBuffers(fd, buffers, position, req);
}

ObjectDefineProperty(writev, kCustomPromisifyArgsSymbol, {
  __proto__: null,
  value: ['bytesWritten', 'buffer'],
  enumerable: false,
});

/**
 * Synchronously writes an array of `ArrayBufferView`s
 * to the specified `fd` (file descriptor).
 * @param {number} fd
 * @param {ArrayBufferView[]} buffers
 * @param {number | null} [position]
 * @returns {number}
 */
function writevSync(fd, buffers, position) {
  fd = getValidatedFd(fd);
  validateBufferArray(buffers);

  if (buffers.length === 0) {
    return 0;
  }

  if (typeof position !== 'number')
    position = null;

  return binding.writeBuffers(fd, buffers, position);
}

/**
 * Asynchronously renames file at `oldPath` to
 * the pathname provided as `newPath`.
 * @param {string | Buffer | URL} oldPath
 * @param {string | Buffer | URL} newPath
 * @param {(err?: Error) => any} callback
 * @returns {void}
 */
function rename(oldPath, newPath, callback) {
  callback = makeCallback(callback);
  const req = new FSReqCallback();
  req.oncomplete = callback;
  binding.rename(
    getValidatedPath(oldPath, 'oldPath'),
    getValidatedPath(newPath, 'newPath'),
    req,
  );
}


/**
 * Synchronously renames file at `oldPath` to
 * the pathname provided as `newPath`.
 * @param {string | Buffer | URL} oldPath
 * @param {string | Buffer | URL} newPath
 * @returns {void}
 */
function renameSync(oldPath, newPath) {
  binding.rename(
    getValidatedPath(oldPath, 'oldPath'),
    getValidatedPath(newPath, 'newPath'),
  );
}

/**
 * Truncates the file.
 * @param {string | Buffer | URL} path
 * @param {number} [len]
 * @param {(err?: Error) => any} callback
 * @returns {void}
 */
function truncate(path, len, callback) {
  if (typeof path === 'number') {
    showTruncateDeprecation();
    return fs.ftruncate(path, len, callback);
  }
  if (typeof len === 'function') {
    callback = len;
    len = 0;
  } else if (len === undefined) {
    len = 0;
  }

  validateInteger(len, 'len');
  len = MathMax(0, len);
  validateFunction(callback, 'cb');
  fs.open(path, 'r+', (er, fd) => {
    if (er) return callback(er);
    const req = new FSReqCallback();
    req.oncomplete = function oncomplete(er) {
      fs.close(fd, (er2) => {
        callback(aggregateTwoErrors(er2, er));
      });
    };
    binding.ftruncate(fd, len, req);
  });
}

/**
 * Synchronously truncates the file.
 * @param {string | Buffer | URL} path
 * @param {number} [len]
 * @returns {void}
 */
function truncateSync(path, len) {
  if (typeof path === 'number') {
    // legacy
    showTruncateDeprecation();
    return fs.ftruncateSync(path, len);
  }
  if (len === undefined) {
    len = 0;
  }
  // Allow error to be thrown, but still close fd.
  const fd = fs.openSync(path, 'r+');
  try {
    fs.ftruncateSync(fd, len);
  } finally {
    fs.closeSync(fd);
  }
}

/**
 * Truncates the file descriptor.
 * @param {number} fd
 * @param {number} [len]
 * @param {(err?: Error) => any} callback
 * @returns {void}
 */
function ftruncate(fd, len = 0, callback) {
  if (typeof len === 'function') {
    callback = len;
    len = 0;
  }
  validateInteger(len, 'len');
  len = MathMax(0, len);
  callback = makeCallback(callback);

  const req = new FSReqCallback();
  req.oncomplete = callback;
  binding.ftruncate(fd, len, req);
}

/**
 * Synchronously truncates the file descriptor.
 * @param {number} fd
 * @param {number} [len]
 * @returns {void}
 */
function ftruncateSync(fd, len = 0) {
  validateInteger(len, 'len');
  binding.ftruncate(fd, len < 0 ? 0 : len);
}

function lazyLoadCp() {
  if (cpFn === undefined) {
    ({ cpFn } = require('internal/fs/cp/cp'));
    cpFn = require('util').callbackify(cpFn);
    ({ cpSyncFn } = require('internal/fs/cp/cp-sync'));
  }
}

function lazyLoadRimraf() {
  if (rimraf === undefined)
    ({ rimraf } = require('internal/fs/rimraf'));
}

/**
 * Asynchronously removes a directory.
 * @param {string | Buffer | URL} path
 * @param {{
 *   maxRetries?: number;
 *   recursive?: boolean;
 *   retryDelay?: number;
 *   }} [options]
 * @param {(err?: Error) => any} callback
 * @returns {void}
 */
function rmdir(path, options, callback) {
  if (typeof options === 'function') {
    callback = options;
    options = undefined;
  }

  callback = makeCallback(callback);
  path = getValidatedPath(path);

  if (options?.recursive) {
    emitRecursiveRmdirWarning();
    validateRmOptions(
      path,
      { ...options, force: false },
      true,
      (err, options) => {
        if (err === false) {
          const req = new FSReqCallback();
          req.oncomplete = callback;
          binding.rmdir(path, req);
          return;
        }
        if (err) {
          return callback(err);
        }

        lazyLoadRimraf();
        rimraf(path, options, callback);
      });
  } else {
    validateRmdirOptions(options);
    const req = new FSReqCallback();
    req.oncomplete = callback;
    binding.rmdir(path, req);
  }
}

/**
 * Synchronously removes a directory.
 * @param {string | Buffer | URL} path
 * @param {{
 *   maxRetries?: number;
 *   recursive?: boolean;
 *   retryDelay?: number;
 *   }} [options]
 * @returns {void}
 */
function rmdirSync(path, options) {
  path = getValidatedPath(path);

  if (options?.recursive) {
    emitRecursiveRmdirWarning();
    options = validateRmOptionsSync(path, { ...options, force: false }, true);
    if (options !== false) {
      return binding.rmSync(path, options.maxRetries, options.recursive, options.retryDelay);
    }
  } else {
    validateRmdirOptions(options);
  }

  binding.rmdir(path);
}

/**
 * Asynchronously removes files and
 * directories (modeled on the standard POSIX `rm` utility).
 * @param {string | Buffer | URL} path
 * @param {{
 *   force?: boolean;
 *   maxRetries?: number;
 *   recursive?: boolean;
 *   retryDelay?: number;
 *   }} [options]
 * @param {(err?: Error) => any} callback
 * @returns {void}
 */
function rm(path, options, callback) {
  if (typeof options === 'function') {
    callback = options;
    options = undefined;
  }
  path = getValidatedPath(path);

  validateRmOptions(path, options, false, (err, options) => {
    if (err) {
      return callback(err);
    }
    lazyLoadRimraf();
    return rimraf(path, options, callback);
  });
}

/**
 * Synchronously removes files and
 * directories (modeled on the standard POSIX `rm` utility).
 * @param {string | Buffer | URL} path
 * @param {{
 *   force?: boolean;
 *   maxRetries?: number;
 *   recursive?: boolean;
 *   retryDelay?: number;
 *   }} [options]
 * @returns {void}
 */
function rmSync(path, options) {
  const opts = validateRmOptionsSync(path, options, false);
  return binding.rmSync(getValidatedPath(path), opts.maxRetries, opts.recursive, opts.retryDelay);
}

/**
 * Forces all currently queued I/O operations associated
 * with the file to the operating system's synchronized
 * I/O completion state.
 * @param {number} fd
 * @param {(err?: Error) => any} callback
 * @returns {void}
 */
function fdatasync(fd, callback) {
  const req = new FSReqCallback();
  req.oncomplete = makeCallback(callback);
  binding.fdatasync(fd, req);
}

/**
 * Synchronously forces all currently queued I/O operations
 * associated with the file to the operating
 * system's synchronized I/O completion state.
 * @param {number} fd
 * @returns {void}
 */
function fdatasyncSync(fd) {
  binding.fdatasync(fd);
}

/**
 * Requests for all data for the open file descriptor
 * to be flushed to the storage device.
 * @param {number} fd
 * @param {(err?: Error) => any} callback
 * @returns {void}
 */
function fsync(fd, callback) {
  const req = new FSReqCallback();
  req.oncomplete = makeCallback(callback);
  binding.fsync(fd, req);
}

/**
 * Synchronously requests for all data for the open
 * file descriptor to be flushed to the storage device.
 * @param {number} fd
 * @returns {void}
 */
function fsyncSync(fd) {
  binding.fsync(fd);
}

/**
 * Asynchronously creates a directory.
 * @param {string | Buffer | URL} path
 * @param {{
 *   recursive?: boolean;
 *   mode?: string | number;
 *   } | number} [options]
 * @param {(err?: Error) => any} callback
 * @returns {void}
 */
function mkdir(path, options, callback) {
  let mode = 0o777;
  let recursive = false;
  if (typeof options === 'function') {
    callback = options;
  } else if (typeof options === 'number' || typeof options === 'string') {
    mode = parseFileMode(options, 'mode');
  } else if (options) {
    if (options.recursive !== undefined) {
      recursive = options.recursive;
      validateBoolean(recursive, 'options.recursive');
    }
    if (options.mode !== undefined) {
      mode = parseFileMode(options.mode, 'options.mode');
    }
  }
  callback = makeCallback(callback);

  const req = new FSReqCallback();
  req.oncomplete = callback;
  binding.mkdir(
    getValidatedPath(path),
    mode,
    recursive,
    req,
  );
}

/**
 * Synchronously creates a directory.
 * @param {string | Buffer | URL} path
 * @param {{
 *   recursive?: boolean;
 *   mode?: string | number;
 *   } | number} [options]
 * @returns {string | void}
 */
function mkdirSync(path, options) {
  let mode = 0o777;
  let recursive = false;
  if (typeof options === 'number' || typeof options === 'string') {
    mode = parseFileMode(options, 'mode');
  } else if (options) {
    if (options.recursive !== undefined) {
      recursive = options.recursive;
      validateBoolean(recursive, 'options.recursive');
    }
    if (options.mode !== undefined) {
      mode = parseFileMode(options.mode, 'options.mode');
    }
  }

  const result = binding.mkdir(
    getValidatedPath(path),
    mode,
    recursive,
  );

  if (recursive) {
    return result;
  }
}

/*
 * An recursive algorithm for reading the entire contents of the `basePath` directory.
 * This function does not validate `basePath` as a directory. It is passed directly to
 * `binding.readdir`.
 * @param {string} basePath
 * @param {{ encoding: string, withFileTypes: boolean }} options
 * @param {(
 *   err?: Error,
 *   files?: string[] | Buffer[] | Dirent[]
 * ) => any} callback
 * @returns {void}
*/
function readdirRecursive(basePath, options, callback) {
  const context = {
    withFileTypes: Boolean(options.withFileTypes),
    encoding: options.encoding,
    basePath,
    readdirResults: [],
    pathsQueue: [basePath],
  };

  let i = 0;

  function read(path) {
    const req = new FSReqCallback();
    req.oncomplete = (err, result) => {
      if (err) {
        callback(err);
        return;
      }

      if (result === undefined) {
        callback(null, context.readdirResults);
        return;
      }

      processReaddirResult({
        result,
        currentPath: path,
        context,
      });

      if (i < context.pathsQueue.length) {
        read(context.pathsQueue[i++]);
      } else {
        callback(null, context.readdirResults);
      }
    };

    binding.readdir(
      path,
      context.encoding,
      context.withFileTypes,
      req,
    );
  }

  read(context.pathsQueue[i++]);
}

// Calling `readdir` with `withFileTypes=true`, the result is an array of arrays.
// The first array is the names, and the second array is the types.
// They are guaranteed to be the same length; hence, setting `length` to the length
// of the first array within the result.
const processReaddirResult = (args) => (args.context.withFileTypes ? handleDirents(args) : handleFilePaths(args));

function handleDirents({ result, currentPath, context }) {
  const { 0: names, 1: types } = result;
  const { length } = names;

  for (let i = 0; i < length; i++) {
    // Avoid excluding symlinks, as they are not directories.
    // Refs: https://github.com/nodejs/node/issues/52663
    const fullPath = pathModule.join(currentPath, names[i]);
    const dirent = getDirent(currentPath, names[i], types[i]);
    ArrayPrototypePush(context.readdirResults, dirent);

    if (dirent.isDirectory() || binding.internalModuleStat(binding, fullPath) === 1) {
      ArrayPrototypePush(context.pathsQueue, fullPath);
    }
  }
}

function handleFilePaths({ result, currentPath, context }) {
  for (let i = 0; i < result.length; i++) {
    const resultPath = pathModule.join(currentPath, result[i]);
    const relativeResultPath = pathModule.relative(context.basePath, resultPath);
    const stat = binding.internalModuleStat(binding, resultPath);
    ArrayPrototypePush(context.readdirResults, relativeResultPath);

    if (stat === 1) {
      ArrayPrototypePush(context.pathsQueue, resultPath);
    }
  }
}

/**
 * An iterative algorithm for reading the entire contents of the `basePath` directory.
 * This function does not validate `basePath` as a directory. It is passed directly to
 * `binding.readdir`.
 * @param {string} basePath
 * @param {{ encoding: string, withFileTypes: boolean }} options
 * @returns {string[] | Dirent[]}
 */
function readdirSyncRecursive(basePath, options) {
  const context = {
    withFileTypes: Boolean(options.withFileTypes),
    encoding: options.encoding,
    basePath,
    readdirResults: [],
    pathsQueue: [basePath],
  };

  function read(path) {
    const readdirResult = binding.readdir(
      path,
      context.encoding,
      context.withFileTypes,
    );

    if (readdirResult === undefined) {
      return;
    }

    processReaddirResult({
      result: readdirResult,
      currentPath: path,
      context,
    });
  }

  for (let i = 0; i < context.pathsQueue.length; i++) {
    read(context.pathsQueue[i]);
  }

  return context.readdirResults;
}

/**
 * Reads the contents of a directory.
 * @param {string | Buffer | URL} path
 * @param {string | {
 *   encoding?: string;
 *   withFileTypes?: boolean;
 *   recursive?: boolean;
 *   }} [options]
 * @param {(
 *   err?: Error,
 *   files?: string[] | Buffer[] | Dirent[]
 * ) => any} callback
 * @returns {void}
 */
function readdir(path, options, callback) {
  callback = makeCallback(typeof options === 'function' ? options : callback);
  options = getOptions(options);
  path = getValidatedPath(path);
  if (options.recursive != null) {
    validateBoolean(options.recursive, 'options.recursive');
  }

  if (options.recursive) {
    // Make shallow copy to prevent mutating options from affecting results
    options = copyObject(options);

    readdirRecursive(path, options, callback);
    return;
  }

  const req = new FSReqCallback();
  if (!options.withFileTypes) {
    req.oncomplete = callback;
  } else {
    req.oncomplete = (err, result) => {
      if (err) {
        callback(err);
        return;
      }
      getDirents(path, result, callback);
    };
  }
  binding.readdir(
    path,
    options.encoding,
    !!options.withFileTypes,
    req,
  );
}

/**
 * Synchronously reads the contents of a directory.
 * @param {string | Buffer | URL} path
 * @param {string | {
 *   encoding?: string;
 *   withFileTypes?: boolean;
 *   recursive?: boolean;
 *   }} [options]
 * @returns {string | Buffer[] | Dirent[]}
 */
function readdirSync(path, options) {
  options = getOptions(options);
  path = getValidatedPath(path);
  if (options.recursive != null) {
    validateBoolean(options.recursive, 'options.recursive');
  }

  if (options.recursive) {
    return readdirSyncRecursive(path, options);
  }

  const result = binding.readdir(
    path,
    options.encoding,
    !!options.withFileTypes,
  );

  return result !== undefined && options.withFileTypes ? getDirents(path, result) : result;
}

/**
 * Invokes the callback with the `fs.Stats`
 * for the file descriptor.
 * @param {number} fd
 * @param {{ bigint?: boolean; }} [options]
 * @param {(
 *   err?: Error,
 *   stats?: Stats
 *   ) => any} [callback]
 * @returns {void}
 */
function fstat(fd, options = { bigint: false }, callback) {
  if (typeof options === 'function') {
    callback = options;
    options = kEmptyObject;
  }
  callback = makeStatsCallback(callback);

  const req = new FSReqCallback(options.bigint);
  req.oncomplete = callback;
  binding.fstat(fd, options.bigint, req);
}

/**
 * Retrieves the `fs.Stats` for the symbolic link
 * referred to by the `path`.
 * @param {string | Buffer | URL} path
 * @param {{ bigint?: boolean; }} [options]
 * @param {(
 *   err?: Error,
 *   stats?: Stats
 *   ) => any} callback
 * @returns {void}
 */
function lstat(path, options = { bigint: false }, callback) {
  if (typeof options === 'function') {
    callback = options;
    options = kEmptyObject;
  }
  callback = makeStatsCallback(callback);
  path = getValidatedPath(path);
  if (permission.isEnabled() && !permission.has('fs.read', path)) {
    const resource = BufferIsBuffer(path) ? BufferToString(path) : path;
    callback(new ERR_ACCESS_DENIED('Access to this API has been restricted', 'FileSystemRead', resource));
    return;
  }

  const req = new FSReqCallback(options.bigint);
  req.oncomplete = callback;
  binding.lstat(path, options.bigint, req);
}

/**
 * Asynchronously gets the stats of a file.
 * @param {string | Buffer | URL} path
 * @param {{ bigint?: boolean; }} [options]
 * @param {(
 *   err?: Error,
 *   stats?: Stats
 *   ) => any} callback
 * @returns {void}
 */
function stat(path, options = { bigint: false }, callback) {
  if (typeof options === 'function') {
    callback = options;
    options = kEmptyObject;
  }
  callback = makeStatsCallback(callback);

  const req = new FSReqCallback(options.bigint);
  req.oncomplete = callback;
  binding.stat(getValidatedPath(path), options.bigint, req);
}

function statfs(path, options = { bigint: false }, callback) {
  if (typeof options === 'function') {
    callback = options;
    options = kEmptyObject;
  }
  validateFunction(callback, 'cb');
  path = getValidatedPath(path);
  const req = new FSReqCallback(options.bigint);
  req.oncomplete = (err, stats) => {
    if (err) {
      return callback(err);
    }

    callback(err, getStatFsFromBinding(stats));
  };
  binding.statfs(getValidatedPath(path), options.bigint, req);
}

/**
 * Synchronously retrieves the `fs.Stats` for
 * the file descriptor.
 * @param {number} fd
 * @param {{ bigint?: boolean; }} [options]
 * @returns {Stats | undefined}
 */
function fstatSync(fd, options = { bigint: false }) {
  const stats = binding.fstat(fd, options.bigint, undefined, false);
  if (stats === undefined) {
    return;
  }
  return getStatsFromBinding(stats);
}

/**
 * Synchronously retrieves the `fs.Stats` for
 * the symbolic link referred to by the `path`.
 * @param {string | Buffer | URL} path
 * @param {{
 *   bigint?: boolean;
 *   throwIfNoEntry?: boolean;
 *   }} [options]
 * @returns {Stats | undefined}
 */
function lstatSync(path, options = { bigint: false, throwIfNoEntry: true }) {
  path = getValidatedPath(path);
  if (permission.isEnabled() && !permission.has('fs.read', path)) {
    const resource = BufferIsBuffer(path) ? BufferToString(path) : path;
    throw new ERR_ACCESS_DENIED('Access to this API has been restricted', 'FileSystemRead', resource);
  }
  const stats = binding.lstat(
    getValidatedPath(path),
    options.bigint,
    undefined,
    options.throwIfNoEntry,
  );

  if (stats === undefined) {
    return;
  }
  return getStatsFromBinding(stats);
}

/**
 * Synchronously retrieves the `fs.Stats`
 * for the `path`.
 * @param {string | Buffer | URL} path
 * @param {{
 *   bigint?: boolean;
 *   throwIfNoEntry?: boolean;
 *   }} [options]
 * @returns {Stats}
 */
function statSync(path, options = { bigint: false, throwIfNoEntry: true }) {
  const stats = binding.stat(
    getValidatedPath(path),
    options.bigint,
    undefined,
    options.throwIfNoEntry,
  );
  if (stats === undefined) {
    return undefined;
  }
  return getStatsFromBinding(stats);
}

function statfsSync(path, options = { bigint: false }) {
  const stats = binding.statfs(getValidatedPath(path), options.bigint);
  return getStatFsFromBinding(stats);
}

/**
 * Reads the contents of a symbolic link
 * referred to by `path`.
 * @param {string | Buffer | URL} path
 * @param {{ encoding?: string; } | string} [options]
 * @param {(
 *   err?: Error,
 *   linkString?: string | Buffer
 *   ) => any} callback
 * @returns {void}
 */
function readlink(path, options, callback) {
  callback = makeCallback(typeof options === 'function' ? options : callback);
  options = getOptions(options);
  const req = new FSReqCallback();
  req.oncomplete = callback;
  binding.readlink(getValidatedPath(path), options.encoding, req);
}

/**
 * Synchronously reads the contents of a symbolic link
 * referred to by `path`.
 * @param {string | Buffer | URL} path
 * @param {{ encoding?: string; } | string} [options]
 * @returns {string | Buffer}
 */
function readlinkSync(path, options) {
  options = getOptions(options);
  return binding.readlink(getValidatedPath(path), options.encoding);
}

/**
 * Creates the link called `path` pointing to `target`.
 * @param {string | Buffer | URL} target
 * @param {string | Buffer | URL} path
 * @param {string | null} [type]
 * @param {(err?: Error) => any} callback
 * @returns {void}
 */
function symlink(target, path, type, callback) {
  if (callback === undefined) {
    callback = makeCallback(type);
    type = undefined;
  } else {
    validateOneOf(type, 'type', ['dir', 'file', 'junction', null, undefined]);
  }

  if (permission.isEnabled()) {
    // The permission model's security guarantees fall apart in the presence of
    // relative symbolic links. Thus, we have to prevent their creation.
    if (BufferIsBuffer(target)) {
      if (!isAbsolute(BufferToString(target))) {
        callback(new ERR_ACCESS_DENIED('relative symbolic link target'));
        return;
      }
    } else if (typeof target !== 'string' || !isAbsolute(toPathIfFileURL(target))) {
      callback(new ERR_ACCESS_DENIED('relative symbolic link target'));
      return;
    }
  }

  target = getValidatedPath(target, 'target');
  path = getValidatedPath(path);

  if (isWindows && type == null) {
    let absoluteTarget;
    try {
      // Symlinks targets can be relative to the newly created path.
      // Calculate absolute file name of the symlink target, and check
      // if it is a directory. Ignore resolve error to keep symlink
      // errors consistent between platforms if invalid path is
      // provided.
      absoluteTarget = pathModule.resolve(path, '..', target);
    } catch {
      // Continue regardless of error.
    }
    if (absoluteTarget !== undefined) {
      stat(absoluteTarget, (err, stat) => {
        const resolvedType = !err && stat.isDirectory() ? 'dir' : 'file';
        const resolvedFlags = stringToSymlinkType(resolvedType);
        const destination = preprocessSymlinkDestination(target,
                                                         resolvedType,
                                                         path);

        const req = new FSReqCallback();
        req.oncomplete = callback;
        binding.symlink(
          destination,
          path,
          resolvedFlags,
          req,
        );
      });
      return;
    }
  }

  const destination = preprocessSymlinkDestination(target, type, path);

  const flags = stringToSymlinkType(type);
  const req = new FSReqCallback();
  req.oncomplete = callback;
  binding.symlink(destination, path, flags, req);
}

/**
 * Synchronously creates the link called `path`
 * pointing to `target`.
 * @param {string | Buffer | URL} target
 * @param {string | Buffer | URL} path
 * @param {string | null} [type]
 * @returns {void}
 */
function symlinkSync(target, path, type) {
  validateOneOf(type, 'type', ['dir', 'file', 'junction', null, undefined]);
  if (isWindows && type == null) {
    const absoluteTarget = pathModule.resolve(`${path}`, '..', `${target}`);
    if (statSync(absoluteTarget, { throwIfNoEntry: false })?.isDirectory()) {
      type = 'dir';
    }
  }

  if (permission.isEnabled()) {
    // The permission model's security guarantees fall apart in the presence of
    // relative symbolic links. Thus, we have to prevent their creation.
    if (BufferIsBuffer(target)) {
      if (!isAbsolute(BufferToString(target))) {
        throw new ERR_ACCESS_DENIED('relative symbolic link target');
      }
    } else if (typeof target !== 'string' || !isAbsolute(toPathIfFileURL(target))) {
      throw new ERR_ACCESS_DENIED('relative symbolic link target');
    }
  }

  target = getValidatedPath(target, 'target');
  path = getValidatedPath(path);

  binding.symlink(
    preprocessSymlinkDestination(target, type, path),
    path,
    stringToSymlinkType(type),
  );
}

/**
 * Creates a new link from the `existingPath`
 * to the `newPath`.
 * @param {string | Buffer | URL} existingPath
 * @param {string | Buffer | URL} newPath
 * @param {(err?: Error) => any} callback
 * @returns {void}
 */
function link(existingPath, newPath, callback) {
  callback = makeCallback(callback);

  existingPath = getValidatedPath(existingPath, 'existingPath');
  newPath = getValidatedPath(newPath, 'newPath');

  const req = new FSReqCallback();
  req.oncomplete = callback;

  binding.link(existingPath, newPath, req);
}

/**
 * Synchronously creates a new link from the `existingPath`
 * to the `newPath`.
 * @param {string | Buffer | URL} existingPath
 * @param {string | Buffer | URL} newPath
 * @returns {void}
 */
function linkSync(existingPath, newPath) {
  existingPath = getValidatedPath(existingPath, 'existingPath');
  newPath = getValidatedPath(newPath, 'newPath');

  binding.link(
    existingPath,
    newPath,
  );
}

/**
 * Asynchronously removes a file or symbolic link.
 * @param {string | Buffer | URL} path
 * @param {(err?: Error) => any} callback
 * @returns {void}
 */
function unlink(path, callback) {
  callback = makeCallback(callback);
  const req = new FSReqCallback();
  req.oncomplete = callback;
  binding.unlink(getValidatedPath(path), req);
}

/**
 * Synchronously removes a file or symbolic link.
 * @param {string | Buffer | URL} path
 * @returns {void}
 */
function unlinkSync(path) {
  binding.unlink(getValidatedPath(path));
}

/**
 * Sets the permissions on the file.
 * @param {number} fd
 * @param {string | number} mode
 * @param {(err?: Error) => any} callback
 * @returns {void}
 */
function fchmod(fd, mode, callback) {
  mode = parseFileMode(mode, 'mode');
  callback = makeCallback(callback);

  if (permission.isEnabled()) {
    callback(new ERR_ACCESS_DENIED('fchmod API is disabled when Permission Model is enabled.'));
    return;
  }

  const req = new FSReqCallback();
  req.oncomplete = callback;
  binding.fchmod(fd, mode, req);
}

/**
 * Synchronously sets the permissions on the file.
 * @param {number} fd
 * @param {string | number} mode
 * @returns {void}
 */
function fchmodSync(fd, mode) {
  if (permission.isEnabled()) {
    throw new ERR_ACCESS_DENIED('fchmod API is disabled when Permission Model is enabled.');
  }
  binding.fchmod(
    fd,
    parseFileMode(mode, 'mode'),
  );
}

/**
 * Changes the permissions on a symbolic link.
 * @param {string | Buffer | URL} path
 * @param {number} mode
 * @param {(err?: Error) => any} callback
 * @returns {void}
 */
function lchmod(path, mode, callback) {
  validateFunction(callback, 'cb');
  mode = parseFileMode(mode, 'mode');
  fs.open(path, O_WRONLY | O_SYMLINK, (err, fd) => {
    if (err) {
      callback(err);
      return;
    }
    // Prefer to return the chmod error, if one occurs,
    // but still try to close, and report closing errors if they occur.
    fs.fchmod(fd, mode, (err) => {
      fs.close(fd, (err2) => {
        callback(aggregateTwoErrors(err2, err));
      });
    });
  });
}

/**
 * Synchronously changes the permissions on a symbolic link.
 * @param {string | Buffer | URL} path
 * @param {number} mode
 * @returns {void}
 */
function lchmodSync(path, mode) {
  const fd = fs.openSync(path, O_WRONLY | O_SYMLINK);

  // Prefer to return the chmod error, if one occurs,
  // but still try to close, and report closing errors if they occur.
  try {
    fs.fchmodSync(fd, mode);
  } finally {
    fs.closeSync(fd);
  }
}

/**
 * Asynchronously changes the permissions of a file.
 * @param {string | Buffer | URL} path
 * @param {string | number} mode
 * @param {(err?: Error) => any} callback
 * @returns {void}
 */
function chmod(path, mode, callback) {
  path = getValidatedPath(path);
  mode = parseFileMode(mode, 'mode');
  callback = makeCallback(callback);

  const req = new FSReqCallback();
  req.oncomplete = callback;
  binding.chmod(path, mode, req);
}

/**
 * Synchronously changes the permissions of a file.
 * @param {string | Buffer | URL} path
 * @param {string | number} mode
 * @returns {void}
 */
function chmodSync(path, mode) {
  path = getValidatedPath(path);
  mode = parseFileMode(mode, 'mode');

  binding.chmod(path, mode);
}

/**
 * Sets the owner of the symbolic link.
 * @param {string | Buffer | URL} path
 * @param {number} uid
 * @param {number} gid
 * @param {(err?: Error) => any} callback
 * @returns {void}
 */
function lchown(path, uid, gid, callback) {
  callback = makeCallback(callback);
  path = getValidatedPath(path);
  validateInteger(uid, 'uid', -1, kMaxUserId);
  validateInteger(gid, 'gid', -1, kMaxUserId);
  const req = new FSReqCallback();
  req.oncomplete = callback;
  binding.lchown(path, uid, gid, req);
}

/**
 * Synchronously sets the owner of the symbolic link.
 * @param {string | Buffer | URL} path
 * @param {number} uid
 * @param {number} gid
 * @returns {void}
 */
function lchownSync(path, uid, gid) {
  path = getValidatedPath(path);
  validateInteger(uid, 'uid', -1, kMaxUserId);
  validateInteger(gid, 'gid', -1, kMaxUserId);
  binding.lchown(path, uid, gid);
}

/**
 * Sets the owner of the file.
 * @param {number} fd
 * @param {number} uid
 * @param {number} gid
 * @param {(err?: Error) => any} callback
 * @returns {void}
 */
function fchown(fd, uid, gid, callback) {
  validateInteger(uid, 'uid', -1, kMaxUserId);
  validateInteger(gid, 'gid', -1, kMaxUserId);
  callback = makeCallback(callback);
  if (permission.isEnabled()) {
    callback(new ERR_ACCESS_DENIED('fchown API is disabled when Permission Model is enabled.'));
    return;
  }

  const req = new FSReqCallback();
  req.oncomplete = callback;
  binding.fchown(fd, uid, gid, req);
}

/**
 * Synchronously sets the owner of the file.
 * @param {number} fd
 * @param {number} uid
 * @param {number} gid
 * @returns {void}
 */
function fchownSync(fd, uid, gid) {
  validateInteger(uid, 'uid', -1, kMaxUserId);
  validateInteger(gid, 'gid', -1, kMaxUserId);
  if (permission.isEnabled()) {
    throw new ERR_ACCESS_DENIED('fchown API is disabled when Permission Model is enabled.');
  }

  binding.fchown(fd, uid, gid);
}

/**
 * Asynchronously changes the owner and group
 * of a file.
 * @param {string | Buffer | URL} path
 * @param {number} uid
 * @param {number} gid
 * @param {(err?: Error) => any} callback
 * @returns {void}
 */
function chown(path, uid, gid, callback) {
  callback = makeCallback(callback);
  path = getValidatedPath(path);
  validateInteger(uid, 'uid', -1, kMaxUserId);
  validateInteger(gid, 'gid', -1, kMaxUserId);

  const req = new FSReqCallback();
  req.oncomplete = callback;
  binding.chown(path, uid, gid, req);
}

/**
 * Synchronously changes the owner and group
 * of a file.
 * @param {string | Buffer | URL} path
 * @param {number} uid
 * @param {number} gid
 * @returns {void}
 */
function chownSync(path, uid, gid) {
  path = getValidatedPath(path);
  validateInteger(uid, 'uid', -1, kMaxUserId);
  validateInteger(gid, 'gid', -1, kMaxUserId);
  binding.chown(path, uid, gid);
}

/**
 * Changes the file system timestamps of the object
 * referenced by `path`.
 * @param {string | Buffer | URL} path
 * @param {number | string | Date} atime
 * @param {number | string | Date} mtime
 * @param {(err?: Error) => any} callback
 * @returns {void}
 */
function utimes(path, atime, mtime, callback) {
  callback = makeCallback(callback);
  path = getValidatedPath(path);

  const req = new FSReqCallback();
  req.oncomplete = callback;
  binding.utimes(
    path,
    toUnixTimestamp(atime),
    toUnixTimestamp(mtime),
    req,
  );
}

/**
 * Synchronously changes the file system timestamps
 * of the object referenced by `path`.
 * @param {string | Buffer | URL} path
 * @param {number | string | Date} atime
 * @param {number | string | Date} mtime
 * @returns {void}
 */
function utimesSync(path, atime, mtime) {
  binding.utimes(
    getValidatedPath(path),
    toUnixTimestamp(atime),
    toUnixTimestamp(mtime),
  );
}

/**
 * Changes the file system timestamps of the object
 * referenced by the supplied `fd` (file descriptor).
 * @param {number} fd
 * @param {number | string | Date} atime
 * @param {number | string | Date} mtime
 * @param {(err?: Error) => any} callback
 * @returns {void}
 */
function futimes(fd, atime, mtime, callback) {
  atime = toUnixTimestamp(atime, 'atime');
  mtime = toUnixTimestamp(mtime, 'mtime');
  callback = makeCallback(callback);

  const req = new FSReqCallback();
  req.oncomplete = callback;
  binding.futimes(fd, atime, mtime, req);
}

/**
 * Synchronously changes the file system timestamps
 * of the object referenced by the
 * supplied `fd` (file descriptor).
 * @param {number} fd
 * @param {number | string | Date} atime
 * @param {number | string | Date} mtime
 * @returns {void}
 */
function futimesSync(fd, atime, mtime) {
  binding.futimes(
    fd,
    toUnixTimestamp(atime, 'atime'),
    toUnixTimestamp(mtime, 'mtime'),
  );
}

/**
 * Changes the access and modification times of
 * a file in the same way as `fs.utimes()`.
 * @param {string | Buffer | URL} path
 * @param {number | string | Date} atime
 * @param {number | string | Date} mtime
 * @param {(err?: Error) => any} callback
 * @returns {void}
 */
function lutimes(path, atime, mtime, callback) {
  callback = makeCallback(callback);
  path = getValidatedPath(path);

  const req = new FSReqCallback();
  req.oncomplete = callback;
  binding.lutimes(
    path,
    toUnixTimestamp(atime),
    toUnixTimestamp(mtime),
    req,
  );
}

/**
 * Synchronously changes the access and modification
 * times of a file in the same way as `fs.utimesSync()`.
 * @param {string | Buffer | URL} path
 * @param {number | string | Date} atime
 * @param {number | string | Date} mtime
 * @returns {void}
 */
function lutimesSync(path, atime, mtime) {
  binding.lutimes(
    getValidatedPath(path),
    toUnixTimestamp(atime),
    toUnixTimestamp(mtime),
  );
}

function writeAll(fd, isUserFd, buffer, offset, length, signal, flush, callback) {
  if (signal?.aborted) {
    const abortError = new AbortError(undefined, { cause: signal.reason });
    if (isUserFd) {
      callback(abortError);
    } else {
      fs.close(fd, (err) => {
        callback(aggregateTwoErrors(err, abortError));
      });
    }
    return;
  }
  // write(fd, buffer, offset, length, position, callback)
  fs.write(fd, buffer, offset, length, null, (writeErr, written) => {
    if (writeErr) {
      if (isUserFd) {
        callback(writeErr);
      } else {
        fs.close(fd, (err) => {
          callback(aggregateTwoErrors(err, writeErr));
        });
      }
    } else if (written === length) {
      if (!flush) {
        if (isUserFd) {
          callback(null);
        } else {
          fs.close(fd, callback);
        }
      } else {
        fs.fsync(fd, (syncErr) => {
          if (syncErr) {
            if (isUserFd) {
              callback(syncErr);
            } else {
              fs.close(fd, (err) => {
                callback(aggregateTwoErrors(err, syncErr));
              });
            }
          } else if (isUserFd) {
            callback(null);
          } else {
            fs.close(fd, callback);
          }
        });
      }
    } else {
      offset += written;
      length -= written;
      writeAll(fd, isUserFd, buffer, offset, length, signal, flush, callback);
    }
  });
}

/**
 * Asynchronously writes data to the file.
 * @param {string | Buffer | URL | number} path
 * @param {string | Buffer | TypedArray | DataView} data
 * @param {{
 *   encoding?: string | null;
 *   mode?: number;
 *   flag?: string;
 *   signal?: AbortSignal;
 *   flush?: boolean;
 *   } | string} [options]
 * @param {(err?: Error) => any} callback
 * @returns {void}
 */
function writeFile(path, data, options, callback) {
  callback ||= options;
  validateFunction(callback, 'cb');
  options = getOptions(options, {
    encoding: 'utf8',
    mode: 0o666,
    flag: 'w',
    flush: false,
  });
  const flag = options.flag || 'w';
  const flush = options.flush ?? false;

  validateBoolean(flush, 'options.flush');

  if (!isArrayBufferView(data)) {
    validateStringAfterArrayBufferView(data, 'data');
    data = Buffer.from(data, options.encoding || 'utf8');
  }

  if (isFd(path)) {
    const isUserFd = true;
    const signal = options.signal;
    writeAll(path, isUserFd, data, 0, data.byteLength, signal, flush, callback);
    return;
  }

  if (checkAborted(options.signal, callback))
    return;

  fs.open(path, flag, options.mode, (openErr, fd) => {
    if (openErr) {
      callback(openErr);
    } else {
      const isUserFd = false;
      const signal = options.signal;
      writeAll(fd, isUserFd, data, 0, data.byteLength, signal, flush, callback);
    }
  });
}

/**
 * Synchronously writes data to the file.
 * @param {string | Buffer | URL | number} path
 * @param {string | Buffer | TypedArray | DataView} data
 * @param {{
 *   encoding?: string | null;
 *   mode?: number;
 *   flag?: string;
 *   flush?: boolean;
 *   } | string} [options]
 * @returns {void}
 */
function writeFileSync(path, data, options) {
  options = getOptions(options, {
    encoding: 'utf8',
    mode: 0o666,
    flag: 'w',
    flush: false,
  });

  const flush = options.flush ?? false;

  validateBoolean(flush, 'options.flush');

  const flag = options.flag || 'w';

  // C++ fast path for string data and UTF8 encoding
  if (typeof data === 'string' && (options.encoding === 'utf8' || options.encoding === 'utf-8')) {
    if (!isInt32(path)) {
      path = getValidatedPath(path);
    }

    return binding.writeFileUtf8(
      path,
      data,
      stringToFlags(flag),
      parseFileMode(options.mode, 'mode', 0o666),
    );
  }

  if (!isArrayBufferView(data)) {
    validateStringAfterArrayBufferView(data, 'data');
    data = Buffer.from(data, options.encoding || 'utf8');
  }

  const isUserFd = isFd(path); // File descriptor ownership
  const fd = isUserFd ? path : fs.openSync(path, flag, options.mode);

  let offset = 0;
  let length = data.byteLength;
  try {
    while (length > 0) {
      const written = fs.writeSync(fd, data, offset, length);
      offset += written;
      length -= written;
    }

    if (flush) {
      fs.fsyncSync(fd);
    }
  } finally {
    if (!isUserFd) fs.closeSync(fd);
  }
}

/**
 * Asynchronously appends data to a file.
 * @param {string | Buffer | URL | number} path
 * @param {string | Buffer} data
 * @param {{
 *   encoding?: string | null;
 *   mode?: number;
 *   flag?: string;
 *   flush?: boolean;
 *   } | string} [options]
 * @param {(err?: Error) => any} callback
 * @returns {void}
 */
function appendFile(path, data, options, callback) {
  callback ||= options;
  validateFunction(callback, 'cb');
  options = getOptions(options, { encoding: 'utf8', mode: 0o666, flag: 'a' });

  // Don't make changes directly on options object
  options = copyObject(options);

  // Force append behavior when using a supplied file descriptor
  if (!options.flag || isFd(path))
    options.flag = 'a';

  fs.writeFile(path, data, options, callback);
}

/**
 * Synchronously appends data to a file.
 * @param {string | Buffer | URL | number} path
 * @param {string | Buffer} data
 * @param {{
 *   encoding?: string | null;
 *   mode?: number;
 *   flag?: string;
 *   } | string} [options]
 * @returns {void}
 */
function appendFileSync(path, data, options) {
  options = getOptions(options, { encoding: 'utf8', mode: 0o666, flag: 'a' });

  // Don't make changes directly on options object
  options = copyObject(options);

  // Force append behavior when using a supplied file descriptor
  if (!options.flag || isFd(path))
    options.flag = 'a';

  fs.writeFileSync(path, data, options);
}

/**
 * Watches for the changes on `filename`.
 * @param {string | Buffer | URL} filename
 * @param {string | {
 *   persistent?: boolean;
 *   recursive?: boolean;
 *   encoding?: string;
 *   signal?: AbortSignal;
 *   }} [options]
 * @param {(
 *   eventType?: string,
 *   filename?: string | Buffer
 *   ) => any} [listener]
 * @returns {watchers.FSWatcher}
 */
function watch(filename, options, listener) {
  if (typeof options === 'function') {
    listener = options;
  }
  options = getOptions(options);

  // Don't make changes directly on options object
  options = copyObject(options);

  if (options.persistent === undefined) options.persistent = true;
  if (options.recursive === undefined) options.recursive = false;

  let watcher;
  const watchers = require('internal/fs/watchers');
  const path = getValidatedPath(filename);
  // TODO(anonrig): Remove non-native watcher when/if libuv supports recursive.
  // As of November 2022, libuv does not support recursive file watch on all platforms,
  // e.g. Linux due to the limitations of inotify.
  if (options.recursive && !isMacOS && !isWindows) {
    const nonNativeWatcher = require('internal/fs/recursive_watch');
    watcher = new nonNativeWatcher.FSWatcher(options);
    watcher[watchers.kFSWatchStart](path);
  } else {
    watcher = new watchers.FSWatcher();
    watcher[watchers.kFSWatchStart](path,
                                    options.persistent,
                                    options.recursive,
                                    options.encoding);
  }

  if (listener) {
    watcher.addListener('change', listener);
  }
  if (options.signal) {
    if (options.signal.aborted) {
      process.nextTick(() => watcher.close());
    } else {
      const listener = () => watcher.close();
      kResistStopPropagation ??= require('internal/event_target').kResistStopPropagation;
      options.signal.addEventListener('abort', listener, { __proto__: null, [kResistStopPropagation]: true });
      watcher.once('close', () => {
        options.signal.removeEventListener('abort', listener);
      });
    }
  }

  return watcher;
}


const statWatchers = new SafeMap();

/**
 * Watches for changes on `filename`.
 * @param {string | Buffer | URL} filename
 * @param {{
 *   bigint?: boolean;
 *   persistent?: boolean;
 *   interval?: number;
 *   }} [options]
 * @param {(
 *   current?: Stats,
 *   previous?: Stats
 *   ) => any} listener
 * @returns {watchers.StatWatcher}
 */
function watchFile(filename, options, listener) {
  filename = getValidatedPath(filename);
  filename = pathModule.resolve(filename);
  let stat;

  if (options === null || typeof options !== 'object') {
    listener = options;
    options = null;
  }

  options = {
    // Poll interval in milliseconds. 5007 is what libev used to use. It's
    // a little on the slow side but let's stick with it for now to keep
    // behavioral changes to a minimum.
    interval: 5007,
    persistent: true,
    ...options,
  };

  validateFunction(listener, 'listener');

  stat = statWatchers.get(filename);
  const watchers = require('internal/fs/watchers');
  if (stat === undefined) {
    stat = new watchers.StatWatcher(options.bigint);
    stat[watchers.kFSStatWatcherStart](filename,
                                       options.persistent, options.interval);
    statWatchers.set(filename, stat);
  } else {
    stat[watchers.kFSStatWatcherAddOrCleanRef]('add');
  }

  stat.addListener('change', listener);
  return stat;
}

/**
 * Stops watching for changes on `filename`.
 * @param {string | Buffer | URL} filename
 * @param {() => any} [listener]
 * @returns {void}
 */
function unwatchFile(filename, listener) {
  filename = getValidatedPath(filename);
  filename = pathModule.resolve(filename);
  const stat = statWatchers.get(filename);

  if (stat === undefined) return;
  const watchers = require('internal/fs/watchers');
  if (typeof listener === 'function') {
    const beforeListenerCount = stat.listenerCount('change');
    stat.removeListener('change', listener);
    if (stat.listenerCount('change') < beforeListenerCount)
      stat[watchers.kFSStatWatcherAddOrCleanRef]('clean');
  } else {
    stat.removeAllListeners('change');
    stat[watchers.kFSStatWatcherAddOrCleanRef]('cleanAll');
  }

  if (stat.listenerCount('change') === 0) {
    stat.stop();
    statWatchers.delete(filename);
  }
}


let splitRoot;
if (isWindows) {
  // Regex to find the device root on Windows (e.g. 'c:\\'), including trailing
  // slash.
  const splitRootRe = /^(?:[a-zA-Z]:|[\\/]{2}[^\\/]+[\\/][^\\/]+)?[\\/]*/;
  splitRoot = function splitRoot(str) {
    return SideEffectFreeRegExpPrototypeExec(splitRootRe, str)[0];
  };
} else {
  splitRoot = function splitRoot(str) {
    for (let i = 0; i < str.length; ++i) {
      if (StringPrototypeCharCodeAt(str, i) !== CHAR_FORWARD_SLASH)
        return StringPrototypeSlice(str, 0, i);
    }
    return str;
  };
}

function encodeRealpathResult(result, options) {
  if (!options || !options.encoding || options.encoding === 'utf8')
    return result;
  const asBuffer = Buffer.from(result);
  if (options.encoding === 'buffer') {
    return asBuffer;
  }
  return asBuffer.toString(options.encoding);
}

// Finds the next portion of a (partial) path, up to the next path delimiter
let nextPart;
if (isWindows) {
  nextPart = function nextPart(p, i) {
    for (; i < p.length; ++i) {
      const ch = StringPrototypeCharCodeAt(p, i);

      // Check for a separator character
      if (ch === CHAR_BACKWARD_SLASH || ch === CHAR_FORWARD_SLASH)
        return i;
    }
    return -1;
  };
} else {
  nextPart = function nextPart(p, i) {
    return StringPrototypeIndexOf(p, '/', i);
  };
}

/**
 * Returns the resolved pathname.
 * @param {string | Buffer | URL} p
 * @param {string | { encoding?: string | null; }} [options]
 * @returns {string | Buffer}
 */
function realpathSync(p, options) {
  options = getOptions(options);
  p = toPathIfFileURL(p);
  if (typeof p !== 'string') {
    p += '';
  }
  validatePath(p);
  p = pathModule.resolve(p);

  const cache = options[realpathCacheKey];
  const maybeCachedResult = cache?.get(p);
  if (maybeCachedResult) {
    return maybeCachedResult;
  }

  const seenLinks = new SafeMap();
  const knownHard = new SafeSet();
  const original = p;

  // Current character position in p
  let pos;
  // The partial path so far, including a trailing slash if any
  let current;
  // The partial path without a trailing slash (except when pointing at a root)
  let base;
  // The partial path scanned in the previous round, with slash
  let previous;

  // Skip over roots
  current = base = splitRoot(p);
  pos = current.length;

  // On windows, check that the root exists. On unix there is no need.
  if (isWindows) {
    const out = binding.lstat(base, false, undefined, true /* throwIfNoEntry */);
    if (out === undefined) {
      return;
    }
    knownHard.add(base);
  }

  // Walk down the path, swapping out linked path parts for their real
  // values
  // NB: p.length changes.
  while (pos < p.length) {
    // find the next part
    const result = nextPart(p, pos);
    previous = current;
    if (result === -1) {
      const last = StringPrototypeSlice(p, pos);
      current += last;
      base = previous + last;
      pos = p.length;
    } else {
      current += StringPrototypeSlice(p, pos, result + 1);
      base = previous + StringPrototypeSlice(p, pos, result);
      pos = result + 1;
    }

    // Continue if not a symlink, break if a pipe/socket
    if (knownHard.has(base) || cache?.get(base) === base) {
      if (isFileType(statValues, S_IFIFO) ||
          isFileType(statValues, S_IFSOCK)) {
        break;
      }
      continue;
    }

    let resolvedLink;
    const maybeCachedResolved = cache?.get(base);
    if (maybeCachedResolved) {
      resolvedLink = maybeCachedResolved;
    } else {
      // Use stats array directly to avoid creating an fs.Stats instance just
      // for our internal use.

      const stats = binding.lstat(base, true, undefined, true /* throwIfNoEntry */);
      if (stats === undefined) {
        return;
      }

      if (!isFileType(stats, S_IFLNK)) {
        knownHard.add(base);
        cache?.set(base, base);
        continue;
      }

      // Read the link if it wasn't read before
      // dev/ino always return 0 on windows, so skip the check.
      let linkTarget = null;
      let id;
      if (!isWindows) {
        const dev = BigIntPrototypeToString(stats[0], 32);
        const ino = BigIntPrototypeToString(stats[7], 32);
        id = `${dev}:${ino}`;
        if (seenLinks.has(id)) {
          linkTarget = seenLinks.get(id);
        }
      }
      if (linkTarget === null) {
        binding.stat(base, false, undefined, true);
        linkTarget = binding.readlink(base, undefined);
      }
      resolvedLink = pathModule.resolve(previous, linkTarget);

      cache?.set(base, resolvedLink);
      if (!isWindows) seenLinks.set(id, linkTarget);
    }

    // Resolve the link, then start over
    p = pathModule.resolve(resolvedLink, StringPrototypeSlice(p, pos));

    // Skip over roots
    current = base = splitRoot(p);
    pos = current.length;

    // On windows, check that the root exists. On unix there is no need.
    if (isWindows && !knownHard.has(base)) {
      const out = binding.lstat(base, false, undefined, true /* throwIfNoEntry */);
      if (out === undefined) {
        return;
      }
      knownHard.add(base);
    }
  }

  cache?.set(original, p);
  return encodeRealpathResult(p, options);
}

/**
 * Returns the resolved pathname.
 * @param {string | Buffer | URL} path
 * @param {string | { encoding?: string; }} [options]
 * @returns {string | Buffer}
 */
realpathSync.native = (path, options) => {
  options = getOptions(options);
  return binding.realpath(
    getValidatedPath(path),
    options.encoding,
  );
};

/**
 * Asynchronously computes the canonical pathname by
 * resolving `.`, `..` and symbolic links.
 * @param {string | Buffer | URL} p
 * @param {string | { encoding?: string; }} [options]
 * @param {(
 *   err?: Error,
 *   resolvedPath?: string | Buffer
 *   ) => any} callback
 * @returns {void}
 */
function realpath(p, options, callback) {
  if (typeof options === 'function') {
    callback = options;
  } else {
    validateFunction(callback, 'cb');
  }
  options = getOptions(options);
  p = toPathIfFileURL(p);

  if (typeof p !== 'string') {
    p += '';
  }
  validatePath(p);
  p = pathModule.resolve(p);

  const seenLinks = new SafeMap();
  const knownHard = new SafeSet();

  // Current character position in p
  let pos;
  // The partial path so far, including a trailing slash if any
  let current;
  // The partial path without a trailing slash (except when pointing at a root)
  let base;
  // The partial path scanned in the previous round, with slash
  let previous;

  current = base = splitRoot(p);
  pos = current.length;

  // On windows, check that the root exists. On unix there is no need.
  if (isWindows && !knownHard.has(base)) {
    fs.lstat(base, (err) => {
      if (err) return callback(err);
      knownHard.add(base);
      LOOP();
    });
  } else {
    process.nextTick(LOOP);
  }

  // Walk down the path, swapping out linked path parts for their real
  // values
  function LOOP() {
    // Stop if scanned past end of path
    if (pos >= p.length) {
      return callback(null, encodeRealpathResult(p, options));
    }

    // find the next part
    const result = nextPart(p, pos);
    previous = current;
    if (result === -1) {
      const last = StringPrototypeSlice(p, pos);
      current += last;
      base = previous + last;
      pos = p.length;
    } else {
      current += StringPrototypeSlice(p, pos, result + 1);
      base = previous + StringPrototypeSlice(p, pos, result);
      pos = result + 1;
    }

    // Continue if not a symlink, break if a pipe/socket
    if (knownHard.has(base)) {
      if (isFileType(statValues, S_IFIFO) ||
          isFileType(statValues, S_IFSOCK)) {
        return callback(null, encodeRealpathResult(p, options));
      }
      return process.nextTick(LOOP);
    }

    return fs.lstat(base, { bigint: true }, gotStat);
  }

  function gotStat(err, stats) {
    if (err) return callback(err);

    // If not a symlink, skip to the next path part
    if (!stats.isSymbolicLink()) {
      knownHard.add(base);
      return process.nextTick(LOOP);
    }

    // Stat & read the link if not read before.
    // Call `gotTarget()` as soon as the link target is known.
    // `dev`/`ino` always return 0 on windows, so skip the check.
    let id;
    if (!isWindows) {
      const dev = BigIntPrototypeToString(stats.dev, 32);
      const ino = BigIntPrototypeToString(stats.ino, 32);
      id = `${dev}:${ino}`;
      if (seenLinks.has(id)) {
        return gotTarget(null, seenLinks.get(id));
      }
    }
    fs.stat(base, (err) => {
      if (err) return callback(err);

      fs.readlink(base, (err, target) => {
        if (!isWindows) seenLinks.set(id, target);
        gotTarget(err, target);
      });
    });
  }

  function gotTarget(err, target) {
    if (err) return callback(err);

    gotResolvedLink(pathModule.resolve(previous, target));
  }

  function gotResolvedLink(resolvedLink) {
    // Resolve the link, then start over
    p = pathModule.resolve(resolvedLink, StringPrototypeSlice(p, pos));
    current = base = splitRoot(p);
    pos = current.length;

    // On windows, check that the root exists. On unix there is no need.
    if (isWindows && !knownHard.has(base)) {
      fs.lstat(base, (err) => {
        if (err) return callback(err);
        knownHard.add(base);
        LOOP();
      });
    } else {
      process.nextTick(LOOP);
    }
  }
}

/**
 * Asynchronously computes the canonical pathname by
 * resolving `.`, `..` and symbolic links.
 * @param {string | Buffer | URL} path
 * @param {string | { encoding?: string; }} [options]
 * @param {(
 *   err?: Error,
 *   resolvedPath?: string | Buffer
 *   ) => any} callback
 * @returns {void}
 */
realpath.native = (path, options, callback) => {
  callback = makeCallback(callback || options);
  options = getOptions(options);
  path = getValidatedPath(path);
  const req = new FSReqCallback();
  req.oncomplete = callback;
  binding.realpath(path, options.encoding, req);
};

/**
 * Creates a unique temporary directory.
 * @param {string | Buffer | URL} prefix
 * @param {string | { encoding?: string; }} [options]
 * @param {(
 *   err?: Error,
 *   directory?: string
 *   ) => any} callback
 * @returns {void}
 */
function mkdtemp(prefix, options, callback) {
  callback = makeCallback(typeof options === 'function' ? options : callback);
  options = getOptions(options);

  prefix = getValidatedPath(prefix, 'prefix');
  warnOnNonPortableTemplate(prefix);

  const req = new FSReqCallback();
  req.oncomplete = callback;
  binding.mkdtemp(prefix, options.encoding, req);
}

/**
 * Synchronously creates a unique temporary directory.
 * @param {string | Buffer | URL} prefix
 * @param {string | { encoding?: string; }} [options]
 * @returns {string}
 */
function mkdtempSync(prefix, options) {
  options = getOptions(options);

  prefix = getValidatedPath(prefix, 'prefix');
  warnOnNonPortableTemplate(prefix);
  return binding.mkdtemp(prefix, options.encoding);
}

/**
 * Asynchronously copies `src` to `dest`. By
 * default, `dest` is overwritten if it already exists.
 * @param {string | Buffer | URL} src
 * @param {string | Buffer | URL} dest
 * @param {number} [mode]
 * @param {(err?: Error) => any} callback
 * @returns {void}
 */
function copyFile(src, dest, mode, callback) {
  if (typeof mode === 'function') {
    callback = mode;
    mode = 0;
  }

  src = getValidatedPath(src, 'src');
  dest = getValidatedPath(dest, 'dest');
  callback = makeCallback(callback);

  const req = new FSReqCallback();
  req.oncomplete = callback;
  binding.copyFile(src, dest, mode, req);
}

/**
 * Synchronously copies `src` to `dest`. By
 * default, `dest` is overwritten if it already exists.
 * @param {string | Buffer | URL} src
 * @param {string | Buffer | URL} dest
 * @param {number} [mode]
 * @returns {void}
 */
function copyFileSync(src, dest, mode) {
  binding.copyFile(
    getValidatedPath(src, 'src'),
    getValidatedPath(dest, 'dest'),
    mode,
  );
}

/**
 * Asynchronously copies `src` to `dest`. `src` can be a file, directory, or
 * symlink. The contents of directories will be copied recursively.
 * @param {string | URL} src
 * @param {string | URL} dest
 * @param {object} [options]
 * @param {(err?: Error) => any} callback
 * @returns {void}
 */
function cp(src, dest, options, callback) {
  if (typeof options === 'function') {
    callback = options;
    options = undefined;
  }
  callback = makeCallback(callback);
  options = validateCpOptions(options);
  src = getValidatedPath(src, 'src');
  dest = getValidatedPath(dest, 'dest');
  lazyLoadCp();
  cpFn(src, dest, options, callback);
}

/**
 * Synchronously copies `src` to `dest`. `src` can be a file, directory, or
 * symlink. The contents of directories will be copied recursively.
 * @param {string | URL} src
 * @param {string | URL} dest
 * @param {object} [options]
 * @returns {void}
 */
function cpSync(src, dest, options) {
  options = validateCpOptions(options);
  src = getValidatedPath(src, 'src');
  dest = getValidatedPath(dest, 'dest');
  lazyLoadCp();
  cpSyncFn(src, dest, options);
}

function lazyLoadStreams() {
  if (!ReadStream) {
    ({ ReadStream, WriteStream } = require('internal/fs/streams'));
    FileReadStream = ReadStream;
    FileWriteStream = WriteStream;
  }
}

/**
 * Creates a readable stream with a default `highWaterMark`
 * of 64 KiB.
 * @param {string | Buffer | URL} path
 * @param {string | {
 *   flags?: string;
 *   encoding?: string;
 *   fd?: number | FileHandle;
 *   mode?: number;
 *   autoClose?: boolean;
 *   emitClose?: boolean;
 *   start: number;
 *   end?: number;
 *   highWaterMark?: number;
 *   fs?: object | null;
 *   signal?: AbortSignal | null;
 *   }} [options]
 * @returns {ReadStream}
 */
function createReadStream(path, options) {
  lazyLoadStreams();
  return new ReadStream(path, options);
}

/**
 * Creates a write stream.
 * @param {string | Buffer | URL} path
 * @param {string | {
 *   flags?: string;
 *   encoding?: string;
 *   fd?: number | FileHandle;
 *   mode?: number;
 *   autoClose?: boolean;
 *   emitClose?: boolean;
 *   start: number;
 *   fs?: object | null;
 *   signal?: AbortSignal | null;
 *   highWaterMark?: number;
 *   flush?: boolean;
 *   }} [options]
 * @returns {WriteStream}
 */
function createWriteStream(path, options) {
  lazyLoadStreams();
  return new WriteStream(path, options);
}

const lazyGlob = getLazy(() => require('internal/fs/glob').Glob);

function glob(pattern, options, callback) {
  emitExperimentalWarning('glob');
  if (typeof options === 'function') {
    callback = options;
    options = undefined;
  }
  callback = makeCallback(callback);

  const Glob = lazyGlob();
  // TODO: Use iterator helpers when available
  (async () => {
    try {
      const res = await ArrayFromAsync(new Glob(pattern, options).glob());
      callback(null, res);
    } catch (err) {
      callback(err);
    }
  })();
}

function globSync(pattern, options) {
  emitExperimentalWarning('globSync');
  const Glob = lazyGlob();
  return new Glob(pattern, options).globSync();
}


module.exports = fs = {
  appendFile,
  appendFileSync,
  access,
  accessSync,
  chown,
  chownSync,
  chmod,
  chmodSync,
  close,
  closeSync,
  copyFile,
  copyFileSync,
  cp,
  cpSync,
  createReadStream,
  createWriteStream,
  exists,
  existsSync,
  fchown,
  fchownSync,
  fchmod,
  fchmodSync,
  fdatasync,
  fdatasyncSync,
  fstat,
  fstatSync,
  fsync,
  fsyncSync,
  ftruncate,
  ftruncateSync,
  futimes,
  futimesSync,
  glob,
  globSync,
  lchown,
  lchownSync,
  lchmod: constants.O_SYMLINK !== undefined ? lchmod : undefined,
  lchmodSync: constants.O_SYMLINK !== undefined ? lchmodSync : undefined,
  link,
  linkSync,
  lstat,
  lstatSync,
  lutimes,
  lutimesSync,
  mkdir,
  mkdirSync,
  mkdtemp,
  mkdtempSync,
  open,
  openSync,
  openAsBlob,
  readdir,
  readdirSync,
  read,
  readSync,
  readv,
  readvSync,
  readFile,
  readFileSync,
  readlink,
  readlinkSync,
  realpath,
  realpathSync,
  rename,
  renameSync,
  rm,
  rmSync,
  rmdir,
  rmdirSync,
  stat,
  statfs,
  statSync,
  statfsSync,
  symlink,
  symlinkSync,
  truncate,
  truncateSync,
  unwatchFile,
  unlink,
  unlinkSync,
  utimes,
  utimesSync,
  watch,
  watchFile,
  writeFile,
  writeFileSync,
  write,
  writeSync,
  writev,
  writevSync,
  Dirent,
  Stats,

  get ReadStream() {
    lazyLoadStreams();
    return ReadStream;
  },

  set ReadStream(val) {
    ReadStream = val;
  },

  get WriteStream() {
    lazyLoadStreams();
    return WriteStream;
  },

  set WriteStream(val) {
    WriteStream = val;
  },

  // Legacy names... these have to be separate because of how graceful-fs
  // (and possibly other) modules monkey patch the values.
  get FileReadStream() {
    lazyLoadStreams();
    return FileReadStream;
  },

  set FileReadStream(val) {
    FileReadStream = val;
  },

  get FileWriteStream() {
    lazyLoadStreams();
    return FileWriteStream;
  },

  set FileWriteStream(val) {
    FileWriteStream = val;
  },

  // For tests
  _toUnixTimestamp: toUnixTimestamp,
};

defineLazyProperties(
  fs,
  'internal/fs/dir',
  ['Dir', 'opendir', 'opendirSync'],
);

ObjectDefineProperties(fs, {
  F_OK: {
    __proto__: null,
    enumerable: false,
    get: deprecate(
      function get() {
        return F_OK || 0;
      },
      'fs.F_OK is deprecated, use fs.constants.F_OK instead',
      'DEP0176',
    ),
  },
  R_OK: {
    __proto__: null,
    enumerable: false,
    get: deprecate(
      function get() {
        return R_OK || 0;
      },
      'fs.R_OK is deprecated, use fs.constants.R_OK instead',
      'DEP0176',
    ),
  },
  W_OK: {
    __proto__: null,
    enumerable: false,
    get: deprecate(
      function get() {
        return W_OK || 0;
      },
      'fs.W_OK is deprecated, use fs.constants.W_OK instead',
      'DEP0176',
    ),
  },
  X_OK: {
    __proto__: null,
    enumerable: false,
    get: deprecate(
      function get() {
        return X_OK || 0;
      },
      'fs.X_OK is deprecated, use fs.constants.X_OK instead',
      'DEP0176',
    ),
  },
  constants: {
    __proto__: null,
    configurable: false,
    enumerable: true,
    value: constants,
  },
  promises: {
    __proto__: null,
    configurable: true,
    enumerable: true,
    get() {
      promises ??= require('internal/fs/promises').exports;
      return promises;
    },
  },
});
