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

'use strict';

const {
  ArrayIsArray,
  ArrayPrototypeIncludes,
  ArrayPrototypeIndexOf,
  ArrayPrototypePush,
  Boolean,
  FunctionPrototypeBind,
  FunctionPrototypeCall,
  MathMax,
  Number,
  NumberIsNaN,
  NumberParseInt,
  ObjectDefineProperty,
  ObjectSetPrototypeOf,
  Symbol,
  SymbolAsyncDispose,
  SymbolDispose,
} = primordials;

const EventEmitter = require('events');
const { addAbortListener } = require('internal/events/abort_listener');
const stream = require('stream');
let debug = require('internal/util/debuglog').debuglog('net', (fn) => {
  debug = fn;
});
const {
  kReinitializeHandle,
  isIP,
  isIPv4,
  isIPv6,
  normalizedArgsSymbol,
  makeSyncWrite,
} = require('internal/net');
const assert = require('internal/assert');
const {
  UV_EADDRINUSE,
  UV_EINVAL,
  UV_ENOTCONN,
  UV_ECANCELED,
  UV_ETIMEDOUT,
} = internalBinding('uv');
const { convertIpv6StringToBuffer } = internalBinding('cares_wrap');

const { Buffer } = require('buffer');
const { ShutdownWrap } = internalBinding('stream_wrap');
const {
  TCP,
  TCPConnectWrap,
  constants: TCPConstants,
} = internalBinding('tcp_wrap');
const {
  Pipe,
  PipeConnectWrap,
  constants: PipeConstants,
} = internalBinding('pipe_wrap');
const {
  newAsyncId,
  defaultTriggerAsyncIdScope,
  symbols: { async_id_symbol, owner_symbol },
} = require('internal/async_hooks');
const {
  writevGeneric,
  writeGeneric,
  onStreamRead,
  kAfterAsyncWrite,
  kHandle,
  kUpdateTimer,
  setStreamTimeout,
  kBuffer,
  kBufferCb,
  kBufferGen,
} = require('internal/stream_base_commons');
const {
  ErrnoException,
  ExceptionWithHostPort,
  NodeAggregateError,
  UVExceptionWithHostPort,
  codes: {
    ERR_INVALID_ADDRESS_FAMILY,
    ERR_INVALID_ARG_TYPE,
    ERR_INVALID_ARG_VALUE,
    ERR_INVALID_FD_TYPE,
    ERR_INVALID_HANDLE_TYPE,
    ERR_INVALID_IP_ADDRESS,
    ERR_IP_BLOCKED,
    ERR_MISSING_ARGS,
    ERR_SERVER_ALREADY_LISTEN,
    ERR_SERVER_NOT_RUNNING,
    ERR_SOCKET_CLOSED,
    ERR_SOCKET_CLOSED_BEFORE_CONNECTION,
    ERR_SOCKET_CONNECTION_TIMEOUT,
  },
  genericNodeError,
} = require('internal/errors');
const { isUint8Array } = require('internal/util/types');
const { queueMicrotask } = require('internal/process/task_queues');
const {
  guessHandleType,
  isWindows,
  kEmptyObject,
  promisify,
} = require('internal/util');
const {
  validateAbortSignal,
  validateBoolean,
  validateFunction,
  validateInt32,
  validateNumber,
  validatePort,
  validateString,
} = require('internal/validators');
const kLastWriteQueueSize = Symbol('lastWriteQueueSize');
const { getOptionValue } = require('internal/options');

// Lazy loaded to improve startup performance.
let cluster;
let dns;
let BlockList;
let SocketAddress;
let autoSelectFamilyDefault = getOptionValue('--network-family-autoselection');
let autoSelectFamilyAttemptTimeoutDefault = getOptionValue('--network-family-autoselection-attempt-timeout');

const { clearTimeout, setTimeout } = require('timers');
const { kTimeout } = require('internal/timers');

const DEFAULT_IPV4_ADDR = '0.0.0.0';
const DEFAULT_IPV6_ADDR = '::';

const noop = () => {};

const kPerfHooksNetConnectContext = Symbol('kPerfHooksNetConnectContext');

const dc = require('diagnostics_channel');
const netClientSocketChannel = dc.channel('net.client.socket');
const netServerSocketChannel = dc.channel('net.server.socket');
const netServerListen = dc.tracingChannel('net.server.listen');

const {
  hasObserver,
  startPerf,
  stopPerf,
} = require('internal/perf/observe');
const { getDefaultHighWaterMark } = require('internal/streams/state');

function getFlags(options) {
  let flags = 0;
  if (options.ipv6Only === true) {
    flags |= TCPConstants.UV_TCP_IPV6ONLY;
  }
  if (options.reusePort === true) {
    flags |= TCPConstants.UV_TCP_REUSEPORT;
  }
  return flags;
}

function createHandle(fd, is_server) {
  validateInt32(fd, 'fd', 0);
  const type = guessHandleType(fd);
  if (type === 'PIPE') {
    return new Pipe(
      is_server ? PipeConstants.SERVER : PipeConstants.SOCKET,
    );
  }

  if (type === 'TCP') {
    return new TCP(
      is_server ? TCPConstants.SERVER : TCPConstants.SOCKET,
    );
  }

  throw new ERR_INVALID_FD_TYPE(type);
}


function getNewAsyncId(handle) {
  return (!handle || typeof handle.getAsyncId !== 'function') ?
    newAsyncId() : handle.getAsyncId();
}


function isPipeName(s) {
  return typeof s === 'string' && toNumber(s) === false;
}

/**
 * Creates a new TCP or IPC server
 * @param {{
 *   allowHalfOpen?: boolean;
 *   pauseOnConnect?: boolean;
 *   }} [options]
 * @param {Function} [connectionListener]
 * @returns {Server}
 */

function createServer(options, connectionListener) {
  return new Server(options, connectionListener);
}


// Target API:
//
// let s = net.connect({port: 80, host: 'google.com'}, function() {
//   ...
// });
//
// There are various forms:
//
// connect(options, [cb])
// connect(port, [host], [cb])
// connect(path, [cb]);
//
function connect(...args) {
  const normalized = normalizeArgs(args);
  const options = normalized[0];
  debug('createConnection', normalized);
  const socket = new Socket(options);

  if (netClientSocketChannel.hasSubscribers) {
    netClientSocketChannel.publish({
      socket,
    });
  }
  if (options.timeout) {
    socket.setTimeout(options.timeout);
  }

  return socket.connect(normalized);
}

function getDefaultAutoSelectFamily() {
  return autoSelectFamilyDefault;
}

function setDefaultAutoSelectFamily(value) {
  validateBoolean(value, 'value');
  autoSelectFamilyDefault = value;
}

function getDefaultAutoSelectFamilyAttemptTimeout() {
  return autoSelectFamilyAttemptTimeoutDefault;
}

function setDefaultAutoSelectFamilyAttemptTimeout(value) {
  validateInt32(value, 'value', 1);

  if (value < 10) {
    value = 10;
  }

  autoSelectFamilyAttemptTimeoutDefault = value;
}

// Returns an array [options, cb], where options is an object,
// cb is either a function or null.
// Used to normalize arguments of Socket.prototype.connect() and
// Server.prototype.listen(). Possible combinations of parameters:
//   (options[...][, cb])
//   (path[...][, cb])
//   ([port][, host][...][, cb])
// For Socket.prototype.connect(), the [...] part is ignored
// For Server.prototype.listen(), the [...] part is [, backlog]
// but will not be handled here (handled in listen())
function normalizeArgs(args) {
  let arr;

  if (args.length === 0) {
    arr = [{}, null];
    arr[normalizedArgsSymbol] = true;
    return arr;
  }

  const arg0 = args[0];
  let options = {};
  if (typeof arg0 === 'object' && arg0 !== null) {
    // (options[...][, cb])
    options = arg0;
  } else if (isPipeName(arg0)) {
    // (path[...][, cb])
    options.path = arg0;
  } else {
    // ([port][, host][...][, cb])
    options.port = arg0;
    if (args.length > 1 && typeof args[1] === 'string') {
      options.host = args[1];
    }
  }

  const cb = args[args.length - 1];
  if (typeof cb !== 'function')
    arr = [options, null];
  else
    arr = [options, cb];

  arr[normalizedArgsSymbol] = true;
  return arr;
}


// Called when creating new Socket, or when re-using a closed Socket
function initSocketHandle(self) {
  self._undestroy();
  self._sockname = null;

  // Handle creation may be deferred to bind() or connect() time.
  if (self._handle) {
    self._handle[owner_symbol] = self;
    self._handle.onread = onStreamRead;
    self[async_id_symbol] = getNewAsyncId(self._handle);

    let userBuf = self[kBuffer];
    if (userBuf) {
      const bufGen = self[kBufferGen];
      if (bufGen !== null) {
        userBuf = bufGen();
        if (!isUint8Array(userBuf))
          return;
        self[kBuffer] = userBuf;
      }
      self._handle.useUserBuffer(userBuf);
    }
  }
}

