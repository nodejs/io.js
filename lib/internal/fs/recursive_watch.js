'use strict';

const {
  Promise,
  SafeMap,
  SafeSet,
  StringPrototypeStartsWith,
  SymbolAsyncIterator,
} = primordials;

const { EventEmitter } = require('events');
const assert = require('internal/assert');
const {
  AbortError,
  codes: {
    ERR_INVALID_ARG_VALUE,
  },
} = require('internal/errors');
const { getValidatedPath } = require('internal/fs/utils');
const { kFSWatchStart, StatWatcher } = require('internal/fs/watchers');
const { kEmptyObject } = require('internal/util');
const { validateBoolean, validateAbortSignal } = require('internal/validators');
const {
  basename: pathBasename,
  join: pathJoin,
  relative: pathRelative,
  resolve: pathResolve,
} = require('path');

let internalSync;

function lazyLoadFsSync() {
  internalSync ??= require('fs');
  return internalSync;
}

let kResistStopPropagation;

class FSWatcher extends EventEmitter {
  #options = null;
  #closed = false;
  #files = new SafeMap();
  #watchers = new SafeMap();
  #symbolicFiles = new SafeSet();
  #rootPath = pathResolve();
  #watchingFile = false;

  constructor(options = kEmptyObject) {
    super();

    assert(typeof options === 'object');

    const { persistent, recursive, signal, encoding } = options;

    // TODO(anonrig): Add non-recursive support to non-native-watcher for IBMi & AIX support.
    if (recursive != null) {
      validateBoolean(recursive, 'options.recursive');
    }

    if (persistent != null) {
      validateBoolean(persistent, 'options.persistent');
    }

    if (signal != null) {
      validateAbortSignal(signal, 'options.signal');
    }

    if (encoding != null) {
      // This is required since on macOS and Windows it throws ERR_INVALID_ARG_VALUE
      if (typeof encoding !== 'string') {
        throw new ERR_INVALID_ARG_VALUE('options.encoding', encoding);
      }
    }

    this.#options = { persistent, recursive, signal, encoding };
  }

  close() {
    if (this.#closed) {
      return;
    }

    this.#closed = true;

    for (const file of this.#files.keys()) {
      this.#watchers.get(file)?.close();
      this.#watchers.delete(file);
    }

    this.#files.clear();
    this.#symbolicFiles.clear();
    this.emit('close');
  }

  #unwatchFiles(file) {
    this.#symbolicFiles.delete(file);

    for (const filename of this.#files.keys()) {
      if (StringPrototypeStartsWith(filename, file)) {
        this.#files.delete(filename);
        this.#watchers.get(filename)?.close();
        this.#watchers.delete(filename);
      }
    }
  }

  #watchFolder(folder) {
    const { readdirSync } = lazyLoadFsSync();

    try {
      const files = readdirSync(folder, {
        withFileTypes: true,
      });

      for (const file of files) {
        if (this.#closed) {
          break;
        }

        const f = pathJoin(folder, file.name);

        if (!this.#files.has(f)) {
          this.emit('change', 'rename', pathRelative(this.#rootPath, f));

          if (file.isSymbolicLink()) {
            this.#symbolicFiles.add(f);
          }

          try {
            this.#watchFile(f);
            if (file.isDirectory() && !file.isSymbolicLink()) {
              this.#watchFolder(f);
            }
          } catch (err) {
            // Ignore ENOENT
            if (err.code !== 'ENOENT') {
              throw err;
            }
          }
        }
      }
    } catch (error) {
      this.emit('error', error);
    }
  }

  #watchFile(file) {
    if (this.#closed) {
      return;
    }

    const { watch, statSync } = lazyLoadFsSync();

    if (this.#files.has(file)) {
      return;
    }

    {
      const existingStat = statSync(file);
      this.#files.set(file, existingStat);
    }

    const watcher = watch(file, {
      persistent: this.#options.persistent,
    }, (eventType, filename) => {
      const existingStat = this.#files.get(file);
      let currentStats;

      try {
        currentStats = statSync(file);
        this.#files.set(file, currentStats);
      } catch {
        // This happens if the file was removed
      }

      if (currentStats === undefined || (currentStats.birthtimeMs === 0 && existingStat.birthtimeMs !== 0)) {
        // The file is now deleted
        this.#files.delete(file);
        this.#watchers.delete(file);
        watcher.close();
        this.emit('change', 'rename', pathRelative(this.#rootPath, file));
        this.#unwatchFiles(file);
      } else if (file === this.#rootPath && this.#watchingFile) {
        // This case will only be triggered when watching a file with fs.watch
        this.emit('change', 'change', pathBasename(file));
      } else if (this.#symbolicFiles.has(file)) {
        // Stats from watchFile does not return correct value for currentStats.isSymbolicLink()
        // Since it is only valid when using fs.lstat(). Therefore, check the existing symbolic files.
        this.emit('change', 'rename', pathRelative(this.#rootPath, file));
      } else if (currentStats.isDirectory()) {
        this.#watchFolder(file);
      } else {
        // Watching a directory will trigger a change event for child files)
        this.emit('change', 'change', pathRelative(this.#rootPath, file));
      }
    });
    this.#watchers.set(file, watcher);
  }

  [kFSWatchStart](filename) {
    filename = pathResolve(getValidatedPath(filename));

    try {
      const file = lazyLoadFsSync().statSync(filename);

      this.#rootPath = filename;
      this.#closed = false;
      this.#watchingFile = file.isFile();

      this.#watchFile(filename);
      if (file.isDirectory()) {
        this.#watchFolder(filename);
      }
    } catch (error) {
      if (error.code === 'ENOENT') {
        error.filename = filename;
        throw error;
      }
    }

  }

  ref() {
    this.#files.forEach((file) => {
      if (file instanceof StatWatcher) {
        file.ref();
      }
    });
  }

  unref() {
    this.#files.forEach((file) => {
      if (file instanceof StatWatcher) {
        file.unref();
      }
    });
  }

  [SymbolAsyncIterator]() {
    const { signal } = this.#options;
    const promiseExecutor = signal == null ?
      (resolve) => {
        this.once('change', (eventType, filename) => {
          resolve({ __proto__: null, value: { eventType, filename } });
        });
      } : (resolve, reject) => {
        const onAbort = () => {
          this.close();
          reject(new AbortError(undefined, { cause: signal.reason }));
        };
        if (signal.aborted) return onAbort();
        kResistStopPropagation ??= require('internal/event_target').kResistStopPropagation;
        signal.addEventListener('abort', onAbort, { __proto__: null, once: true, [kResistStopPropagation]: true });
        this.once('change', (eventType, filename) => {
          signal.removeEventListener('abort', onAbort);
          resolve({ __proto__: null, value: { eventType, filename } });
        });
      };
    return {
      next: () => (this.#closed ?
        { __proto__: null, done: true } :
        new Promise(promiseExecutor)),
      return: () => {
        this.close();
        return { __proto__: null, done: true };
      },
      [SymbolAsyncIterator]() { return this; },
    };
  }
}

module.exports = {
  FSWatcher,
  kFSWatchStart,
};
