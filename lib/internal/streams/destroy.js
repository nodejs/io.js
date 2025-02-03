'use strict';

const {
  Symbol,
} = primordials;

const {
  AbortError,
  aggregateTwoErrors,
  codes: {
    ERR_MULTIPLE_CALLBACK,
  },
} = require('internal/errors');
const {
  kIsDestroyed,
  isDestroyed,
  isFinished,
  isServerRequest,
  kState,
  kErrorEmitted,
  kEmitClose,
  kClosed,
  kCloseEmitted,
  kConstructed,
  kDestroyed,
  kAutoDestroy,
  kErrored,
} = require('internal/streams/utils');

const kDestroy = Symbol('kDestroy');
const kConstruct = Symbol('kConstruct');

function checkError(err, w, r) {
  if (err) {
    // Avoid V8 leak, https://github.com/nodejs/node/pull/34103#issuecomment-652002364
    err.stack; // eslint-disable-line no-unused-expressions

    if (w && !w.errored) {
      w.errored = err;
    }
    if (r && !r.errored) {
      r.errored = err;
    }
  }
}

// Backwards compat. cb() is undocumented and unused in core but
// unfortunately might be used by modules.
function destroy(err, cb) {
  const r = this._readableState;
  const w = this._writableState;
  // With duplex streams we use the writable side for state.
  const s = w || r;

  if (
    (w && (w[kState] & kDestroyed) !== 0) ||
    (r && (r[kState] & kDestroyed) !== 0)
  ) {
    if (typeof cb === 'function') {
      cb();
    }

    return this;
  }


  // We set destroyed to true before firing error callbacks in order
  // to make it re-entrance safe in case destroy() is called within callbacks
  checkError(err, w, r);

  if (w) {
    w[kState] |= kDestroyed;
  }
  if (r) {
    r[kState] |= kDestroyed;
  }

  // If still constructing then defer calling _destroy.
  if ((s[kState] & kConstructed) === 0) {
    this.once(kDestroy, function(er) {
      _destroy(this, aggregateTwoErrors(er, err), cb);
    });
  } else {
    _destroy(this, err, cb);
  }

  return this;
}

function _destroy(self, err, cb) {
  let called = false;

  function onDestroy(err) {
    if (called) {
      return;
    }
    called = true;

    const r = self._readableState;
    const w = self._writableState;

    checkError(err, w, r);

    if (w) {
      w[kState] |= kClosed;
    }
    if (r) {
      r[kState] |= kClosed;
    }

    if (typeof cb === 'function') {
      cb(err);
    }

    if (err) {
      process.nextTick(emitErrorCloseNT, self, err);
    } else {
      process.nextTick(emitCloseNT, self);
    }
  }
  try {
    self._destroy(err || null, onDestroy);
  } catch (err) {
    onDestroy(err);
  }
}

function emitErrorCloseNT(self, err) {
  emitErrorNT(self, err);
  emitCloseNT(self);
}

function emitCloseNT(self) {
  const r = self._readableState;
  const w = self._writableState;

  if (w) {
    w[kState] |= kCloseEmitted;
  }
  if (r) {
    r[kState] |= kCloseEmitted;
  }

  if (
    (w && (w[kState] & kEmitClose) !== 0) ||
    (r && (r[kState] & kEmitClose) !== 0)
  ) {
    self.emit('close');
  }
}

function emitErrorNT(self, err) {
  const r = self._readableState;
  const w = self._writableState;

  if (
    (w && (w[kState] & kErrorEmitted) !== 0) ||
    (r && (r[kState] & kErrorEmitted) !== 0)
  ) {
    return;
  }

  if (w) {
    w[kState] |= kErrorEmitted;
  }
  if (r) {
    r[kState] |= kErrorEmitted;
  }

  self.emit('error', err);
}

function undestroy() {
  const r = this._readableState;
  const w = this._writableState;

  if (r) {
    r.constructed = true;
    r.closed = false;
    r.closeEmitted = false;
    r.destroyed = false;
    r.errored = null;
    r.errorEmitted = false;
    r.reading = false;
    r.ended = r.readable === false;
    r.endEmitted = r.readable === false;
  }

  if (w) {
    w.constructed = true;
    w.destroyed = false;
    w.closed = false;
    w.closeEmitted = false;
    w.errored = null;
    w.errorEmitted = false;
    w.finalCalled = false;
    w.prefinished = false;
    w.ended = w.writable === false;
    w.ending = w.writable === false;
    w.finished = w.writable === false;
  }
}