function closeSocketHandle(self, isException, isCleanupPending = false) {
  if (self._handle) {
    self._handle.close(() => {
      debug('emit close');
      self.emit('close', isException);
      if (isCleanupPending) {
        self._handle.onread = noop;
        self._handle = null;
        self._sockname = null;
      }
    });
  }
}

const kBytesRead = Symbol('kBytesRead');
const kBytesWritten = Symbol('kBytesWritten');
const kSetNoDelay = Symbol('kSetNoDelay');
const kSetKeepAlive = Symbol('kSetKeepAlive');
const kSetKeepAliveInitialDelay = Symbol('kSetKeepAliveInitialDelay');

function Socket(options) {
  if (!(this instanceof Socket)) return new Socket(options);
  if (options?.objectMode) {
    throw new ERR_INVALID_ARG_VALUE(
      'options.objectMode',
      options.objectMode,
      'is not supported',
    );
  } else if (options?.readableObjectMode || options?.writableObjectMode) {
    throw new ERR_INVALID_ARG_VALUE(
      `options.${
        options.readableObjectMode ? 'readableObjectMode' : 'writableObjectMode'
      }`,
      options.readableObjectMode || options.writableObjectMode,
      'is not supported',
    );
  }
  if (typeof options?.keepAliveInitialDelay !== 'undefined') {
    validateNumber(
      options?.keepAliveInitialDelay, 'options.keepAliveInitialDelay',
    );

    if (options.keepAliveInitialDelay < 0) {
      options.keepAliveInitialDelay = 0;
    }
  }

  this.connecting = false;
  // Problem with this is that users can supply their own handle, that may not
  // have _handle.getAsyncId(). In this case an[async_id_symbol] should
  // probably be supplied by async_hooks.
  this[async_id_symbol] = -1;
  this._hadError = false;
  this[kHandle] = null;
  this._parent = null;
  this._host = null;
  this[kLastWriteQueueSize] = 0;
  this[kTimeout] = null;
  this[kBuffer] = null;
  this[kBufferCb] = null;
  this[kBufferGen] = null;
  this._closeAfterHandlingError = false;

  if (typeof options === 'number')
    options = { fd: options }; // Legacy interface.
  else
    options = { ...options };

  // Default to *not* allowing half open sockets.
  options.allowHalfOpen = Boolean(options.allowHalfOpen);
  // For backwards compat do not emit close on destroy.
  options.emitClose = false;
  options.autoDestroy = true;
  // Handle strings directly.
  options.decodeStrings = false;
  stream.Duplex.call(this, options);

  if (options.handle) {
    this._handle = options.handle; // private
    this[async_id_symbol] = getNewAsyncId(this._handle);
  } else if (options.fd !== undefined) {
    const { fd } = options;
    let err;

    // createHandle will throw ERR_INVALID_FD_TYPE if `fd` is not
    // a valid `PIPE` or `TCP` descriptor
    this._handle = createHandle(fd, false);

    err = this._handle.open(fd);

    // While difficult to fabricate, in some architectures
    // `open` may return an error code for valid file descriptors
    // which cannot be opened. This is difficult to test as most
    // un-openable fds will throw on `createHandle`
    if (err)
      throw new ErrnoException(err, 'open');

    this[async_id_symbol] = this._handle.getAsyncId();

    if ((fd === 1 || fd === 2) &&
        (this._handle instanceof Pipe) && isWindows) {
      // Make stdout and stderr blocking on Windows
      err = this._handle.setBlocking(true);
      if (err)
        throw new ErrnoException(err, 'setBlocking');

      this._writev = null;
      this._write = makeSyncWrite(fd);
      // makeSyncWrite adjusts this value like the original handle would, so
      // we need to let it do that by turning it into a writable, own
      // property.
      ObjectDefineProperty(this._handle, 'bytesWritten', {
        __proto__: null,
        value: 0, writable: true,
      });
    }
  }

  const onread = options.onread;
  if (onread !== null && typeof onread === 'object' &&
      (isUint8Array(onread.buffer) || typeof onread.buffer === 'function') &&
      typeof onread.callback === 'function') {
    if (typeof onread.buffer === 'function') {
      this[kBuffer] = true;
      this[kBufferGen] = onread.buffer;
    } else {
      this[kBuffer] = onread.buffer;
    }
    this[kBufferCb] = onread.callback;
  }

  this[kSetNoDelay] = Boolean(options.noDelay);
  this[kSetKeepAlive] = Boolean(options.keepAlive);
  this[kSetKeepAliveInitialDelay] = ~~(options.keepAliveInitialDelay / 1000);

  // Shut down the socket when we're finished with it.
  this.on('end', onReadableStreamEnd);

  initSocketHandle(this);

  this._pendingData = null;
  this._pendingEncoding = '';

  // If we have a handle, then start the flow of data into the
  // buffer.  if not, then this will happen when we connect
  if (this._handle && options.readable !== false) {
    if (options.pauseOnCreate) {
      // Stop the handle from reading and pause the stream
      this._handle.reading = false;
      this._handle.readStop();
      this.readableFlowing = false;
    } else if (!options.manualStart) {
      this.read(0);
    }
  }

  if (options.signal) {
    addClientAbortSignalOption(this, options);
  }

  // Reserve properties
  this.server = null;
  this._server = null;

  // Used after `.destroy()`
  this[kBytesRead] = 0;
  this[kBytesWritten] = 0;
  if (options.blockList) {
    if (!module.exports.BlockList.isBlockList(options.blockList)) {
      throw new ERR_INVALID_ARG_TYPE('options.blockList', 'net.BlockList', options.blockList);
    }
    this.blockList = options.blockList;
  }
}
ObjectSetPrototypeOf(Socket.prototype, stream.Duplex.prototype);
ObjectSetPrototypeOf(Socket, stream.Duplex);

// Refresh existing timeouts.
Socket.prototype._unrefTimer = function _unrefTimer() {
  for (let s = this; s !== null; s = s._parent) {
    if (s[kTimeout])
      s[kTimeout].refresh();
  }
};


// The user has called .end(), and all the bytes have been
// sent out to the other side.
Socket.prototype._final = function(cb) {
  // If still connecting - defer handling `_final` until 'connect' will happen
  if (this.connecting) {
    debug('_final: not yet connected');
    return this.once('connect', () => this._final(cb));
  }

  if (!this._handle)
    return cb();

  debug('_final: not ended, call shutdown()');

  const req = new ShutdownWrap();
  req.oncomplete = afterShutdown;
  req.handle = this._handle;
  req.callback = cb;
  const err = this._handle.shutdown(req);

  if (err === 1 || err === UV_ENOTCONN)  // synchronous finish
    return cb();
  else if (err !== 0)
    return cb(new ErrnoException(err, 'shutdown'));
};

function afterShutdown() {
  const self = this.handle[owner_symbol];

  debug('afterShutdown destroyed=%j', self.destroyed);

  this.callback();
}

// Provide a better error message when we call end() as a result
// of the other side sending a FIN.  The standard 'write after end'
// is overly vague, and makes it seem like the user's code is to blame.
function writeAfterFIN(chunk, encoding, cb) {
  if (!this.writableEnded) {
    return stream.Duplex.prototype.write.call(this, chunk, encoding, cb);
  }

  if (typeof encoding === 'function') {
    cb = encoding;
    encoding = null;
  }

  const er = genericNodeError(
    'This socket has been ended by the other party',
    { code: 'EPIPE' },
  );
  if (typeof cb === 'function') {
    defaultTriggerAsyncIdScope(this[async_id_symbol], process.nextTick, cb, er);
  }
  this.destroy(er);

  return false;
}

Socket.prototype.setTimeout = setStreamTimeout;


Socket.prototype._onTimeout = function() {
  const handle = this._handle;
  const lastWriteQueueSize = this[kLastWriteQueueSize];
  if (lastWriteQueueSize > 0 && handle) {
    // `lastWriteQueueSize !== writeQueueSize` means there is
    // an active write in progress, so we suppress the timeout.
    const { writeQueueSize } = handle;
    if (lastWriteQueueSize !== writeQueueSize) {
      this[kLastWriteQueueSize] = writeQueueSize;
      this._unrefTimer();
      return;
    }
  }
  debug('_onTimeout');
  this.emit('timeout');
};


Socket.prototype.setNoDelay = function(enable) {
  // Backwards compatibility: assume true when `enable` is omitted
  enable = Boolean(enable === undefined ? true : enable);

  if (!this._handle) {
    this[kSetNoDelay] = enable;
    return this;
  }

  if (this._handle.setNoDelay && enable !== this[kSetNoDelay]) {
    this[kSetNoDelay] = enable;
    this._handle.setNoDelay(enable);
  }

  return this;
};


