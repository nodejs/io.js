'use strict';

const {
  Symbol,
} = primordials;

const { setImmediate } = require('timers');
const assert = require('internal/assert');
const { Socket } = require('net');
const { JSStream } = internalBinding('js_stream');
const uv = internalBinding('uv');
let debug = require('internal/util/debuglog').debuglog(
  'stream_socket',
  (fn) => {
    debug = fn;
  },
);
const { owner_symbol } = require('internal/async_hooks').symbols;
const { ERR_STREAM_WRAP } = require('internal/errors').codes;
const { kBoundSession } = require('internal/stream_base_commons');

const kCurrentWriteRequest = Symbol('kCurrentWriteRequest');
const kCurrentShutdownRequest = Symbol('kCurrentShutdownRequest');
const kPendingShutdownRequest = Symbol('kPendingShutdownRequest');
const kPendingClose = Symbol('kPendingClose');

function isClosing() { return this[owner_symbol].isClosing(); }

function onreadstart() { return this[owner_symbol].readStart(); }

function onreadstop() { return this[owner_symbol].readStop(); }

function onshutdown(req) { return this[owner_symbol].doShutdown(req); }

function onwrite(req, bufs) { return this[owner_symbol].doWrite(req, bufs); }

/* This class serves as a wrapper for when the C++ side of Node wants access
 * to a standard JS stream. For example, TLS or HTTP do not operate on network
 * resources conceptually, although that is the common case and what we are
 * optimizing for; in theory, they are completely composable and can work with
 * any stream resource they see.
 *
 * For the common case, i.e. a TLS socket wrapping around a net.Socket, we
 * can skip going through the JS layer and let TLS access the raw C++ handle
 * of a net.Socket. The flipside of this is that, to maintain composability,
 * we need a way to create "fake" net.Socket instances that call back into a
 * "real" JavaScript stream. JSStreamSocket is exactly this.
 */
class JSStreamSocket extends Socket {
  constructor(stream) {
    const handle = new JSStream();
    handle.close = (cb) => {
      debug('close');
      this.doClose(cb);
    };
    // Inside of the following functions, `this` refers to the handle
    // and `this[owner_symbol]` refers to this JSStreamSocket instance.
    handle.isClosing = isClosing;
    handle.onreadstart = onreadstart;
    handle.onreadstop = onreadstop;
    handle.onshutdown = onshutdown;
    handle.onwrite = onwrite;

    stream.pause();
    stream.on('error', (err) => this.emit('error', err));
    const ondata = (chunk) => {
      if (typeof chunk === 'string' ||
          stream.readableObjectMode === true) {
        // Make sure that no further `data` events will happen.
        stream.pause();
        stream.removeListener('data', ondata);

        this.emit('error', new ERR_STREAM_WRAP());
        return;
      }

      debug('data', chunk.length);
      if (this._handle)
        this._handle.readBuffer(chunk);
    };
    stream.on('data', ondata);
    stream.once('end', () => {
      debug('end');
      if (this._handle)
        this._handle.emitEOF();
    });
    // Some `Stream` don't pass `hasError` parameters when closed.
    stream.once('close', () => {
      // Errors emitted from `stream` have also been emitted to this instance
      // so that we don't pass errors to `destroy()` again.
      this.destroy();
    });

    super({ handle, manualStart: true });
    this.stream = stream;
    this[kCurrentWriteRequest] = null;
    this[kCurrentShutdownRequest] = null;
    this[kPendingShutdownRequest] = null;
    this[kPendingClose] = false;
    this.readable = stream.readable;
    this.writable = stream.writable;

    // Start reading.
    this.read(0);
  }

  // Allow legacy requires in the test suite to keep working:
  //   const { StreamWrap } = require('internal/js_stream_socket')
  static get StreamWrap() {
    return JSStreamSocket;
  }

  isClosing() {
    return !this.readable || !this.writable;
  }

  readStart() {
    this.stream.resume();
    return 0;
  }

  readStop() {
    this.stream.pause();
    return 0;
  }

