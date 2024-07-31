'use strict';

const {
  FunctionPrototypeCall,
  ObjectDefineProperty,
  ObjectSetPrototypeOf,
  Symbol,
} = primordials;

const {
  AbortError,
  UVException,
  codes: {
    ERR_INVALID_ARG_VALUE,
  },
} = require('internal/errors');
const {
  createDeferredPromise,
  kEmptyObject,
} = require('internal/util');

const {
  kFsStatsFieldsNumber,
  StatWatcher: _StatWatcher,
} = internalBinding('fs');

const { FSEvent } = internalBinding('fs_event_wrap');
const { UV_ENOSPC } = internalBinding('uv');
const { EventEmitter } = require('events');

const {
  getStatsFromBinding,
  getValidatedPath,
} = require('internal/fs/utils');

const {
  defaultTriggerAsyncIdScope,
  symbols: { owner_symbol },
} = require('internal/async_hooks');

const { toNamespacedPath } = require('path');

const {
  validateAbortSignal,
  validateBoolean,
  validateObject,
  validateUint32,
} = require('internal/validators');

const {
  Buffer: {
    isEncoding,
  },
} = require('buffer');

const assert = require('internal/assert');

const kOldStatus = Symbol('kOldStatus');
const kUseBigint = Symbol('kUseBigint');

const kFSWatchStart = Symbol('kFSWatchStart');
const kFSStatWatcherStart = Symbol('kFSStatWatcherStart');
const KFSStatWatcherRefCount = Symbol('KFSStatWatcherRefCount');
const KFSStatWatcherMaxRefCount = Symbol('KFSStatWatcherMaxRefCount');
const kFSStatWatcherAddOrCleanRef = Symbol('kFSStatWatcherAddOrCleanRef');

function emitStop(self) {
  self.emit('stop');
}

function StatWatcher(bigint) {
  FunctionPrototypeCall(EventEmitter, this);

  this._handle = null;
  this[kOldStatus] = -1;
  this[kUseBigint] = bigint;
  this[KFSStatWatcherRefCount] = 1;
  this[KFSStatWatcherMaxRefCount] = 1;
}
ObjectSetPrototypeOf(StatWatcher.prototype, EventEmitter.prototype);
ObjectSetPrototypeOf(StatWatcher, EventEmitter);

function onchange(newStatus, stats) {
  const self = this[owner_symbol];
  if (self[kOldStatus] === -1 &&
      newStatus === -1 &&
      stats[2/* new nlink */] === stats[16/* old nlink */]) {
    return;
  }

  self[kOldStatus] = newStatus;
  self.emit('change', getStatsFromBinding(stats),
            getStatsFromBinding(stats, kFsStatsFieldsNumber));
}

// At the moment if filename is undefined, we
// 1. Throw an Error if it's the first
//    time Symbol('kFSStatWatcherStart') is called
// 2. Return silently if Symbol('kFSStatWatcherStart') has already been called
//    on a valid filename and the wrap has been initialized
// This method is a noop if the watcher has already been started.
StatWatcher.prototype[kFSStatWatcherStart] = function(filename,
                                                      persistent,
                                                      interval) {
  if (this._handle !== null)
    return;

  this._handle = new _StatWatcher(this[kUseBigint]);
  this._handle[owner_symbol] = this;
  this._handle.onchange = onchange;
  if (!persistent)
    this.unref();

  // uv_fs_poll is a little more powerful than ev_stat but we curb it for
  // the sake of backwards compatibility.
  this[kOldStatus] = -1;

  filename = getValidatedPath(filename, 'filename');
  validateUint32(interval, 'interval');
  const err = this._handle.start(toNamespacedPath(filename), interval);
  if (err) {
    const error = new UVException({
      errno: err,
      syscall: 'watch',
      path: filename,
    });
    error.filename = filename;
    throw error;
  }
};

// To maximize backward-compatibility for the end user,
// a no-op stub method has been added instead of
// totally removing StatWatcher.prototype.start.
// This should not be documented.
StatWatcher.prototype.start = () => {};

// FIXME(joyeecheung): this method is not documented while there is
// another documented fs.unwatchFile(). The counterpart in
// FSWatcher is .close()
// This method is a noop if the watcher has not been started.
StatWatcher.prototype.stop = function() {
  if (this._handle === null)
    return;

  defaultTriggerAsyncIdScope(this._handle.getAsyncId(),
                             process.nextTick,
                             emitStop,
                             this);
  this._handle.close();
  this._handle = null;
};

// Clean up or add ref counters.
StatWatcher.prototype[kFSStatWatcherAddOrCleanRef] = function(operate) {
  if (operate === 'add') {
    // Add a Ref
    this[KFSStatWatcherRefCount]++;
    this[KFSStatWatcherMaxRefCount]++;
  } else if (operate === 'clean') {
    // Clean up a single
    this[KFSStatWatcherMaxRefCount]--;
    this.unref();
  } else if (operate === 'cleanAll') {
    // Clean up all
    this[KFSStatWatcherMaxRefCount] = 0;
    this[KFSStatWatcherRefCount] = 0;
    this._handle?.unref();
  }
};

StatWatcher.prototype.ref = function() {
  // Avoid refCount calling ref multiple times causing unref to have no effect.
  if (this[KFSStatWatcherRefCount] === this[KFSStatWatcherMaxRefCount])
    return this;
  if (this._handle && this[KFSStatWatcherRefCount]++ === 0)
    this._handle.ref();
  return this;
};

StatWatcher.prototype.unref = function() {
  // Avoid refCount calling unref multiple times causing ref to have no effect.
  if (this[KFSStatWatcherRefCount] === 0) return this;
  if (this._handle && --this[KFSStatWatcherRefCount] === 0)
    this._handle.unref();
  return this;
};