Socket.prototype.setKeepAlive = function(enable, initialDelayMsecs) {
  enable = Boolean(enable);
  const initialDelay = ~~(initialDelayMsecs / 1000);

  if (!this._handle) {
    this[kSetKeepAlive] = enable;
    this[kSetKeepAliveInitialDelay] = initialDelay;
    return this;
  }

  if (!this._handle.setKeepAlive) {
    return this;
  }

  if (enable !== this[kSetKeepAlive] ||
      (
        enable &&
        this[kSetKeepAliveInitialDelay] !== initialDelay
      )
  ) {
    this[kSetKeepAlive] = enable;
    this[kSetKeepAliveInitialDelay] = initialDelay;
    this._handle.setKeepAlive(enable, initialDelay);
  }

  return this;
};


Socket.prototype.address = function() {
  return this._getsockname();
};


ObjectDefineProperty(Socket.prototype, '_connecting', {
  __proto__: null,
  get: function() {
    return this.connecting;
  },
});

ObjectDefineProperty(Socket.prototype, 'pending', {
  __proto__: null,
  get() {
    return !this._handle || this.connecting;
  },
  configurable: true,
});


ObjectDefineProperty(Socket.prototype, 'readyState', {
  __proto__: null,
  get: function() {
    if (this.connecting) {
      return 'opening';
    } else if (this.readable && this.writable) {
      return 'open';
    } else if (this.readable && !this.writable) {
      return 'readOnly';
    } else if (!this.readable && this.writable) {
      return 'writeOnly';
    }
    return 'closed';
  },
});


ObjectDefineProperty(Socket.prototype, 'bufferSize', {
  __proto__: null,
  get: function() {
    if (this._handle) {
      return this.writableLength;
    }
  },
});

ObjectDefineProperty(Socket.prototype, kUpdateTimer, {
  __proto__: null,
  get: function() {
    return this._unrefTimer;
  },
});


function tryReadStart(socket) {
  // Not already reading, start the flow
  debug('Socket._handle.readStart');
  socket._handle.reading = true;
  const err = socket._handle.readStart();
  if (err)
    socket.destroy(new ErrnoException(err, 'read'));
}

// Just call handle.readStart until we have enough in the buffer
Socket.prototype._read = function(n) {
  debug(
    '_read - n', n,
    'isConnecting?', !!this.connecting,
    'hasHandle?', !!this._handle,
  );

  if (this.connecting || !this._handle) {
    debug('_read wait for connection');
    this.once('connect', () => this._read(n));
  } else if (!this._handle.reading) {
    tryReadStart(this);
  }
};


Socket.prototype.end = function(data, encoding, callback) {
  stream.Duplex.prototype.end.call(this,
                                   data, encoding, callback);
  return this;
};

Socket.prototype.resetAndDestroy = function() {
  if (this._handle) {
    if (!(this._handle instanceof TCP))
      throw new ERR_INVALID_HANDLE_TYPE();
    if (this.connecting) {
      debug('reset wait for connection');
      this.once('connect', () => this._reset());
    } else {
      this._reset();
    }
  } else {
    this.destroy(new ERR_SOCKET_CLOSED());
  }
  return this;
};

Socket.prototype.pause = function() {
  if (this[kBuffer] && !this.connecting && this._handle?.reading) {
    this._handle.reading = false;
    if (!this.destroyed) {
      const err = this._handle.readStop();
      if (err)
        this.destroy(new ErrnoException(err, 'read'));
    }
  }
  return stream.Duplex.prototype.pause.call(this);
};


Socket.prototype.resume = function() {
  if (this[kBuffer] && !this.connecting && this._handle &&
      !this._handle.reading) {
    tryReadStart(this);
  }
  return stream.Duplex.prototype.resume.call(this);
};


Socket.prototype.read = function(n) {
  if (this[kBuffer] && !this.connecting && this._handle &&
      !this._handle.reading) {
    tryReadStart(this);
  }
  return stream.Duplex.prototype.read.call(this, n);
};


// Called when the 'end' event is emitted.
function onReadableStreamEnd() {
  if (!this.allowHalfOpen) {
    this.write = writeAfterFIN;
  }
}


Socket.prototype.destroySoon = function() {
  if (this.writable)
    this.end();

  if (this.writableFinished)
    this.destroy();
  else
    this.once('finish', this.destroy);
};


Socket.prototype._destroy = function(exception, cb) {
  debug('destroy');

  this.connecting = false;

  for (let s = this; s !== null; s = s._parent) {
    clearTimeout(s[kTimeout]);
  }

  debug('close');
  if (this._handle) {
    if (this !== process.stderr)
      debug('close handle');
    const isException = exception ? true : false;
    // `bytesRead` and `kBytesWritten` should be accessible after `.destroy()`
    this[kBytesRead] = this._handle.bytesRead;
    this[kBytesWritten] = this._handle.bytesWritten;

    if (this.resetAndClosing) {
      this.resetAndClosing = false;
      const err = this._handle.reset(() => {
        debug('emit close');
        this.emit('close', isException);
      });
      if (err)
        this.emit('error', new ErrnoException(err, 'reset'));
    } else if (this._closeAfterHandlingError) {
      // Enqueue closing the socket as a microtask, so that the socket can be
      // accessible when an `error` event is handled in the `next tick queue`.
      queueMicrotask(() => closeSocketHandle(this, isException, true));
    } else {
      closeSocketHandle(this, isException);
    }

    if (!this._closeAfterHandlingError) {
      this._handle.onread = noop;
      this._handle = null;
      this._sockname = null;
    }
    cb(exception);
  } else {
    cb(exception);
    process.nextTick(emitCloseNT, this);
  }

  if (this._server) {
    debug('has server');
    this._server._connections--;
    if (this._server._emitCloseIfDrained) {
      this._server._emitCloseIfDrained();
    }
  }
};

Socket.prototype._reset = function() {
  debug('reset connection');
  this.resetAndClosing = true;
  return this.destroy();
};

Socket.prototype._getpeername = function() {
  if (!this._handle || !this._handle.getpeername || this.connecting) {
    return this._peername || {};
  } else if (!this._peername) {
    const out = {};
    const err = this._handle.getpeername(out);
    if (err) return out;
    this._peername = out;
  }
  return this._peername;
};

function protoGetter(name, callback) {
  ObjectDefineProperty(Socket.prototype, name, {
    __proto__: null,
    configurable: false,
    enumerable: true,
    get: callback,
  });
}

protoGetter('bytesRead', function bytesRead() {
  return this._handle ? this._handle.bytesRead : this[kBytesRead];
});

protoGetter('remoteAddress', function remoteAddress() {
  return this._getpeername().address;
});

protoGetter('remoteFamily', function remoteFamily() {
  return this._getpeername().family;
});

protoGetter('remotePort', function remotePort() {
  return this._getpeername().port;
});


Socket.prototype._getsockname = function() {
  if (!this._handle || !this._handle.getsockname) {
    return {};
  } else if (!this._sockname) {
    this._sockname = {};
    // FIXME(bnoordhuis) Throw when the return value is not 0?
    this._handle.getsockname(this._sockname);
  }
  return this._sockname;
};


protoGetter('localAddress', function localAddress() {
  return this._getsockname().address;
});


protoGetter('localPort', function localPort() {
  return this._getsockname().port;
});

protoGetter('localFamily', function localFamily() {
  return this._getsockname().family;
});

Socket.prototype[kAfterAsyncWrite] = function() {
  this[kLastWriteQueueSize] = 0;
};

Socket.prototype._writeGeneric = function(writev, data, encoding, cb) {
  // If we are still connecting, then buffer this for later.
  // The Writable logic will buffer up any more writes while
  // waiting for this one to be done.
  if (this.connecting) {
    this._pendingData = data;
    this._pendingEncoding = encoding;
    this.once('connect', function connect() {
      this.off('close', onClose);
      this._writeGeneric(writev, data, encoding, cb);
    });
    function onClose() {
      cb(new ERR_SOCKET_CLOSED_BEFORE_CONNECTION());
    }
    this.once('close', onClose);
    return;
  }
  this._pendingData = null;
  this._pendingEncoding = '';

  if (!this._handle) {
    cb(new ERR_SOCKET_CLOSED());
    return false;
  }

  this._unrefTimer();

  let req;
  if (writev)
    req = writevGeneric(this, data, cb);
  else
    req = writeGeneric(this, data, encoding, cb);
  if (req.async)
    this[kLastWriteQueueSize] = req.bytes;
};


Socket.prototype._writev = function(chunks, cb) {
  this._writeGeneric(true, chunks, '', cb);
};


Socket.prototype._write = function(data, encoding, cb) {
  this._writeGeneric(false, data, encoding, cb);
};


// Legacy alias. Having this is probably being overly cautious, but it doesn't
// really hurt anyone either. This can probably be removed safely if desired.
protoGetter('_bytesDispatched', function _bytesDispatched() {
  return this._handle ? this._handle.bytesWritten : this[kBytesWritten];
});

