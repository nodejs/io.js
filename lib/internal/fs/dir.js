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
  #operationQueue = null;
  #handlerQueue = [];

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

  #processHandlerQueue() {
    while (this.#handlerQueue.length > 0) {
      const handler = ArrayPrototypeShift(this.#handlerQueue);
      const { handle, path } = handler;

      const result = handle.read(
        this.#options.encoding,
        this.#options.bufferSize,
      );

      if (result !== null) {
        this.processReadResult(path, result);
        if (result.length > 0) {
          ArrayPrototypePush(this.#handlerQueue, handler);
        }
      } else {
        handle.close();
      }

      if (this.#bufferedEntries.length > 0) {
        break;
      }
    }

    return this.#bufferedEntries.length > 0;
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

    if (this.#processHandlerQueue()) {
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

    if (handle === undefined) {
      return;
    }

    ArrayPrototypePush(this.#handlerQueue, { handle, path });
  }

  readSync() {
    if (this.#closed === true) {
      throw new ERR_DIR_CLOSED();
    }

    if (this.#operationQueue !== null) {
      throw new ERR_DIR_CONCURRENT_OPERATION();
    }

    if (this.#processHandlerQueue()) {
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
    if (callback === undefined) {
      if (this.#closed === true) {
        return PromiseReject(new ERR_DIR_CLOSED());
      }
      return this.#closePromisified();
    }

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

    while (this.#handlerQueue.length > 0) {
      const handler = ArrayPrototypeShift(this.#handlerQueue);
      handler.handle.close();
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

    while (this.#handlerQueue.length > 0) {
      const handler = ArrayPrototypeShift(this.#handlerQueue);
      handler.handle.close();
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