function errorOrDestroy(stream, err, sync) {
  // We have tests that rely on errors being emitted
  // in the same tick, so changing this is semver major.
  // For now when you opt-in to autoDestroy we allow
  // the error to be emitted nextTick. In a future
  // semver major update we should change the default to this.

  const r = stream._readableState;
  const w = stream._writableState;

  if (
    (w && (w[kState] ? (w[kState] & kDestroyed) !== 0 : w.destroyed)) ||
    (r && (r[kState] ? (r[kState] & kDestroyed) !== 0 : r.destroyed))
  ) {
    return this;
  }

  if (
    (r && (r[kState] & kAutoDestroy) !== 0) ||
    (w && (w[kState] & kAutoDestroy) !== 0)
  ) {
    stream.destroy(err);
  } else if (err) {
    // Avoid V8 leak, https://github.com/nodejs/node/pull/34103#issuecomment-652002364
    err.stack; // eslint-disable-line no-unused-expressions

    if (w && (w[kState] & kErrored) === 0) {
      w.errored = err;
    }
    if (r && (r[kState] & kErrored) === 0) {
      r.errored = err;
    }
    if (sync) {
      process.nextTick(emitErrorNT, stream, err);
    } else {
      emitErrorNT(stream, err);
    }
  }
}

function construct(stream, cb) {
  if (typeof stream._construct !== 'function') {
    return;
  }

  const r = stream._readableState;
  const w = stream._writableState;

  if (r) {
    r[kState] &= ~kConstructed;
  }
  if (w) {
    w[kState] &= ~kConstructed;
  }

  stream.once(kConstruct, cb);

  if (stream.listenerCount(kConstruct) > 1) {
    // Duplex
    return;
  }

  process.nextTick(constructNT, stream);
}

function constructNT(stream) {
  let called = false;

  function onConstruct(err) {
    if (called) {
      errorOrDestroy(stream, err ?? new ERR_MULTIPLE_CALLBACK());
      return;
    }
    called = true;

    const r = stream._readableState;
    const w = stream._writableState;
    const s = w || r;

    if (r) {
      r[kState] |= kConstructed;
    }
    if (w) {
      w[kState] |= kConstructed;
    }

    if (s.destroyed) {
      stream.emit(kDestroy, err);
    } else if (err) {
      errorOrDestroy(stream, err, true);
    } else {
      stream.emit(kConstruct);
    }
  }

  try {
    stream._construct((err) => {
      process.nextTick(onConstruct, err);
    });
  } catch (err) {
    process.nextTick(onConstruct, err);
  }
}

function isRequest(stream) {
  return stream?.setHeader && typeof stream.abort === 'function';
}

function emitCloseLegacy(stream) {
  stream.emit('close');
}

function emitErrorCloseLegacy(stream, err) {
  stream.emit('error', err);
  process.nextTick(emitCloseLegacy, stream);
}

// Normalize destroy for legacy.
function destroyer(stream, err) {
  if (!stream || isDestroyed(stream)) {
    return;
  }

  if (!err && !isFinished(stream)) {
    err = new AbortError();
  }

  // TODO: Remove isRequest branches.
  if (isServerRequest(stream)) {
    stream.socket = null;
    stream.destroy(err);
  } else if (isRequest(stream)) {
    stream.abort();
  } else if (isRequest(stream.req)) {
    stream.req.abort();
  } else if (typeof stream.destroy === 'function') {
    stream.destroy(err);
  } else if (typeof stream.close === 'function') {
    // TODO: Don't lose err?
    stream.close();
  } else if (err) {
    process.nextTick(emitErrorCloseLegacy, stream, err);
  } else {
    process.nextTick(emitCloseLegacy, stream);
  }

  if (!stream.destroyed) {
    stream[kIsDestroyed] = true;
  }
}

module.exports = {
  construct,
  destroyer,
  destroy,
  undestroy,
  errorOrDestroy,
};