protoGetter('bytesWritten', function bytesWritten() {
  let bytes = this._bytesDispatched;
  const data = this._pendingData;
  const encoding = this._pendingEncoding;
  const writableBuffer = this.writableBuffer;

  if (!writableBuffer)
    return undefined;

  for (const el of writableBuffer) {
    bytes += el.chunk instanceof Buffer ?
      el.chunk.length :
      Buffer.byteLength(el.chunk, el.encoding);
  }

  if (ArrayIsArray(data)) {
    // Was a writev, iterate over chunks to get total length
    for (let i = 0; i < data.length; i++) {
      const chunk = data[i];

      if (data.allBuffers || chunk instanceof Buffer)
        bytes += chunk.length;
      else
        bytes += Buffer.byteLength(chunk.chunk, chunk.encoding);
    }
  } else if (data) {
    // Writes are either a string or a Buffer.
    if (typeof data !== 'string')
      bytes += data.length;
    else
      bytes += Buffer.byteLength(data, encoding);
  }

  return bytes;
});


function checkBindError(err, port, handle) {
  // EADDRINUSE may not be reported until we call listen() or connect().
  // To complicate matters, a failed bind() followed by listen() or connect()
  // will implicitly bind to a random port. Ergo, check that the socket is
  // bound to the expected port before calling listen() or connect().
  //
  // FIXME(bnoordhuis) Doesn't work for pipe handles, they don't have a
  // getsockname() method. Non-issue for now, the cluster module doesn't
  // really support pipes anyway.
  if (err === 0 && port > 0 && handle.getsockname) {
    const out = {};
    err = handle.getsockname(out);
    if (err === 0 && port !== out.port) {
      debug(`checkBindError, bound to ${out.port} instead of ${port}`);
      err = UV_EADDRINUSE;
    }
  }
  return err;
}


function internalConnect(
  self, address, port, addressType, localAddress, localPort, flags) {
  // TODO return promise from Socket.prototype.connect which
  // wraps _connectReq.

  assert(self.connecting);

  let err;

  if (localAddress || localPort) {
    if (addressType === 4) {
      localAddress ||= DEFAULT_IPV4_ADDR;
      err = self._handle.bind(localAddress, localPort);
    } else { // addressType === 6
      localAddress ||= DEFAULT_IPV6_ADDR;
      err = self._handle.bind6(localAddress, localPort, flags);
    }
    debug('connect: binding to localAddress: %s and localPort: %d (addressType: %d)',
          localAddress, localPort, addressType);

    err = checkBindError(err, localPort, self._handle);
    if (err) {
      const ex = new ExceptionWithHostPort(err, 'bind', localAddress, localPort);
      self.destroy(ex);
      return;
    }
  }

  debug('connect: attempting to connect to %s:%d (addressType: %d)', address, port, addressType);
  self.emit('connectionAttempt', address, port, addressType);

  if (addressType === 6 || addressType === 4) {
    if (self.blockList?.check(address, `ipv${addressType}`)) {
      self.destroy(new ERR_IP_BLOCKED(address));
      return;
    }
    const req = new TCPConnectWrap();
    req.oncomplete = afterConnect;
    req.address = address;
    req.port = port;
    req.localAddress = localAddress;
    req.localPort = localPort;
    req.addressType = addressType;

    if (addressType === 4)
      err = self._handle.connect(req, address, port);
    else
      err = self._handle.connect6(req, address, port);
  } else {
    const req = new PipeConnectWrap();
    req.address = address;
    req.oncomplete = afterConnect;

    err = self._handle.connect(req, address);
  }

  if (err) {
    const sockname = self._getsockname();
    let details;

    if (sockname) {
      details = sockname.address + ':' + sockname.port;
    }

    const ex = new ExceptionWithHostPort(err, 'connect', address, port, details);
    self.destroy(ex);
  } else if ((addressType === 6 || addressType === 4) && hasObserver('net')) {
    startPerf(self, kPerfHooksNetConnectContext, { type: 'net', name: 'connect', detail: { host: address, port } });
  }
}


function internalConnectMultiple(context, canceled) {
  clearTimeout(context[kTimeout]);
  const self = context.socket;

  // We were requested to abort. Stop all operations
  if (self._aborted) {
    return;
  }

  // All connections have been tried without success, destroy with error
  if (canceled || context.current === context.addresses.length) {
    if (context.errors.length === 0) {
      self.destroy(new ERR_SOCKET_CONNECTION_TIMEOUT());
      return;
    }

    self.destroy(new NodeAggregateError(context.errors));
    return;
  }

  assert(self.connecting);

  const current = context.current++;

  if (current > 0) {
    self[kReinitializeHandle](new TCP(TCPConstants.SOCKET));
  }

  const { localPort, port, flags } = context;
  const { address, family: addressType } = context.addresses[current];
  let localAddress;
  let err;

  if (localPort) {
    if (addressType === 4) {
      localAddress = DEFAULT_IPV4_ADDR;
      err = self._handle.bind(localAddress, localPort);
    } else { // addressType === 6
      localAddress = DEFAULT_IPV6_ADDR;
      err = self._handle.bind6(localAddress, localPort, flags);
    }

    debug('connect/multiple: binding to localAddress: %s and localPort: %d (addressType: %d)',
          localAddress, localPort, addressType);

    err = checkBindError(err, localPort, self._handle);
    if (err) {
      ArrayPrototypePush(context.errors, new ExceptionWithHostPort(err, 'bind', localAddress, localPort));
      internalConnectMultiple(context);
      return;
    }
  }

  if (self.blockList?.check(address, `ipv${addressType}`)) {
    const ex = new ERR_IP_BLOCKED(address);
    ArrayPrototypePush(context.errors, ex);
    self.emit('connectionAttemptFailed', address, port, addressType, ex);
    internalConnectMultiple(context);
    return;
  }

  debug('connect/multiple: attempting to connect to %s:%d (addressType: %d)', address, port, addressType);
  self.emit('connectionAttempt', address, port, addressType);

  const req = new TCPConnectWrap();
  req.oncomplete = FunctionPrototypeBind(afterConnectMultiple, undefined, context, current);
  req.address = address;
  req.port = port;
  req.localAddress = localAddress;
  req.localPort = localPort;
  req.addressType = addressType;

  ArrayPrototypePush(self.autoSelectFamilyAttemptedAddresses, `${address}:${port}`);

  if (addressType === 4) {
    err = self._handle.connect(req, address, port);
  } else {
    err = self._handle.connect6(req, address, port);
  }

  if (err) {
    const sockname = self._getsockname();
    let details;

    if (sockname) {
      details = sockname.address + ':' + sockname.port;
    }

    const ex = new ExceptionWithHostPort(err, 'connect', address, port, details);
    ArrayPrototypePush(context.errors, ex);

    self.emit('connectionAttemptFailed', address, port, addressType, ex);
    internalConnectMultiple(context);
    return;
  }

  if (current < context.addresses.length - 1) {
    debug('connect/multiple: setting the attempt timeout to %d ms', context.timeout);

    // If the attempt has not returned an error, start the connection timer
    context[kTimeout] = setTimeout(internalConnectMultipleTimeout, context.timeout, context, req, self._handle);
  }
}

Socket.prototype.connect = function(...args) {
  let normalized;
  // If passed an array, it's treated as an array of arguments that have
  // already been normalized (so we don't normalize more than once). This has
  // been solved before in https://github.com/nodejs/node/pull/12342, but was
  // reverted as it had unintended side effects.
  if (ArrayIsArray(args[0]) && args[0][normalizedArgsSymbol]) {
    normalized = args[0];
  } else {
    normalized = normalizeArgs(args);
  }
  const options = normalized[0];
  const cb = normalized[1];

  if (cb !== null) {
    this.once('connect', cb);
  }

  // If the parent is already connecting, do not attempt to connect again
  if (this._parent?.connecting) {
    return this;
  }

  // options.port === null will be checked later.
  if (options.port === undefined && options.path == null)
    throw new ERR_MISSING_ARGS(['options', 'port', 'path']);

  if (this.write !== Socket.prototype.write)
    this.write = Socket.prototype.write;

  if (this.destroyed) {
    this._handle = null;
    this._peername = null;
    this._sockname = null;
  }

  const { path } = options;
  const pipe = !!path;
  debug('pipe', pipe, path);

  if (!this._handle) {
    this._handle = pipe ?
      new Pipe(PipeConstants.SOCKET) :
      new TCP(TCPConstants.SOCKET);
    initSocketHandle(this);
  }

  this._unrefTimer();

  this.connecting = true;

  if (pipe) {
    validateString(path, 'options.path');
    defaultTriggerAsyncIdScope(
      this[async_id_symbol], internalConnect, this, path,
    );
  } else {
    lookupAndConnect(this, options);
  }
  return this;
};