function FSWatcher() {
  FunctionPrototypeCall(EventEmitter, this);

  this._handle = new FSEvent();
  this._handle[owner_symbol] = this;

  this._handle.onchange = (status, eventType, filename) => {
    // TODO(joyeecheung): we may check self._handle.initialized here
    // and return if that is false. This allows us to avoid firing the event
    // after the handle is closed, and to fire both UV_RENAME and UV_CHANGE
    // if they are set by libuv at the same time.
    if (status < 0) {
      if (this._handle !== null) {
        // We don't use this.close() here to avoid firing the close event.
        this._handle.close();
        this._handle = null;  // Make the handle garbage collectable.
      }
      const error = new UVException({
        errno: status,
        syscall: 'watch',
        path: filename,
      });
      error.filename = filename;
      this.emit('error', error);
    } else {
      this.emit('change', eventType, filename);
    }
  };
}
ObjectSetPrototypeOf(FSWatcher.prototype, EventEmitter.prototype);
ObjectSetPrototypeOf(FSWatcher, EventEmitter);

// At the moment if filename is undefined, we
// 1. Throw an Error if it's the first time Symbol('kFSWatchStart') is called
// 2. Return silently if Symbol('kFSWatchStart') has already been called
//    on a valid filename and the wrap has been initialized
// 3. Return silently if the watcher has already been closed
// This method is a noop if the watcher has already been started.
FSWatcher.prototype[kFSWatchStart] = function(filename,
                                              persistent,
                                              recursive,
                                              encoding) {
  if (this._handle === null) {  // closed
    return;
  }
  assert(this._handle instanceof FSEvent, 'handle must be a FSEvent');
  if (this._handle.initialized) {  // already started
    return;
  }

  filename = getValidatedPath(filename, 'filename');

  const err = this._handle.start(toNamespacedPath(filename),
                                 persistent,
                                 recursive,
                                 encoding);
  if (err) {
    const error = new UVException({
      errno: err,
      syscall: 'watch',
      path: filename,
      message: err === UV_ENOSPC ?
        'System limit for number of file watchers reached' : '',
    });
    error.filename = filename;
    throw error;
  }
};

// To maximize backward-compatibility for the end user,
// a no-op stub method has been added instead of
// totally removing FSWatcher.prototype.start.
// This should not be documented.
FSWatcher.prototype.start = () => {};

// This method is a noop if the watcher has not been started or
// has already been closed.
FSWatcher.prototype.close = function() {
  if (this._handle === null) {  // closed
    return;
  }
  assert(this._handle instanceof FSEvent, 'handle must be a FSEvent');
  if (!this._handle.initialized) {  // not started
    return;
  }
  this._handle.close();
  this._handle = null;  // Make the handle garbage collectable.
  process.nextTick(emitCloseNT, this);
};

FSWatcher.prototype.ref = function() {
  if (this._handle) this._handle.ref();
  return this;
};

FSWatcher.prototype.unref = function() {
  if (this._handle) this._handle.unref();
  return this;
};

function emitCloseNT(self) {
  self.emit('close');
}

// Legacy alias on the C++ wrapper object. This is not public API, so we may
// want to runtime-deprecate it at some point. There's no hurry, though.
ObjectDefineProperty(FSEvent.prototype, 'owner', {
  __proto__: null,
  get() { return this[owner_symbol]; },
  set(v) { return this[owner_symbol] = v; },
});

let kResistStopPropagation;

async function* watch(filename, options = kEmptyObject) {
  const path = toNamespacedPath(getValidatedPath(filename));
  validateObject(options, 'options');

  const {
    persistent = true,
    recursive = false,
    encoding = 'utf8',
    signal,
  } = options;

  validateBoolean(persistent, 'options.persistent');
  validateBoolean(recursive, 'options.recursive');
  validateAbortSignal(signal, 'options.signal');

  if (encoding && !isEncoding(encoding)) {
    const reason = 'is invalid encoding';
    throw new ERR_INVALID_ARG_VALUE(encoding, 'encoding', reason);
  }

  if (signal?.aborted)
    throw new AbortError(undefined, { cause: signal?.reason });

  const handle = new FSEvent();
  let { promise, resolve, reject } = createDeferredPromise();
  const oncancel = () => {
    handle.close();
    reject(new AbortError(undefined, { cause: signal?.reason }));
  };

  try {
    if (signal) {
      kResistStopPropagation ??= require('internal/event_target').kResistStopPropagation;
      signal.addEventListener('abort', oncancel, { __proto__: null, once: true, [kResistStopPropagation]: true });
    }
    handle.onchange = (status, eventType, filename) => {
      if (status < 0) {
        const error = new UVException({
          errno: status,
          syscall: 'watch',
          path: filename,
        });
        error.filename = filename;
        handle.close();
        reject(error);
        return;
      }

      resolve({ eventType, filename });
    };

    const err = handle.start(path, persistent, recursive, encoding);
    if (err) {
      const error = new UVException({
        errno: err,
        syscall: 'watch',
        path: filename,
        message: err === UV_ENOSPC ?
          'System limit for number of file watchers reached' : '',
      });
      error.filename = filename;
      handle.close();
      throw error;
    }

    while (!signal?.aborted) {
      yield await promise;
      ({ promise, resolve, reject } = createDeferredPromise());
    }
    throw new AbortError(undefined, { cause: signal?.reason });
  } finally {
    handle.close();
    signal?.removeEventListener('abort', oncancel);
  }
}

module.exports = {
  FSWatcher,
  StatWatcher,
  kFSWatchStart,
  kFSStatWatcherStart,
  kFSStatWatcherAddOrCleanRef,
  watch,
};