  doShutdown(req) {
    // TODO(addaleax): It might be nice if we could get into a state where
    // DoShutdown() is not called on streams while a write is still pending.
    //
    // Currently, the only part of the code base where that happens is the
    // TLS implementation, which calls both DoWrite() and DoShutdown() on the
    // underlying network stream inside of its own DoShutdown() method.
    // Working around that on the native side is not quite trivial (yet?),
    // so for now that is supported here.

    if (this[kCurrentWriteRequest] !== null) {
      this[kPendingShutdownRequest] = req;
      return 0;
    }

    assert(this[kCurrentWriteRequest] === null);
    assert(this[kCurrentShutdownRequest] === null);
    this[kCurrentShutdownRequest] = req;

    if (this[kPendingClose]) {
      // If doClose is pending, the stream & this._handle are gone. We can't do
      // anything. doClose will call finishShutdown with ECANCELED for us shortly.
      return 0;
    }

    const handle = this._handle;
    assert(handle !== null);

    process.nextTick(() => {
      // Ensure that write is dispatched asynchronously.
      this.stream.end(() => {
        this.finishShutdown(handle, 0);
      });
    });
    return 0;
  }

  // handle === this._handle except when called from doClose().
  finishShutdown(handle, errCode) {
    // The shutdown request might already have been cancelled.
    if (this[kCurrentShutdownRequest] === null)
      return;
    const req = this[kCurrentShutdownRequest];
    this[kCurrentShutdownRequest] = null;
    handle.finishShutdown(req, errCode);
  }

  doWrite(req, bufs) {
    assert(this[kCurrentWriteRequest] === null);
    assert(this[kCurrentShutdownRequest] === null);

    if (this[kPendingClose]) {
      // If doClose is pending, the stream & this._handle are gone. We can't do
      // anything. doClose will call finishWrite with ECANCELED for us shortly.
      this[kCurrentWriteRequest] = req; // Store req, for doClose to cancel
      return 0;
    } else if (this._handle === null) {
      // If this._handle is already null, there is nothing left to do with a
      // pending write request, so we discard it.
      return 0;
    }

    const handle = this._handle;

    const self = this;

    let pending = bufs.length;

    this.stream.cork();
    // Use `var` over `let` for performance optimization.
    // eslint-disable-next-line no-var
    for (var i = 0; i < bufs.length; ++i)
      this.stream.write(bufs[i], done);
    this.stream.uncork();

    // Only set the request here, because the `write()` calls could throw.
    this[kCurrentWriteRequest] = req;

    function done(err) {
      if (!err && --pending !== 0)
        return;

      // Ensure that this is called once in case of error
      pending = 0;

      let errCode = 0;
      if (err) {
        errCode = uv[`UV_${err.code}`] || uv.UV_EPIPE;
      }

      // Ensure that write was dispatched
      setImmediate(() => {
        self.finishWrite(handle, errCode);
      });
    }

    return 0;
  }

  // handle === this._handle except when called from doClose().
  finishWrite(handle, errCode) {
    // The write request might already have been cancelled.
    if (this[kCurrentWriteRequest] === null)
      return;
    const req = this[kCurrentWriteRequest];
    this[kCurrentWriteRequest] = null;

    handle.finishWrite(req, errCode);
    if (this[kPendingShutdownRequest]) {
      const req = this[kPendingShutdownRequest];
      this[kPendingShutdownRequest] = null;
      this.doShutdown(req);
    }
  }

  doClose(cb) {
    this[kPendingClose] = true;

    const handle = this._handle;

    // When sockets of the "net" module destroyed, they will call
    // `this._handle.close()` which will also emit EOF if not emitted before.
    // This feature makes sockets on the other side emit "end" and "close"
    // even though we haven't called `end()`. As `stream` are likely to be
    // instances of `net.Socket`, calling `stream.destroy()` manually will
    // avoid issues that don't properly close wrapped connections.
    this.stream.destroy();

    setImmediate(() => {
      // Should be already set by net.js
      assert(this._handle === null);

      this.finishWrite(handle, uv.UV_ECANCELED);
      this.finishShutdown(handle, uv.UV_ECANCELED);

      this[kPendingClose] = false;

      cb();
    });
  }

  get [kBoundSession]() {
    return this.stream[kBoundSession];
  }

  set [kBoundSession](session) {
    this.stream[kBoundSession] = session;
  }
}

module.exports = JSStreamSocket;