Socket.prototype[kReinitializeHandle] = function reinitializeHandle(handle) {
  this._handle?.close();

  this._handle = handle;
  this._handle[owner_symbol] = this;

  initSocketHandle(this);
};

function socketToDnsFamily(family) {
  switch (family) {
    case 'IPv4':
      return 4;
    case 'IPv6':
      return 6;
  }

  return family;
}

function lookupAndConnect(self, options) {
  const { localAddress, localPort } = options;
  const host = options.host || 'localhost';
  let { port, autoSelectFamilyAttemptTimeout, autoSelectFamily } = options;

  if (localAddress && !isIP(localAddress)) {
    throw new ERR_INVALID_IP_ADDRESS(localAddress);
  }

  if (localPort) {
    validateNumber(localPort, 'options.localPort');
  }

  if (typeof port !== 'undefined') {
    if (typeof port !== 'number' && typeof port !== 'string') {
      throw new ERR_INVALID_ARG_TYPE('options.port',
                                     ['number', 'string'], port);
    }
    validatePort(port);
  }
  port |= 0;


  if (autoSelectFamily != null) {
    validateBoolean(autoSelectFamily, 'options.autoSelectFamily');
  } else {
    autoSelectFamily = autoSelectFamilyDefault;
  }

  if (autoSelectFamilyAttemptTimeout != null) {
    validateInt32(autoSelectFamilyAttemptTimeout, 'options.autoSelectFamilyAttemptTimeout', 1);

    if (autoSelectFamilyAttemptTimeout < 10) {
      autoSelectFamilyAttemptTimeout = 10;
    }
  } else {
    autoSelectFamilyAttemptTimeout = autoSelectFamilyAttemptTimeoutDefault;
  }

  // If host is an IP, skip performing a lookup
  const addressType = isIP(host);
  if (addressType) {
    defaultTriggerAsyncIdScope(self[async_id_symbol], process.nextTick, () => {
      if (self.connecting)
        defaultTriggerAsyncIdScope(
          self[async_id_symbol],
          internalConnect,
          self, host, port, addressType, localAddress, localPort,
        );
    });
    return;
  }

  if (options.lookup != null)
    validateFunction(options.lookup, 'options.lookup');

  if (dns === undefined) dns = require('dns');
  const dnsopts = {
    family: socketToDnsFamily(options.family),
    hints: options.hints || 0,
  };

  if (!isWindows &&
      dnsopts.family !== 4 &&
      dnsopts.family !== 6 &&
      dnsopts.hints === 0) {
    dnsopts.hints = dns.ADDRCONFIG;
  }

  debug('connect: find host', host);
  debug('connect: dns options', dnsopts);
  self._host = host;
  const lookup = options.lookup || dns.lookup;

  if (dnsopts.family !== 4 && dnsopts.family !== 6 && !localAddress && autoSelectFamily) {
    debug('connect: autodetecting');

    dnsopts.all = true;
    defaultTriggerAsyncIdScope(self[async_id_symbol], function() {
      lookupAndConnectMultiple(
        self,
        async_id_symbol,
        lookup,
        host,
        options,
        dnsopts,
        port,
        localAddress,
        localPort,
        autoSelectFamilyAttemptTimeout,
      );
    });

    return;
  }

  defaultTriggerAsyncIdScope(self[async_id_symbol], function() {
    lookup(host, dnsopts, function emitLookup(err, ip, addressType) {
      self.emit('lookup', err, ip, addressType, host);

      // It's possible we were destroyed while looking this up.
      // XXX it would be great if we could cancel the promise returned by
      // the look up.
      if (!self.connecting) return;

      if (err) {
        // net.createConnection() creates a net.Socket object and immediately
        // calls net.Socket.connect() on it (that's us). There are no event
        // listeners registered yet so defer the error event to the next tick.
        process.nextTick(connectErrorNT, self, err);
      } else if (!isIP(ip)) {
        err = new ERR_INVALID_IP_ADDRESS(ip);
        process.nextTick(connectErrorNT, self, err);
      } else if (addressType !== 4 && addressType !== 6) {
        err = new ERR_INVALID_ADDRESS_FAMILY(addressType,
                                             options.host,
                                             options.port);
        process.nextTick(connectErrorNT, self, err);
      } else {
        self._unrefTimer();
        defaultTriggerAsyncIdScope(
          self[async_id_symbol],
          internalConnect,
          self, ip, port, addressType, localAddress, localPort,
        );
      }
    });
  });
}

function lookupAndConnectMultiple(
  self, async_id_symbol, lookup, host, options, dnsopts, port, localAddress, localPort, timeout,
) {
  defaultTriggerAsyncIdScope(self[async_id_symbol], function emitLookup() {
    lookup(host, dnsopts, function emitLookup(err, addresses) {
      // It's possible we were destroyed while looking this up.
      // XXX it would be great if we could cancel the promise returned by
      // the look up.
      if (!self.connecting) {
        return;
      } else if (err) {
        self.emit('lookup', err, undefined, undefined, host);

        // net.createConnection() creates a net.Socket object and immediately
        // calls net.Socket.connect() on it (that's us). There are no event
        // listeners registered yet so defer the error event to the next tick.
        process.nextTick(connectErrorNT, self, err);
        return;
      }

      // Filter addresses by only keeping the one which are either IPv4 or IPV6.
      // The first valid address determines which group has preference on the
      // alternate family sorting which happens later.
      const validAddresses = [[], []];
      const validIps = [[], []];
      let destinations;
      for (let i = 0, l = addresses.length; i < l; i++) {
        const address = addresses[i];
        const { address: ip, family: addressType } = address;
        self.emit('lookup', err, ip, addressType, host);
        // It's possible we were destroyed while looking this up.
        if (!self.connecting) {
          return;
        }
        if (isIP(ip) && (addressType === 4 || addressType === 6)) {
          destinations ||= addressType === 6 ? { 6: 0, 4: 1 } : { 4: 0, 6: 1 };

          const destination = destinations[addressType];

          // Only try an address once
          if (!ArrayPrototypeIncludes(validIps[destination], ip)) {
            ArrayPrototypePush(validAddresses[destination], address);
            ArrayPrototypePush(validIps[destination], ip);
          }
        }
      }


      // When no AAAA or A records are available, fail on the first one
      if (!validAddresses[0].length && !validAddresses[1].length) {
        const { address: firstIp, family: firstAddressType } = addresses[0];

        if (!isIP(firstIp)) {
          err = new ERR_INVALID_IP_ADDRESS(firstIp);
          process.nextTick(connectErrorNT, self, err);
        } else if (firstAddressType !== 4 && firstAddressType !== 6) {
          err = new ERR_INVALID_ADDRESS_FAMILY(firstAddressType,
                                               options.host,
                                               options.port);
          process.nextTick(connectErrorNT, self, err);
        }

        return;
      }

      // Sort addresses alternating families
      const toAttempt = [];
      for (let i = 0, l = MathMax(validAddresses[0].length, validAddresses[1].length); i < l; i++) {
        if (i in validAddresses[0]) {
          ArrayPrototypePush(toAttempt, validAddresses[0][i]);
        }
        if (i in validAddresses[1]) {
          ArrayPrototypePush(toAttempt, validAddresses[1][i]);
        }
      }

      if (toAttempt.length === 1) {
        debug('connect/multiple: only one address found, switching back to single connection');
        const { address: ip, family: addressType } = toAttempt[0];

        self._unrefTimer();
        defaultTriggerAsyncIdScope(
          self[async_id_symbol],
          internalConnect,
          self,
          ip,
          port,
          addressType,
          localAddress,
          localPort,
        );

        return;
      }

      self.autoSelectFamilyAttemptedAddresses = [];
      debug('connect/multiple: will try the following addresses', toAttempt);

      const context = {
        socket: self,
        addresses: toAttempt,
        current: 0,
        port,
        localPort,
        timeout,
        [kTimeout]: null,
        errors: [],
      };

      self._unrefTimer();
      defaultTriggerAsyncIdScope(self[async_id_symbol], internalConnectMultiple, context);
    });
  });
}

function connectErrorNT(self, err) {
  self.destroy(err);
}


Socket.prototype.ref = function() {
  if (!this._handle) {
    this.once('connect', this.ref);
    return this;
  }

  if (typeof this._handle.ref === 'function') {
    this._handle.ref();
  }

  return this;
};


Socket.prototype.unref = function() {
  if (!this._handle) {
    this.once('connect', this.unref);
    return this;
  }

  if (typeof this._handle.unref === 'function') {
    this._handle.unref();
  }

  return this;
};


