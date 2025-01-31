'use strict';

const {
  ArrayPrototypePush,
  ArrayPrototypeShift,
  FunctionPrototypeBind,
  ObjectDefineProperty,
  PromiseReject,
  SymbolAsyncIterator,
} = primordials;

const pathModule = require('path');
const binding = internalBinding('fs');
const dirBinding = internalBinding('fs_dir');
const {
  codes: {
    ERR_DIR_CLOSED,
    ERR_DIR_CONCURRENT_OPERATION,
    ERR_MISSING_ARGS,
  },
} = require('internal/errors');

const { FSReqCallback } = binding;
const internalUtil = require('internal/util');
const {
  getDirent,
  getOptions,
  getValidatedPath,
} = require('internal/fs/utils');
const {
  validateFunction,
  validateUint32,
} = require('internal/validators');

class Dir {
  #handle;
  #path;
  #bufferedEntries = [];
  #closed = false;
  #options;
  #readPromisified;
  #closePromisified;
  // Either `null` or an Array of pending operations (= functions to be called
  // once the current operation is done).
  #operationQueue = null;

  constructor(handle, path, options) {
    if (handle == null) throw new ERR_MISSING_ARGS('handle');
    this.#handle = handle;
    this.#path = path;
    this.#options = {
      bufferSize: 32,
      ...getOptions(options, {
        encoding: 'utf8',
      }),
    };

    validateUint32(this.#options.bufferSize, 'options.bufferSize', true);

    this.#readPromisified = FunctionPrototypeBind(
      internalUtil.promisify(this.#readImpl), this, false);
    this.#closePromisified = FunctionPrototypeBind(
      internalUtil.promisify(this.close), this);
  }

  get path() {
    return this.#path;
  }

  read(callback) {
    return this.#readImpl(true, callback);
  }

  #readImpl(maybeSync, callback) {
    if (this.#closed === true) {
      throw new ERR_DIR_CLOSED();
    }

    if (callback === undefined) {
      return this.#readPromisified();
    }

    validateFunction(callback, 'callback');

    if (this.#operationQueue !== null) {
      ArrayPrototypePush(this.#operationQueue, () => {
        this.#readImpl(maybeSync, callback);
      });
      return;
    }

    if (this.#bufferedEntries.length > 0) {
      try {
        const dirent = ArrayPrototypeShift(this.#bufferedEntries);

        if (this.#options.recursive && dirent.isDirectory()) {
          this.readSyncRecursive(dirent);
        }

        if (maybeSync)
          process.nextTick(callback, null, dirent);
        else
          callback(null, dirent);
        return;
      } catch (error) {
        return callback(error);
      }
    }

    const req = new FSReqCallback();
    req.oncomplete = (err, result) => {
      process.nextTick(() => {
        const queue = this.#operationQueue;
        this.#operationQueue = null;
        for (const op of queue) op();
      });

      if (err || result === null) {
        return callback(err, result);
      }

      try {
        this.processReadResult(this.#path, result);
        const dirent = ArrayPrototypeShift(this.#bufferedEntries);
        if (this.#options.recursive && dirent.isDirectory()) {
          this.readSyncRecursive(dirent);
        }
        callback(null, dirent);
      } catch (error) {
        callback(error);
      }
    };

    this.#operationQueue = [];
    this.#handle.read(
      this.#options.encoding,
      this.#options.bufferSize,
      req,
    );
  }

  processReadResult(path, result) {
    for (let i = 0; i < result.length; i += 2) {
      ArrayPrototypePush(
        this.#bufferedEntries,
        getDirent(
          path,
          result[i],
          result[i + 1],
        ),
      );
    }
  }

  readSyncRecursive(dirent) {
    const path = pathModule.join(dirent.parentPath, dirent.name);
    const handle = dirBinding.opendir(
      path,
      this.#options.encoding,
    );

    // Terminate early, since it's already thrown.
    if (handle === undefined) {
      return;
    }

    // Fully read the directory and buffer the entries.
    // This is a naive solution and for very large sub-directories
    // it can even block the event loop. Essentially, `bufferSize` is
    // not respected for recursive mode. This is a known limitation.
    // Issue to fix: https://github.com/nodejs/node/issues/55764
    let result;
    while ((result = handle.read(
      this.#options.encoding,
      this.#options.bufferSize,
    ))) {
      this.processReadResult(path, result);
    }

    handle.close();
  }

  readSync() {
    if (this.#closed === true) {
      throw new ERR_DIR_CLOSED();
    }

    if (this.#operationQueue !== null) {
      throw new ERR_DIR_CONCURRENT_OPERATION();
    }

    if (this.#bufferedEntries.length > 0) {
      const dirent = ArrayPrototypeShift(this.#bufferedEntries);
      if (this.#options.recursive && dirent.isDirectory()) {
        this.readSyncRecursive(dirent);
      }
      return dirent;
    }

    const result = this.#handle.read(
      this.#options.encoding,
      this.#options.bufferSize,
    );

    if (result === null) {
      return result;
    }

    this.processReadResult(this.#path, result);

    const dirent = ArrayPrototypeShift(this.#bufferedEntries);
    if (this.#options.recursive && dirent.isDirectory()) {
      this.readSyncRecursive(dirent);
    }
    return dirent;
  }

  close(callback) {
    // Promise
    if (callback === undefined) {
      if (this.#closed === true) {
        return PromiseReject(new ERR_DIR_CLOSED());
      }
      return this.#closePromisified();
    }

    // callback
    validateFunction(callback, 'callback');

    if (this.#closed === true) {
      process.nextTick(callback, new ERR_DIR_CLOSED());
      return;
    }

    if (this.#operationQueue !== null) {
      ArrayPrototypePush(this.#operationQueue, () => {
        this.close(callback);
      });
      return;
    }

    this.#closed = true;
    const req = new FSReqCallback();
    req.oncomplete = callback;
    this.#handle.close(req);
  }

  closeSync() {
    if (this.#closed === true) {
      throw new ERR_DIR_CLOSED();
    }

    if (this.#operationQueue !== null) {
      throw new ERR_DIR_CONCURRENT_OPERATION();
    }

    this.#closed = true;
    this.#handle.close();
  }

  async* entries() {
    try {
      while (true) {
        const result = await this.#readPromisified();
        if (result === null) {
          break;
        }
        yield result;
      }
    } finally {
      await this.#closePromisified();
    }
  }
}

ObjectDefineProperty(Dir.prototype, SymbolAsyncIterator, {
  __proto__: null,
  value: Dir.prototype.entries,
  enumerable: false,
  writable: true,
  configurable: true,
});

function opendir(path, options, callback) {
  callback = typeof options === 'function' ? options : callback;
  validateFunction(callback, 'callback');

  path = getValidatedPath(path);
  options = getOptions(options, {
    encoding: 'utf8',
  });

  function opendirCallback(error, handle) {
    if (error) {
      callback(error);
    } else {
      callback(null, new Dir(handle, path, options));
    }
  }

  const req = new FSReqCallback();
  req.oncomplete = opendirCallback;

  dirBinding.opendir(
    path,
    options.encoding,
    req,
  );
}

function opendirSync(path, options) {
  path = getValidatedPath(path);
  options = getOptions(options, { encoding: 'utf8' });

  const handle = dirBinding.opendirSync(path);

  return new Dir(handle, path, options);
}

module.exports = {
  Dir,
  opendir,
  opendirSync,
};