function afterConnect(status, handle, req, readable, writable) {
  const self = handle[owner_symbol];

  // Callback may come after call to destroy
  if (self.destroyed) {
    return;
  }

  debug('afterConnect');

  assert(self.connecting);
  self.connecting = false;
  self._sockname = null;

  if (status === 0) {
    if (self.readable && !readable) {
      self.push(null);
      self.read();
    }
    if (self.writable && !writable) {
      self.end();
    }
    self._unrefTimer();

    if (self[kSetNoDelay] && self._handle.setNoDelay) {
      self._handle.setNoDelay(true);
    }

    if (self[kSetKeepAlive] && self._handle.setKeepAlive) {
      self._handle.setKeepAlive(true, self[kSetKeepAliveInitialDelay]);
    }

    self.emit('connect');
    self.emit('ready');

    // Start the first read, or get an immediate EOF.
    // this doesn't actually consume any bytes, because len=0.
    if (readable && !self.isPaused())
      self.read(0);
    if (self[kPerfHooksNetConnectContext] && hasObserver('net')) {
      stopPerf(self, kPerfHooksNetConnectContext);
    }
  } else {
    let details;
    if (req.localAddress && req.localPort) {
      details = req.localAddress + ':' + req.localPort;
    }
    const ex = new ExceptionWithHostPort(status,
                                         'connect',
                                         req.address,
                                         req.port,
                                         details);
    if (details) {
      ex.localAddress = req.localAddress;
      ex.localPort = req.localPort;
    }

    self.emit('connectionAttemptFailed', req.address, req.port, req.addressType, ex);
    self.destroy(ex);
  }
}

function addClientAbortSignalOption(self, options) {
  validateAbortSignal(options.signal, 'options.signal');
  const { signal } = options;
  let disposable;

  function onAbort() {
    disposable?.[SymbolDispose]();
    self._aborted = true;
  }

  if (signal.aborted) {
    process.nextTick(onAbort);
  } else {
    process.nextTick(() => {
      disposable = addAbortListener(signal, onAbort);
    });
  }
}

function createConnectionError(req, status) {
  let details;

  if (req.localAddress && req.localPort) {
    details = req.localAddress + ':' + req.localPort;
  }

  const ex = new ExceptionWithHostPort(status,
                                       'connect',
                                       req.address,
                                       req.port,
                                       details);
  if (details) {
    ex.localAddress = req.localAddress;
    ex.localPort = req.localPort;
  }

  return ex;
}

function afterConnectMultiple(context, current, status, handle, req, readable, writable) {
  debug('connect/multiple: connection attempt to %s:%s completed with status %s', req.address, req.port, status);

  // Make sure another connection is not spawned
  clearTimeout(context[kTimeout]);

  // One of the connection has completed and correctly dispatched but after timeout, ignore this one
  if (status === 0 && current !== context.current - 1) {
    debug('connect/multiple: ignoring successful but timedout connection to %s:%s', req.address, req.port);
    handle.close();
    return;
  }

  const self = context.socket;

  // Some error occurred, add to the list of exceptions
  if (status !== 0) {
    const ex = createConnectionError(req, status);
    ArrayPrototypePush(context.errors, ex);

    self.emit('connectionAttemptFailed', req.address, req.port, req.addressType, ex);

    // Try the next address, unless we were aborted
    if (context.socket.connecting) {
      internalConnectMultiple(context, status === UV_ECANCELED);
    }

    return;
  }

  if (hasObserver('net')) {
    startPerf(
      self,
      kPerfHooksNetConnectContext,
      { type: 'net', name: 'connect', detail: { host: req.address, port: req.port } },
    );
  }

  afterConnect(status, self._handle, req, readable, writable);
}

function internalConnectMultipleTimeout(context, req, handle) {
  debug('connect/multiple: connection to %s:%s timed out', req.address, req.port);
  context.socket.emit('connectionAttemptTimeout', req.address, req.port, req.addressType);

  req.oncomplete = undefined;
  ArrayPrototypePush(context.errors, createConnectionError(req, UV_ETIMEDOUT));
  handle.close();

  // Try the next address, unless we were aborted
  if (context.socket.connecting) {
    internalConnectMultiple(context);
  }
}

function addServerAbortSignalOption(self, options) {
  if (options?.signal === undefined) {
    return;
  }
  validateAbortSignal(options.signal, 'options.signal');
  const { signal } = options;
  const onAborted = () => {
    self.close();
  };
  if (signal.aborted) {
    process.nextTick(onAborted);
  } else {
    const disposable = addAbortListener(signal, onAborted);
    self.once('close', disposable[SymbolDispose]);
  }
}

function Server(options, connectionListener) {
  if (!(this instanceof Server))
    return new Server(options, connectionListener);

  EventEmitter.call(this);

  if (typeof options === 'function') {
    connectionListener = options;
    options = kEmptyObject;
    this.on('connection', connectionListener);
  } else if (options == null || typeof options === 'object') {
    options = { ...options };

    if (typeof connectionListener === 'function') {
      this.on('connection', connectionListener);
    }
  } else {
    throw new ERR_INVALID_ARG_TYPE('options', 'Object', options);
  }
  if (typeof options.keepAliveInitialDelay !== 'undefined') {
    validateNumber(
      options.keepAliveInitialDelay, 'options.keepAliveInitialDelay',
    );

    if (options.keepAliveInitialDelay < 0) {
      options.keepAliveInitialDelay = 0;
    }
  }
  if (typeof options.highWaterMark !== 'undefined') {
    validateNumber(
      options.highWaterMark, 'options.highWaterMark',
    );

    if (options.highWaterMark < 0) {
      options.highWaterMark = getDefaultHighWaterMark();
    }
  }

  this._connections = 0;

  this[async_id_symbol] = -1;
  this._handle = null;
  this._usingWorkers = false;
  this._workers = [];
  this._unref = false;
  this._listeningId = 1;

  this.allowHalfOpen = options.allowHalfOpen || false;
  this.pauseOnConnect = !!options.pauseOnConnect;
  this.noDelay = Boolean(options.noDelay);
  this.keepAlive = Boolean(options.keepAlive);
  this.keepAliveInitialDelay = ~~(options.keepAliveInitialDelay / 1000);
  this.highWaterMark = options.highWaterMark ?? getDefaultHighWaterMark();
  if (options.blockList) {
    if (!module.exports.BlockList.isBlockList(options.blockList)) {
      throw new ERR_INVALID_ARG_TYPE('options.blockList', 'net.BlockList', options.blockList);
    }
    this.blockList = options.blockList;
  }
}
ObjectSetPrototypeOf(Server.prototype, EventEmitter.prototype);
ObjectSetPrototypeOf(Server, EventEmitter);


function toNumber(x) { return (x = Number(x)) >= 0 ? x : false; }

// Returns handle if it can be created, or error code if it can't
function createServerHandle(address, port, addressType, fd, flags) {
  let err = 0;
  // Assign handle in listen, and clean up if bind or listen fails
  let handle;

  let isTCP = false;
  if (typeof fd === 'number' && fd >= 0) {
    try {
      handle = createHandle(fd, true);
    } catch (e) {
      // Not a fd we can listen on.  This will trigger an error.
      debug('listen invalid fd=%d:', fd, e.message);
      return UV_EINVAL;
    }

    err = handle.open(fd);
    if (err)
      return err;

    assert(!address && !port);
  } else if (port === -1 && addressType === -1) {
    handle = new Pipe(PipeConstants.SERVER);
    if (isWindows) {
      const instances = NumberParseInt(process.env.NODE_PENDING_PIPE_INSTANCES);
      if (!NumberIsNaN(instances)) {
        handle.setPendingInstances(instances);
      }
    }
  } else {
    handle = new TCP(TCPConstants.SERVER);
    isTCP = true;
  }

  if (address || port || isTCP) {
    debug('bind to', address || 'any');
    if (!address) {
      // Try binding to ipv6 first
      err = handle.bind6(DEFAULT_IPV6_ADDR, port, flags);
      if (err) {
        handle.close();
        // Fallback to ipv4
        return createServerHandle(DEFAULT_IPV4_ADDR, port, undefined, undefined, flags);
      }
    } else if (addressType === 6) {
      err = handle.bind6(address, port, flags);
    } else {
      err = handle.bind(address, port, flags);
    }
  }

  if (err) {
    handle.close();
    return err;
  }

  return handle;
}

function setupListenHandle(address, port, addressType, backlog, fd, flags) {
  debug('setupListenHandle', address, port, addressType, backlog, fd);

  // If there is not yet a handle, we need to create one and bind.
  // In the case of a server sent via IPC, we don't need to do this.
  if (this._handle) {
    debug('setupListenHandle: have a handle already');
  } else {
    debug('setupListenHandle: create a handle');

    let rval = null;

    // Try to bind to the unspecified IPv6 address, see if IPv6 is available
    if (!address && typeof fd !== 'number') {
      rval = createServerHandle(DEFAULT_IPV6_ADDR, port, 6, fd, flags);

      if (typeof rval === 'number') {
        rval = null;
        address = DEFAULT_IPV4_ADDR;
        addressType = 4;
      } else {
        address = DEFAULT_IPV6_ADDR;
        addressType = 6;
      }
    }

    if (rval === null)
      rval = createServerHandle(address, port, addressType, fd, flags);

    if (typeof rval === 'number') {
      const error = new UVExceptionWithHostPort(rval, 'listen', address, port);

      if (netServerListen.hasSubscribers) {
        netServerListen.error.publish({ server: this, error });
      }

      process.nextTick(emitErrorNT, this, error);
      return;
    }
    this._handle = rval;
  }

  this[async_id_symbol] = getNewAsyncId(this._handle);
  this._handle.onconnection = onconnection;
  this._handle[owner_symbol] = this;

  // Use a backlog of 512 entries. We pass 511 to the listen() call because
  // the kernel does: backlogsize = roundup_pow_of_two(backlogsize + 1);
  // which will thus give us a backlog of 512 entries.
  const err = this._handle.listen(backlog || 511);

  if (err) {
    const ex = new UVExceptionWithHostPort(err, 'listen', address, port);
    this._handle.close();
    this._handle = null;

    if (netServerListen.hasSubscribers) {
      netServerListen.error.publish({ server: this, error: ex });
    }

    defaultTriggerAsyncIdScope(this[async_id_symbol],
                               process.nextTick,
                               emitErrorNT,
                               this,
                               ex);
    return;
  }

  if (netServerListen.hasSubscribers) {
    netServerListen.asyncEnd.publish({ server: this });
  }

  // Generate connection key, this should be unique to the connection
  this._connectionKey = addressType + ':' + address + ':' + port;

  // Unref the handle if the server was unref'ed prior to listening
  if (this._unref)
    this.unref();

  defaultTriggerAsyncIdScope(this[async_id_symbol],
                             process.nextTick,
                             emitListeningNT,
                             this);
}

Server.prototype._listen2 = setupListenHandle;  // legacy alias

function emitErrorNT(self, err) {
  self.emit('error', err);
}


function emitListeningNT(self) {
  // Ensure handle hasn't closed
  if (self._handle)
    self.emit('listening');
}


function listenInCluster(server, address, port, addressType,
                         backlog, fd, exclusive, flags, options) {
  exclusive = !!exclusive;

  if (cluster === undefined) cluster = require('cluster');

  if (cluster.isPrimary || exclusive) {
    // Will create a new handle
    // _listen2 sets up the listened handle, it is still named like this
    // to avoid breaking code that wraps this method
    server._listen2(address, port, addressType, backlog, fd, flags);
    return;
  }

  const serverQuery = {
    address: address,
    port: port,
    addressType: addressType,
    fd: fd,
    flags,
    backlog,
    ...options,
  };
  const listeningId = server._listeningId;
  // Get the primary's server handle, and listen on it
  cluster._getServer(server, serverQuery, listenOnPrimaryHandle);
  function listenOnPrimaryHandle(err, handle) {
    if (listeningId !== server._listeningId) {
      handle.close();
      return;
    }
    err = checkBindError(err, port, handle);

    if (err) {
      const ex = new ExceptionWithHostPort(err, 'bind', address, port);
      return server.emit('error', ex);
    }
    // If there was a handle, just close it to avoid fd leak
    // but it doesn't look like that's going to happen right now
    if (server._handle) {
      server._handle.close();
    }
    // Reuse primary's server handle
    server._handle = handle;
    // _listen2 sets up the listened handle, it is still named like this
    // to avoid breaking code that wraps this method
    server._listen2(address, port, addressType, backlog, fd, flags);
  }
}


Server.prototype.listen = function(...args) {
  const normalized = normalizeArgs(args);
  let options = normalized[0];
  const cb = normalized[1];

  if (this._handle) {
    throw new ERR_SERVER_ALREADY_LISTEN();
  }

  if (netServerListen.hasSubscribers) {
    netServerListen.asyncStart.publish({ server: this, options });
  }

  if (cb !== null) {
    this.once('listening', cb);
  }
  const backlogFromArgs =
    // (handle, backlog) or (path, backlog) or (port, backlog)
    toNumber(args.length > 1 && args[1]) ||
    toNumber(args.length > 2 && args[2]);  // (port, host, backlog)

  options = options._handle || options.handle || options;
  const flags = getFlags(options);
  //  Refresh the id to make the previous call invalid
  this._listeningId++;
  // (handle[, backlog][, cb]) where handle is an object with a handle
  if (options instanceof TCP) {
    this._handle = options;
    this[async_id_symbol] = this._handle.getAsyncId();
    listenInCluster(this, null, -1, -1, backlogFromArgs, undefined, true);
    return this;
  }
  addServerAbortSignalOption(this, options);
  // (handle[, backlog][, cb]) where handle is an object with a fd
  if (typeof options.fd === 'number' && options.fd >= 0) {
    listenInCluster(this, null, null, null, backlogFromArgs, options.fd);
    return this;
  }

  // ([port][, host][, backlog][, cb]) where port is omitted,
  // that is, listen(), listen(null), listen(cb), or listen(null, cb)
  // or (options[, cb]) where options.port is explicitly set as undefined or
  // null, bind to an arbitrary unused port
  if (args.length === 0 || typeof args[0] === 'function' ||
      (typeof options.port === 'undefined' && 'port' in options) ||
      options.port === null) {
    options.port = 0;
  }
  // ([port][, host][, backlog][, cb]) where port is specified
  // or (options[, cb]) where options.port is specified
  // or if options.port is normalized as 0 before
  let backlog;
  if (typeof options.port === 'number' || typeof options.port === 'string') {
    validatePort(options.port, 'options.port');
    backlog = options.backlog || backlogFromArgs;
    if (options.reusePort === true) {
      options.exclusive = true;
    }
    // start TCP server listening on host:port
    if (options.host) {
      lookupAndListen(this, options.port | 0, options.host, backlog,
                      options.exclusive, flags);
    } else { // Undefined host, listens on unspecified address
      // Default addressType 4 will be used to search for primary server
      listenInCluster(this, null, options.port | 0, 4,
                      backlog, undefined, options.exclusive, flags);
    }
    return this;
  }

  // (path[, backlog][, cb]) or (options[, cb])
  // where path or options.path is a UNIX domain socket or Windows pipe
  if (options.path && isPipeName(options.path)) {
    // We can not call fchmod on abstract unix socket
    if (options.path[0] === '\0' &&
        (options.readableAll || options.writableAll)) {
      const msg = 'can not set readableAll or writableAllt to true when path is abstract unix socket';
      throw new ERR_INVALID_ARG_VALUE('options', options, msg);
    }
    const pipeName = this._pipeName = options.path;
    backlog = options.backlog || backlogFromArgs;
    listenInCluster(this,
                    pipeName,
                    -1,
                    -1,
                    backlog,
                    undefined,
                    options.exclusive,
                    undefined,
                    {
                      readableAll: options.readableAll,
                      writableAll: options.writableAll,
                    });

    if (!this._handle) {
      // Failed and an error shall be emitted in the next tick.
      // Therefore, we directly return.
      return this;
    }

    let mode = 0;
    if (options.readableAll === true)
      mode |= PipeConstants.UV_READABLE;
    if (options.writableAll === true)
      mode |= PipeConstants.UV_WRITABLE;
    if (mode !== 0) {
      const err = this._handle.fchmod(mode);
      if (err) {
        this._handle.close();
        this._handle = null;
        throw new ErrnoException(err, 'uv_pipe_chmod');
      }
    }
    return this;
  }

  if (!(('port' in options) || ('path' in options))) {
    throw new ERR_INVALID_ARG_VALUE('options', options,
                                    'must have the property "port" or "path"');
  }

  throw new ERR_INVALID_ARG_VALUE('options', options);
};

function isIpv6LinkLocal(ip) {
  if (!isIPv6(ip)) { return false; }

  const ipv6Buffer = convertIpv6StringToBuffer(ip);
  const firstByte = ipv6Buffer[0];  // The first 8 bits
  const secondByte = ipv6Buffer[1]; // The next 8 bits

  // The link-local prefix is `1111111010`, which in hexadecimal is `fe80`
  // First 8 bits (firstByte) should be `11111110` (0xfe)
  // The next 2 bits of the second byte should be `10` (0x80)

  const isFirstByteCorrect = (firstByte === 0xfe); // 0b11111110 == 0xfe
  const isSecondByteCorrect = (secondByte & 0xc0) === 0x80; // 0b10xxxxxx == 0x80

  return isFirstByteCorrect && isSecondByteCorrect;
}

function filterOnlyValidAddress(addresses) {
  // Return the first non IPV6 link-local address if present
  for (const address of addresses) {
    if (!isIpv6LinkLocal(address.address)) {
      return address;
    }
  }

  // Otherwise return the first address
  return addresses[0];
}

function lookupAndListen(self, port, address, backlog,
                         exclusive, flags) {
  if (dns === undefined) dns = require('dns');
  const listeningId = self._listeningId;

  dns.lookup(address, { all: true }, (err, addresses) => {
    if (listeningId !== self._listeningId) {
      return;
    }
    if (err) {
      self.emit('error', err);
    } else {
      const validAddress = filterOnlyValidAddress(addresses);
      const family = validAddress?.family || 4;

      listenInCluster(self, validAddress.address, port, family,
                      backlog, undefined, exclusive, flags);
    }
  });
}

ObjectDefineProperty(Server.prototype, 'listening', {
  __proto__: null,
  get: function() {
    return !!this._handle;
  },
  configurable: true,
  enumerable: true,
});

Server.prototype.address = function() {
  if (this._handle?.getsockname) {
    const out = {};
    const err = this._handle.getsockname(out);
    if (err) {
      throw new ErrnoException(err, 'address');
    }
    return out;
  } else if (this._pipeName) {
    return this._pipeName;
  }
  return null;
};

function onconnection(err, clientHandle) {
  const handle = this;
  const self = handle[owner_symbol];

  debug('onconnection');

  if (err) {
    self.emit('error', new ErrnoException(err, 'accept'));
    return;
  }

  if (self.maxConnections != null && self._connections >= self.maxConnections) {
    if (clientHandle.getsockname || clientHandle.getpeername) {
      const data = { __proto__: null };
      if (clientHandle.getsockname) {
        const localInfo = { __proto__: null };
        clientHandle.getsockname(localInfo);
        data.localAddress = localInfo.address;
        data.localPort = localInfo.port;
        data.localFamily = localInfo.family;
      }
      if (clientHandle.getpeername) {
        const remoteInfo = { __proto__: null };
        clientHandle.getpeername(remoteInfo);
        data.remoteAddress = remoteInfo.address;
        data.remotePort = remoteInfo.port;
        data.remoteFamily = remoteInfo.family;
      }
      self.emit('drop', data);
    } else {
      self.emit('drop');
    }
    clientHandle.close();
    return;
  }
  if (self.blockList && typeof clientHandle.getpeername === 'function') {
    const remoteInfo = { __proto__: null };
    clientHandle.getpeername(remoteInfo);
    const addressType = isIP(remoteInfo.address);
    if (addressType && self.blockList.check(remoteInfo.address, `ipv${addressType}`)) {
      clientHandle.close();
      return;
    }
  }
  const socket = new Socket({
    handle: clientHandle,
    allowHalfOpen: self.allowHalfOpen,
    pauseOnCreate: self.pauseOnConnect,
    readable: true,
    writable: true,
    readableHighWaterMark: self.highWaterMark,
    writableHighWaterMark: self.highWaterMark,
  });

  if (self.noDelay && clientHandle.setNoDelay) {
    socket[kSetNoDelay] = true;
    clientHandle.setNoDelay(true);
  }
  if (self.keepAlive && clientHandle.setKeepAlive) {
    socket[kSetKeepAlive] = true;
    socket[kSetKeepAliveInitialDelay] = self.keepAliveInitialDelay;
    clientHandle.setKeepAlive(true, self.keepAliveInitialDelay);
  }

  self._connections++;
  socket.server = self;
  socket._server = self;
  self.emit('connection', socket);
  if (netServerSocketChannel.hasSubscribers) {
    netServerSocketChannel.publish({
      socket,
    });
  }
}

/**
 * Gets the number of concurrent connections on the server
 * @param {Function} cb
 * @returns {Server}
 */

Server.prototype.getConnections = function(cb) {
  const self = this;

  function end(err, connections) {
    defaultTriggerAsyncIdScope(self[async_id_symbol],
                               process.nextTick,
                               cb,
                               err,
                               connections);
  }

  if (!this._usingWorkers) {
    end(null, this._connections);
    return this;
  }

  // Poll workers
  let left = this._workers.length;
  let total = this._connections;

  function oncount(err, count) {
    if (err) {
      left = -1;
      return end(err);
    }

    total += count;
    if (--left === 0) return end(null, total);
  }

  for (let n = 0; n < this._workers.length; n++) {
    this._workers[n].getConnections(oncount);
  }

  return this;
};


Server.prototype.close = function(cb) {
  this._listeningId++;
  if (typeof cb === 'function') {
    if (!this._handle) {
      this.once('close', function close() {
        cb(new ERR_SERVER_NOT_RUNNING());
      });
    } else {
      this.once('close', cb);
    }
  }

  if (this._handle) {
    this._handle.close();
    this._handle = null;
  }

  if (this._usingWorkers) {
    let left = this._workers.length;
    const onWorkerClose = () => {
      if (--left !== 0) return;

      this._connections = 0;
      this._emitCloseIfDrained();
    };

    // Increment connections to be sure that, even if all sockets will be closed
    // during polling of workers, `close` event will be emitted only once.
    this._connections++;

    // Poll workers
    for (let n = 0; n < this._workers.length; n++)
      this._workers[n].close(onWorkerClose);
  } else {
    this._emitCloseIfDrained();
  }

  return this;
};

Server.prototype[SymbolAsyncDispose] = async function() {
  if (!this._handle) {
    return;
  }
  return FunctionPrototypeCall(promisify(this.close), this);
};

Server.prototype._emitCloseIfDrained = function() {
  debug('SERVER _emitCloseIfDrained');

  if (this._handle || this._connections) {
    debug('SERVER handle? %j   connections? %d',
          !!this._handle, this._connections);
    return;
  }

  defaultTriggerAsyncIdScope(this[async_id_symbol],
                             process.nextTick,
                             emitCloseNT,
                             this);
};


function emitCloseNT(self) {
  debug('SERVER: emit close');
  self.emit('close');
}


Server.prototype[EventEmitter.captureRejectionSymbol] = function(
  err, event, sock) {

  switch (event) {
    case 'connection':
      sock.destroy(err);
      break;
    default:
      this.emit('error', err);
  }
};


// Legacy alias on the C++ wrapper object. This is not public API, so we may
// want to runtime-deprecate it at some point. There's no hurry, though.
ObjectDefineProperty(TCP.prototype, 'owner', {
  __proto__: null,
  get() { return this[owner_symbol]; },
  set(v) { return this[owner_symbol] = v; },
});

ObjectDefineProperty(Socket.prototype, '_handle', {
  __proto__: null,
  get() { return this[kHandle]; },
  set(v) { return this[kHandle] = v; },
});

Server.prototype._setupWorker = function(socketList) {
  this._usingWorkers = true;
  this._workers.push(socketList);
  socketList.once('exit', (socketList) => {
    const index = ArrayPrototypeIndexOf(this._workers, socketList);
    this._workers.splice(index, 1);
  });
};

Server.prototype.ref = function() {
  this._unref = false;

  if (this._handle)
    this._handle.ref();

  return this;
};

Server.prototype.unref = function() {
  this._unref = true;

  if (this._handle)
    this._handle.unref();

  return this;
};

let _setSimultaneousAccepts;
let warnSimultaneousAccepts = true;

if (isWindows) {
  let simultaneousAccepts;

  _setSimultaneousAccepts = function(handle) {
    if (warnSimultaneousAccepts) {
      process.emitWarning(
        'net._setSimultaneousAccepts() is deprecated and will be removed.',
        'DeprecationWarning', 'DEP0121');
      warnSimultaneousAccepts = false;
    }
    if (handle === undefined) {
      return;
    }

    if (simultaneousAccepts === undefined) {
      simultaneousAccepts = (process.env.NODE_MANY_ACCEPTS &&
                             process.env.NODE_MANY_ACCEPTS !== '0');
    }

    if (handle._simultaneousAccepts !== simultaneousAccepts) {
      handle.setSimultaneousAccepts(!!simultaneousAccepts);
      handle._simultaneousAccepts = simultaneousAccepts;
    }
  };
} else {
  _setSimultaneousAccepts = function() {
    if (warnSimultaneousAccepts) {
      process.emitWarning(
        'net._setSimultaneousAccepts() is deprecated and will be removed.',
        'DeprecationWarning', 'DEP0121');
      warnSimultaneousAccepts = false;
    }
  };
}

module.exports = {
  _createServerHandle: createServerHandle,
  _normalizeArgs: normalizeArgs,
  _setSimultaneousAccepts,
  get BlockList() {
    BlockList ??= require('internal/blocklist').BlockList;
    return BlockList;
  },
  get SocketAddress() {
    SocketAddress ??= require('internal/socketaddress').SocketAddress;
    return SocketAddress;
  },
  connect,
  createConnection: connect,
  createServer,
  isIP: isIP,
  isIPv4: isIPv4,
  isIPv6: isIPv6,
  Server,
  Socket,
  Stream: Socket, // Legacy naming
  getDefaultAutoSelectFamily,
  setDefaultAutoSelectFamily,
  getDefaultAutoSelectFamilyAttemptTimeout,
  setDefaultAutoSelectFamilyAttemptTimeout,
};
