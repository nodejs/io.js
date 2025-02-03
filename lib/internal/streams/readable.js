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
  ArrayPrototypeIndexOf,
  NumberIsInteger,
  NumberIsNaN,
  NumberParseInt,
  ObjectDefineProperties,
  ObjectKeys,
  ObjectSetPrototypeOf,
  Promise,
  SafeSet,
  Symbol,
  SymbolAsyncDispose,
  SymbolAsyncIterator,
  SymbolSpecies,
  TypedArrayPrototypeSet,
} = primordials;

module.exports = Readable;
Readable.ReadableState = ReadableState;

const EE = require('events');
const { Stream, prependListener } = require('internal/streams/legacy');
const { Buffer } = require('buffer');

const {
  addAbortSignal,
} = require('internal/streams/add-abort-signal');
const eos = require('internal/streams/end-of-stream');

let debug = require('internal/util/debuglog').debuglog('stream', (fn) => {
  debug = fn;
});
const destroyImpl = require('internal/streams/destroy');
const {
  getHighWaterMark,
  getDefaultHighWaterMark,
} = require('internal/streams/state');
const {
  kState,
  // bitfields
  kObjectMode,
  kErrorEmitted,
  kAutoDestroy,
  kEmitClose,
  kDestroyed,
  kClosed,
  kCloseEmitted,
  kErrored,
  kConstructed,
  kOnConstructed,
} = require('internal/streams/utils');

const {
  AbortError,
  aggregateTwoErrors,
  codes: {
    ERR_INVALID_ARG_TYPE,
    ERR_METHOD_NOT_IMPLEMENTED,
    ERR_OUT_OF_RANGE,
    ERR_STREAM_PUSH_AFTER_EOF,
    ERR_STREAM_UNSHIFT_AFTER_END_EVENT,
    ERR_UNKNOWN_ENCODING,
  },
} = require('internal/errors');
const { validateObject } = require('internal/validators');

const FastBuffer = Buffer[SymbolSpecies];

const { StringDecoder } = require('string_decoder');
const from = require('internal/streams/from');

ObjectSetPrototypeOf(Readable.prototype, Stream.prototype);
ObjectSetPrototypeOf(Readable, Stream);
const nop = () => {};

const { errorOrDestroy } = destroyImpl;

const kErroredValue = Symbol('kErroredValue');
const kDefaultEncodingValue = Symbol('kDefaultEncodingValue');
const kDecoderValue = Symbol('kDecoderValue');
const kEncodingValue = Symbol('kEncodingValue');

const kEnded = 1 << 9;
const kEndEmitted = 1 << 10;
const kReading = 1 << 11;
const kSync = 1 << 12;
const kNeedReadable = 1 << 13;
const kEmittedReadable = 1 << 14;
const kReadableListening = 1 << 15;
const kResumeScheduled = 1 << 16;
const kMultiAwaitDrain = 1 << 17;
const kReadingMore = 1 << 18;
const kDataEmitted = 1 << 19;
const kDefaultUTF8Encoding = 1 << 20;
const kDecoder = 1 << 21;
const kEncoding = 1 << 22;
const kHasFlowing = 1 << 23;
const kFlowing = 1 << 24;
const kHasPaused = 1 << 25;
const kPaused = 1 << 26;
const kDataListening = 1 << 27;

// TODO(benjamingr) it is likely slower to do it this way than with free functions
function makeBitMapDescriptor(bit) {
  return {
    enumerable: false,
    get() { return (this[kState] & bit) !== 0; },
    set(value) {
      if (value) this[kState] |= bit;
      else this[kState] &= ~bit;
    },
  };
}
ObjectDefineProperties(ReadableState.prototype, {
  objectMode: makeBitMapDescriptor(kObjectMode),
  ended: makeBitMapDescriptor(kEnded),
  endEmitted: makeBitMapDescriptor(kEndEmitted),
  reading: makeBitMapDescriptor(kReading),
  // Stream is still being constructed and cannot be
  // destroyed until construction finished or failed.
  // Async construction is opt in, therefore we start as
  // constructed.
  constructed: makeBitMapDescriptor(kConstructed),
  // A flag to be able to tell if the event 'readable'/'data' is emitted
  // immediately, or on a later tick.  We set this to true at first, because
  // any actions that shouldn't happen until "later" should generally also
  // not happen before the first read call.
  sync: makeBitMapDescriptor(kSync),
  // Whenever we return null, then we set a flag to say
  // that we're awaiting a 'readable' event emission.
  needReadable: makeBitMapDescriptor(kNeedReadable),
  emittedReadable: makeBitMapDescriptor(kEmittedReadable),
  readableListening: makeBitMapDescriptor(kReadableListening),
  resumeScheduled: makeBitMapDescriptor(kResumeScheduled),
  // True if the error was already emitted and should not be thrown again.
  errorEmitted: makeBitMapDescriptor(kErrorEmitted),
  emitClose: makeBitMapDescriptor(kEmitClose),
  autoDestroy: makeBitMapDescriptor(kAutoDestroy),
  // Has it been destroyed.
  destroyed: makeBitMapDescriptor(kDestroyed),
  // Indicates whether the stream has finished destroying.
  closed: makeBitMapDescriptor(kClosed),
  // True if close has been emitted or would have been emitted
  // depending on emitClose.
  closeEmitted: makeBitMapDescriptor(kCloseEmitted),
  multiAwaitDrain: makeBitMapDescriptor(kMultiAwaitDrain),
  // If true, a maybeReadMore has been scheduled.
  readingMore: makeBitMapDescriptor(kReadingMore),
  dataEmitted: makeBitMapDescriptor(kDataEmitted),

  // Indicates whether the stream has errored. When true no further
  // _read calls, 'data' or 'readable' events should occur. This is needed
  // since when autoDestroy is disabled we need a way to tell whether the
  // stream has failed.
  errored: {
    __proto__: null,
    enumerable: false,
    get() {
      return (this[kState] & kErrored) !== 0 ? this[kErroredValue] : null;
    },
    set(value) {
      if (value) {
        this[kErroredValue] = value;
        this[kState] |= kErrored;
      } else {
        this[kState] &= ~kErrored;
      }
    },
  },

  defaultEncoding: {
    __proto__: null,
    enumerable: false,
    get() { return (this[kState] & kDefaultUTF8Encoding) !== 0 ? 'utf8' : this[kDefaultEncodingValue]; },
    set(value) {
      if (value === 'utf8' || value === 'utf-8') {
        this[kState] |= kDefaultUTF8Encoding;
      } else {
        this[kState] &= ~kDefaultUTF8Encoding;
        this[kDefaultEncodingValue] = value;
      }
    },
  },

  decoder: {
    __proto__: null,
    enumerable: false,
    get() {
      return (this[kState] & kDecoder) !== 0 ? this[kDecoderValue] : null;
    },
    set(value) {
      if (value) {
        this[kDecoderValue] = value;
        this[kState] |= kDecoder;
      } else {
        this[kState] &= ~kDecoder;
      }
    },
  },

  encoding: {
    __proto__: null,
    enumerable: false,
    get() {
      return (this[kState] & kEncoding) !== 0 ? this[kEncodingValue] : null;
    },
    set(value) {
      if (value) {
        this[kEncodingValue] = value;
        this[kState] |= kEncoding;
      } else {
        this[kState] &= ~kEncoding;
      }
    },
  },

  flowing: {
    __proto__: null,
    enumerable: false,
    get() {
      return (this[kState] & kHasFlowing) !== 0 ? (this[kState] & kFlowing) !== 0 : null;
    },
    set(value) {
      if (value == null) {
        this[kState] &= ~(kHasFlowing | kFlowing);
      } else if (value) {
        this[kState] |= (kHasFlowing | kFlowing);
      } else {
        this[kState] |= kHasFlowing;
        this[kState] &= ~kFlowing;
      }
    },
  },
});


function ReadableState(options, stream, isDuplex) {
  // Bit map field to store ReadableState more efficiently with 1 bit per field
  // instead of a V8 slot per field.
  this[kState] = kEmitClose | kAutoDestroy | kConstructed | kSync;

  // Object stream flag. Used to make read(n) ignore n and to
  // make all the buffer merging and length checks go away.
  if (options?.objectMode)
    this[kState] |= kObjectMode;

  if (isDuplex && options?.readableObjectMode)
    this[kState] |= kObjectMode;

  // The point at which it stops calling _read() to fill the buffer
  // Note: 0 is a valid value, means "don't call _read preemptively ever"
  this.highWaterMark = options ?
    getHighWaterMark(this, options, 'readableHighWaterMark', isDuplex) :
    getDefaultHighWaterMark(false);

  this.buffer = [];
  this.bufferIndex = 0;
  this.length = 0;
  this.pipes = [];

  // Should close be emitted on destroy. Defaults to true.
  if (options && options.emitClose === false) this[kState] &= ~kEmitClose;

  // Should .destroy() be called after 'end' (and potentially 'finish').
  if (options && options.autoDestroy === false) this[kState] &= ~kAutoDestroy;

  // Crypto is kind of old and crusty.  Historically, its default string
  // encoding is 'binary' so we have to make this configurable.
  // Everything else in the universe uses 'utf8', though.
  const defaultEncoding = options?.defaultEncoding;
  if (defaultEncoding == null || defaultEncoding === 'utf8' || defaultEncoding === 'utf-8') {
    this[kState] |= kDefaultUTF8Encoding;
  } else if (Buffer.isEncoding(defaultEncoding)) {
    this.defaultEncoding = defaultEncoding;
  } else {
    throw new ERR_UNKNOWN_ENCODING(defaultEncoding);
  }

  // Ref the piped dest which we need a drain event on it
  // type: null | Writable | Set<Writable>.
  this.awaitDrainWriters = null;

  if (options?.encoding) {
    this.decoder = new StringDecoder(options.encoding);
    this.encoding = options.encoding;
  }
}

ReadableState.prototype[kOnConstructed] = function onConstructed(stream) {
  if ((this[kState] & kNeedReadable) !== 0) {
    maybeReadMore(stream, this);
  }
};

function Readable(options) {
  if (!(this instanceof Readable))
    return new Readable(options);

  this._events ??= {
    close: undefined,
    error: undefined,
    data: undefined,
    end: undefined,
    readable: undefined,
    // Skip uncommon events...
    // pause: undefined,
    // resume: undefined,
    // pipe: undefined,
    // unpipe: undefined,
    // [destroyImpl.kConstruct]: undefined,
    // [destroyImpl.kDestroy]: undefined,
  };

  this._readableState = new ReadableState(options, this, false);

  if (options) {
    if (typeof options.read === 'function')
      this._read = options.read;

    if (typeof options.destroy === 'function')
      this._destroy = options.destroy;

    if (typeof options.construct === 'function')
      this._construct = options.construct;

    if (options.signal)
      addAbortSignal(options.signal, this);
  }

  Stream.call(this, options);

  if (this._construct != null) {
    destroyImpl.construct(this, () => {
      this._readableState[kOnConstructed](this);
    });
  }
}

Readable.prototype.destroy = destroyImpl.destroy;
Readable.prototype._undestroy = destroyImpl.undestroy;
Readable.prototype._destroy = function(err, cb) {
  cb(err);
};

Readable.prototype[EE.captureRejectionSymbol] = function(err) {
  this.destroy(err);
};

Readable.prototype[SymbolAsyncDispose] = function() {
  let error;
  if (!this.destroyed) {
    error = this.readableEnded ? null : new AbortError();
    this.destroy(error);
  }
  return new Promise((resolve, reject) => eos(this, (err) => (err && err !== error ? reject(err) : resolve(null))));
};

// Manually shove something into the read() buffer.
// This returns true if the highWaterMark has not been hit yet,
// similar to how Writable.write() returns true if you should
// write() some more.
Readable.prototype.push = function(chunk, encoding) {
  debug('push', chunk);

  const state = this._readableState;
  return (state[kState] & kObjectMode) === 0 ?
    readableAddChunkPushByteMode(this, state, chunk, encoding) :
    readableAddChunkPushObjectMode(this, state, chunk, encoding);
};

// Unshift should *always* be something directly out of read().
Readable.prototype.unshift = function(chunk, encoding) {
  debug('unshift', chunk);
  const state = this._readableState;
  return (state[kState] & kObjectMode) === 0 ?
    readableAddChunkUnshiftByteMode(this, state, chunk, encoding) :
    readableAddChunkUnshiftObjectMode(this, state, chunk);
};


function readableAddChunkUnshiftByteMode(stream, state, chunk, encoding) {
  if (chunk === null) {
    state[kState] &= ~kReading;
    onEofChunk(stream, state);

    return false;
  }

  if (typeof chunk === 'string') {
    encoding ||= state.defaultEncoding;
    if (state.encoding !== encoding) {
      if (state.encoding) {
        // When unshifting, if state.encoding is set, we have to save
        // the string in the BufferList with the state encoding.
        chunk = Buffer.from(chunk, encoding).toString(state.encoding);
      } else {
        chunk = Buffer.from(chunk, encoding);
      }
    }
  } else if (Stream._isArrayBufferView(chunk)) {
    chunk = Stream._uint8ArrayToBuffer(chunk);
  } else if (chunk !== undefined && !(chunk instanceof Buffer)) {
    errorOrDestroy(stream, new ERR_INVALID_ARG_TYPE(
      'chunk', ['string', 'Buffer', 'TypedArray', 'DataView'], chunk));
    return false;
  }


  if (!(chunk && chunk.length > 0)) {
    return canPushMore(state);
  }

  return readableAddChunkUnshiftValue(stream, state, chunk);
}

function readableAddChunkUnshiftObjectMode(stream, state, chunk) {
  if (chunk === null) {
    state[kState] &= ~kReading;
    onEofChunk(stream, state);

    return false;
  }

  return readableAddChunkUnshiftValue(stream, state, chunk);
}

function readableAddChunkUnshiftValue(stream, state, chunk) {
  if ((state[kState] & kEndEmitted) !== 0)
    errorOrDestroy(stream, new ERR_STREAM_UNSHIFT_AFTER_END_EVENT());
  else if ((state[kState] & (kDestroyed | kErrored)) !== 0)
    return false;
  else
    addChunk(stream, state, chunk, true);

  return canPushMore(state);
}

function readableAddChunkPushByteMode(stream, state, chunk, encoding) {
  if (chunk === null) {
    state[kState] &= ~kReading;
    onEofChunk(stream, state);
    return false;
  }

  if (typeof chunk === 'string') {
    encoding ||= state.defaultEncoding;
    if (state.encoding !== encoding) {
      chunk = Buffer.from(chunk, encoding);
      encoding = '';
    }
  } else if (chunk instanceof Buffer) {
    encoding = '';
  } else if (Stream._isArrayBufferView(chunk)) {
    chunk = Stream._uint8ArrayToBuffer(chunk);
    encoding = '';
  } else if (chunk !== undefined) {
    errorOrDestroy(stream, new ERR_INVALID_ARG_TYPE(
      'chunk', ['string', 'Buffer', 'TypedArray', 'DataView'], chunk));
    return false;
  }

  if (!chunk || chunk.length <= 0) {
    state[kState] &= ~kReading;
    maybeReadMore(stream, state);

    return canPushMore(state);
  }

  if ((state[kState] & kEnded) !== 0) {
    errorOrDestroy(stream, new ERR_STREAM_PUSH_AFTER_EOF());
    return false;
  }

  if ((state[kState] & (kDestroyed | kErrored)) !== 0) {
    return false;
  }

  state[kState] &= ~kReading;
  if ((state[kState] & kDecoder) !== 0 && !encoding) {
    chunk = state[kDecoderValue].write(chunk);
    if (chunk.length === 0) {
      maybeReadMore(stream, state);
      return canPushMore(state);
    }
  }

  addChunk(stream, state, chunk, false);
  return canPushMore(state);
}

function readableAddChunkPushObjectMode(stream, state, chunk, encoding) {
  if (chunk === null) {
    state[kState] &= ~kReading;
    onEofChunk(stream, state);
    return false;
  }

  if ((state[kState] & kEnded) !== 0) {
    errorOrDestroy(stream, new ERR_STREAM_PUSH_AFTER_EOF());
    return false;
  }

  if ((state[kState] & (kDestroyed | kErrored)) !== 0) {
    return false;
  }

  state[kState] &= ~kReading;

  if ((state[kState] & kDecoder) !== 0 && !encoding) {
    chunk = state[kDecoderValue].write(chunk);
  }

  addChunk(stream, state, chunk, false);
  return canPushMore(state);
}

function canPushMore(state) {
  // We can push more data if we are below the highWaterMark.
  // Also, if we have no data yet, we can stand some more bytes.
  // This is to work around cases where hwm=0, such as the repl.
  return (state[kState] & kEnded) === 0 &&
    (state.length < state.highWaterMark || state.length === 0);
}

function addChunk(stream, state, chunk, addToFront) {
  if ((state[kState] & (kFlowing | kSync | kDataListening)) === (kFlowing | kDataListening) && state.length === 0) {
    // Use the guard to avoid creating `Set()` repeatedly
    // when we have multiple pipes.
    if ((state[kState] & kMultiAwaitDrain) !== 0) {
      state.awaitDrainWriters.clear();
    } else {
      state.awaitDrainWriters = null;
    }

    state[kState] |= kDataEmitted;
    stream.emit('data', chunk);
  } else {
    // Update the buffer info.
    state.length += (state[kState] & kObjectMode) !== 0 ? 1 : chunk.length;
    if (addToFront) {
      if (state.bufferIndex > 0) {
        state.buffer[--state.bufferIndex] = chunk;
      } else {
        state.buffer.unshift(chunk); // Slow path
      }
    } else {
      state.buffer.push(chunk);
    }

    if ((state[kState] & kNeedReadable) !== 0)
      emitReadable(stream);
  }
  maybeReadMore(stream, state);
}

Readable.prototype.isPaused = function() {
  const state = this._readableState;
  return (state[kState] & kPaused) !== 0 || (state[kState] & (kHasFlowing | kFlowing)) === kHasFlowing;
};

// Backwards compatibility.
Readable.prototype.setEncoding = function(enc) {
  const state = this._readableState;

  const decoder = new StringDecoder(enc);
  state.decoder = decoder;
  // If setEncoding(null), decoder.encoding equals utf8.
  state.encoding = state.decoder.encoding;

  // Iterate over current buffer to convert already stored Buffers:
  let content = '';
  for (const data of state.buffer.slice(state.bufferIndex)) {
    content += decoder.write(data);
  }
  state.buffer.length = 0;
  state.bufferIndex = 0;

  if (content !== '')
    state.buffer.push(content);
  state.length = content.length;
  return this;
};

// Don't raise the hwm > 1GB.
const MAX_HWM = 0x40000000;
function computeNewHighWaterMark(n) {
  if (n > MAX_HWM) {
    throw new ERR_OUT_OF_RANGE('size', '<= 1GiB', n);
  } else {
    // Get the next highest power of 2 to prevent increasing hwm excessively in
    // tiny amounts.
    n--;
    n |= n >>> 1;
    n |= n >>> 2;
    n |= n >>> 4;
    n |= n >>> 8;
    n |= n >>> 16;
    n++;
  }
  return n;
}

// This function is designed to be inlinable, so please take care when making
// changes to the function body.
function howMuchToRead(n, state) {
  if (n <= 0 || (state.length === 0 && (state[kState] & kEnded) !== 0))
    return 0;
  if ((state[kState] & kObjectMode) !== 0)
    return 1;
  if (NumberIsNaN(n)) {
    // Only flow one buffer at a time.
    if ((state[kState] & kFlowing) !== 0 && state.length)
      return state.buffer[state.bufferIndex].length;
    return state.length;
  }
  if (n <= state.length)
    return n;
  return (state[kState] & kEnded) !== 0 ? state.length : 0;
}

// You can override either this method, or the async _read(n) below.
Readable.prototype.read = function(n) {
  debug('read', n);
  // Same as parseInt(undefined, 10), however V8 7.3 performance regressed
  // in this scenario, so we are doing it manually.
  if (n === undefined) {
    n = NaN;
  } else if (!NumberIsInteger(n)) {
    n = NumberParseInt(n, 10);
  }
  const state = this._readableState;
  const nOrig = n;

  // If we're asking for more than the current hwm, then raise the hwm.
  if (n > state.highWaterMark)
    state.highWaterMark = computeNewHighWaterMark(n);

  if (n !== 0)
    state[kState] &= ~kEmittedReadable;

  // If we're doing read(0) to trigger a readable event, but we
  // already have a bunch of data in the buffer, then just trigger
  // the 'readable' event and move on.
  if (n === 0 &&
      (state[kState] & kNeedReadable) !== 0 &&
      ((state.highWaterMark !== 0 ?
        state.length >= state.highWaterMark :
        state.length > 0) ||
       (state[kState] & kEnded) !== 0)) {
    debug('read: emitReadable');
    if (state.length === 0 && (state[kState] & kEnded) !== 0)
      endReadable(this);
    else
      emitReadable(this);
    return null;
  }

  n = howMuchToRead(n, state);

  // If we've ended, and we're now clear, then finish it up.
  if (n === 0 && (state[kState] & kEnded) !== 0) {
    if (state.length === 0)
      endReadable(this);
    return null;
  }

  // All the actual chunk generation logic needs to be
  // *below* the call to _read.  The reason is that in certain
  // synthetic stream cases, such as passthrough streams, _read
  // may be a completely synchronous operation which may change
  // the state of the read buffer, providing enough data when
  // before there was *not* enough.
  //
  // So, the steps are:
  // 1. Figure out what the state of things will be after we do
  // a read from the buffer.
  //
  // 2. If that resulting state will trigger a _read, then call _read.
  // Note that this may be asynchronous, or synchronous.  Yes, it is
  // deeply ugly to write APIs this way, but that still doesn't mean
  // that the Readable class should behave improperly, as streams are
  // designed to be sync/async agnostic.
  // Take note if the _read call is sync or async (ie, if the read call
  // has returned yet), so that we know whether or not it's safe to emit
  // 'readable' etc.
  //
  // 3. Actually pull the requested chunks out of the buffer and return.

  // if we need a readable event, then we need to do some reading.
  let doRead = (state[kState] & kNeedReadable) !== 0;
  debug('need readable', doRead);

  // If we currently have less than the highWaterMark, then also read some.
  if (state.length === 0 || state.length - n < state.highWaterMark) {
    doRead = true;
    debug('length less than watermark', doRead);
  }

  // However, if we've ended, then there's no point, if we're already
  // reading, then it's unnecessary, if we're constructing we have to wait,
  // and if we're destroyed or errored, then it's not allowed,
  if ((state[kState] & (kReading | kEnded | kDestroyed | kErrored | kConstructed)) !== kConstructed) {
    doRead = false;
    debug('reading, ended or constructing', doRead);
  } else if (doRead) {
    debug('do read');
    state[kState] |= kReading | kSync;
    // If the length is currently zero, then we *need* a readable event.
    if (state.length === 0)
      state[kState] |= kNeedReadable;

    // Call internal read method
    try {
      this._read(state.highWaterMark);
    } catch (err) {
      errorOrDestroy(this, err);
    }
    state[kState] &= ~kSync;

    // If _read pushed data synchronously, then `reading` will be false,
    // and we need to re-evaluate how much data we can return to the user.
    if ((state[kState] & kReading) === 0)
      n = howMuchToRead(nOrig, state);
  }

  let ret;
  if (n > 0)
    ret = fromList(n, state);
  else
    ret = null;

  if (ret === null) {
    state[kState] |= state.length <= state.highWaterMark ? kNeedReadable : 0;
    n = 0;
  } else {
    state.length -= n;
    if ((state[kState] & kMultiAwaitDrain) !== 0) {
      state.awaitDrainWriters.clear();
    } else {
      state.awaitDrainWriters = null;
    }
  }

  if (state.length === 0) {
    // If we have nothing in the buffer, then we want to know
    // as soon as we *do* get something into the buffer.
    if ((state[kState] & kEnded) === 0)
      state[kState] |= kNeedReadable;

    // If we tried to read() past the EOF, then emit end on the next tick.
    if (nOrig !== n && (state[kState] & kEnded) !== 0)
      endReadable(this);
  }

  if (ret !== null && (state[kState] & (kErrorEmitted | kCloseEmitted)) === 0) {
    state[kState] |= kDataEmitted;
    this.emit('data', ret);
  }

  return ret;
};

function onEofChunk(stream, state) {
  debug('onEofChunk');
  if ((state[kState] & kEnded) !== 0) return;
  const decoder = (state[kState] & kDecoder) !== 0 ? state[kDecoderValue] : null;
  if (decoder) {
    const chunk = decoder.end();
    if (chunk?.length) {
      state.buffer.push(chunk);
      state.length += (state[kState] & kObjectMode) !== 0 ? 1 : chunk.length;
    }
  }
  state[kState] |= kEnded;

  if ((state[kState] & kSync) !== 0) {
    // If we are sync, wait until next tick to emit the data.
    // Otherwise we risk emitting data in the flow()
    // the readable code triggers during a read() call.
    emitReadable(stream);
  } else {
    // Emit 'readable' now to make sure it gets picked up.
    state[kState] &= ~kNeedReadable;
    state[kState] |= kEmittedReadable;
    // We have to emit readable now that we are EOF. Modules
    // in the ecosystem (e.g. dicer) rely on this event being sync.
    emitReadable_(stream);
  }
}

// Don't emit readable right away in sync mode, because this can trigger
// another read() call => stack overflow.  This way, it might trigger
// a nextTick recursion warning, but that's not so bad.
function emitReadable(stream) {
  const state = stream._readableState;
  debug('emitReadable');
  state[kState] &= ~kNeedReadable;
  if ((state[kState] & kEmittedReadable) === 0) {
    debug('emitReadable', (state[kState] & kFlowing) !== 0);
    state[kState] |= kEmittedReadable;
    process.nextTick(emitReadable_, stream);
  }
}

function emitReadable_(stream) {
  const state = stream._readableState;
  debug('emitReadable_');
  if ((state[kState] & (kDestroyed | kErrored)) === 0 && (state.length || (state[kState] & kEnded) !== 0)) {
    stream.emit('readable');
    state[kState] &= ~kEmittedReadable;
  }

  // The stream needs another readable event if:
  // 1. It is not flowing, as the flow mechanism will take
  //    care of it.
  // 2. It is not ended.
  // 3. It is below the highWaterMark, so we can schedule
  //    another readable later.
  state[kState] |=
    (state[kState] & (kFlowing | kEnded)) === 0 &&
    state.length <= state.highWaterMark ? kNeedReadable : 0;
  flow(stream);
}


// At this point, the user has presumably seen the 'readable' event,
// and called read() to consume some data.  that may have triggered
// in turn another _read(n) call, in which case reading = true if
// it's in progress.
// However, if we're not ended, or reading, and the length < hwm,
// then go ahead and try to read some more preemptively.
function maybeReadMore(stream, state) {
  if ((state[kState] & (kReadingMore | kConstructed)) === kConstructed) {
    state[kState] |= kReadingMore;
    process.nextTick(maybeReadMore_, stream, state);
  }
}

function maybeReadMore_(stream, state) {
  // Attempt to read more data if we should.
  //
  // The conditions for reading more data are (one of):
  // - Not enough data buffered (state.length < state.highWaterMark). The loop
  //   is responsible for filling the buffer with enough data if such data
  //   is available. If highWaterMark is 0 and we are not in the flowing mode
  //   we should _not_ attempt to buffer any extra data. We'll get more data
  //   when the stream consumer calls read() instead.
  // - No data in the buffer, and the stream is in flowing mode. In this mode
  //   the loop below is responsible for ensuring read() is called. Failing to
  //   call read here would abort the flow and there's no other mechanism for
  //   continuing the flow if the stream consumer has just subscribed to the
  //   'data' event.
  //
  // In addition to the above conditions to keep reading data, the following
  // conditions prevent the data from being read:
  // - The stream has ended (state.ended).
  // - There is already a pending 'read' operation (state.reading). This is a
  //   case where the stream has called the implementation defined _read()
  //   method, but they are processing the call asynchronously and have _not_
  //   called push() with new data. In this case we skip performing more
  //   read()s. The execution ends in this method again after the _read() ends
  //   up calling push() with more data.
  while ((state[kState] & (kReading | kEnded)) === 0 &&
         (state.length < state.highWaterMark ||
          ((state[kState] & kFlowing) !== 0 && state.length === 0))) {
    const len = state.length;
    debug('maybeReadMore read 0');
    stream.read(0);
    if (len === state.length)
      // Didn't get any data, stop spinning.
      break;
  }
  state[kState] &= ~kReadingMore;
}

// Abstract method.  to be overridden in specific implementation classes.
// call cb(er, data) where data is <= n in length.
// for virtual (non-string, non-buffer) streams, "length" is somewhat
// arbitrary, and perhaps not very meaningful.
Readable.prototype._read = function(n) {
  throw new ERR_METHOD_NOT_IMPLEMENTED('_read()');
};

Readable.prototype.pipe = function(dest, pipeOpts) {
  const src = this;
  const state = this._readableState;

  if (state.pipes.length === 1) {
    if ((state[kState] & kMultiAwaitDrain) === 0) {
      state[kState] |= kMultiAwaitDrain;
      state.awaitDrainWriters = new SafeSet(
        state.awaitDrainWriters ? [state.awaitDrainWriters] : [],
      );
    }
  }

  state.pipes.push(dest);
  debug('pipe count=%d opts=%j', state.pipes.length, pipeOpts);

  const doEnd = (!pipeOpts || pipeOpts.end !== false) &&
              dest !== process.stdout &&
              dest !== process.stderr;

  const endFn = doEnd ? onend : unpipe;
  if ((state[kState] & kEndEmitted) !== 0)
    process.nextTick(endFn);
  else
    src.once('end', endFn);

  dest.on('unpipe', onunpipe);
  function onunpipe(readable, unpipeInfo) {
    debug('onunpipe');
    if (readable === src) {
      if (unpipeInfo && unpipeInfo.hasUnpiped === false) {
        unpipeInfo.hasUnpiped = true;
        cleanup();
      }
    }
  }

  function onend() {
    debug('onend');
    dest.end();
  }

  let ondrain;

  let cleanedUp = false;
  function cleanup() {
    debug('cleanup');
    // Cleanup event handlers once the pipe is broken.
    dest.removeListener('close', onclose);
    dest.removeListener('finish', onfinish);
    if (ondrain) {
      dest.removeListener('drain', ondrain);
    }
    dest.removeListener('error', onerror);
    dest.removeListener('unpipe', onunpipe);
    src.removeListener('end', onend);
    src.removeListener('end', unpipe);
    src.removeListener('data', ondata);

    cleanedUp = true;

    // If the reader is waiting for a drain event from this
    // specific writer, then it would cause it to never start
    // flowing again.
    // So, if this is awaiting a drain, then we just call it now.
    // If we don't know, then assume that we are waiting for one.
    if (ondrain && state.awaitDrainWriters &&
        (!dest._writableState || dest._writableState.needDrain))
      ondrain();
  }

  function pause() {
    // If the user unpiped during `dest.write()`, it is possible
    // to get stuck in a permanently paused state if that write
    // also returned false.
    // => Check whether `dest` is still a piping destination.
    if (!cleanedUp) {
      if (state.pipes.length === 1 && state.pipes[0] === dest) {
        debug('false write response, pause', 0);
        state.awaitDrainWriters = dest;
        state[kState] &= ~kMultiAwaitDrain;
      } else if (state.pipes.length > 1 && state.pipes.includes(dest)) {
        debug('false write response, pause', state.awaitDrainWriters.size);
        state.awaitDrainWriters.add(dest);
      }
      src.pause();
    }
    if (!ondrain) {
      // When the dest drains, it reduces the awaitDrain counter
      // on the source.  This would be more elegant with a .once()
      // handler in flow(), but adding and removing repeatedly is
      // too slow.
      ondrain = pipeOnDrain(src, dest);
      dest.on('drain', ondrain);
    }
  }

  src.on('data', ondata);
  function ondata(chunk) {
    debug('ondata');
    try {
      const ret = dest.write(chunk);
      debug('dest.write', ret);

      if (ret === false) {
        pause();
      }
    } catch (error) {
      dest.destroy(error);
    }
  }

  // If the dest has an error, then stop piping into it.
  // However, don't suppress the throwing behavior for this.
  function onerror(er) {
    debug('onerror', er);
    unpipe();
    dest.removeListener('error', onerror);
    if (dest.listenerCount('error') === 0) {
      const s = dest._writableState || dest._readableState;
      if (s && !s.errorEmitted) {
        // User incorrectly emitted 'error' directly on the stream.
        errorOrDestroy(dest, er);
      } else {
        dest.emit('error', er);
      }
    }
  }

  // Make sure our error handler is attached before userland ones.
  prependListener(dest, 'error', onerror);

  // Both close and finish should trigger unpipe, but only once.
  function onclose() {
    dest.removeListener('finish', onfinish);
    unpipe();
  }
  dest.once('close', onclose);
  function onfinish() {
    debug('onfinish');
    dest.removeListener('close', onclose);
    unpipe();
  }
  dest.once('finish', onfinish);

  function unpipe() {
    debug('unpipe');
    src.unpipe(dest);
  }

  // Tell the dest that it's being piped to.
  dest.emit('pipe', src);

  // Start the flow if it hasn't been started already.

  if (dest.writableNeedDrain === true) {
    pause();
  } else if ((state[kState] & kFlowing) === 0) {
    debug('pipe resume');
    src.resume();
  }

  return dest;
};

function pipeOnDrain(src, dest) {
  return function pipeOnDrainFunctionResult() {
    const state = src._readableState;

    // `ondrain` will call directly,
    // `this` maybe not a reference to dest,
    // so we use the real dest here.
    if (state.awaitDrainWriters === dest) {
      debug('pipeOnDrain', 1);
      state.awaitDrainWriters = null;
    } else if ((state[kState] & kMultiAwaitDrain) !== 0) {
      debug('pipeOnDrain', state.awaitDrainWriters.size);
      state.awaitDrainWriters.delete(dest);
    }

    if ((!state.awaitDrainWriters || state.awaitDrainWriters.size === 0) &&
      (state[kState] & kDataListening) !== 0) {
      src.resume();
    }
  };
}


Readable.prototype.unpipe = function(dest) {
  const state = this._readableState;
  const unpipeInfo = { hasUnpiped: false };

  // If we're not piping anywhere, then do nothing.
  if (state.pipes.length === 0)
    return this;

  if (!dest) {
    // remove all.
    const dests = state.pipes;
    state.pipes = [];
    this.pause();

    for (let i = 0; i < dests.length; i++)
      dests[i].emit('unpipe', this, { hasUnpiped: false });
    return this;
  }

  // Try to find the right one.
  const index = ArrayPrototypeIndexOf(state.pipes, dest);
  if (index === -1)
    return this;

  state.pipes.splice(index, 1);
  if (state.pipes.length === 0)
    this.pause();

  dest.emit('unpipe', this, unpipeInfo);

  return this;
};

// Set up data events if they are asked for
// Ensure readable listeners eventually get something.
Readable.prototype.on = function(ev, fn) {
  const res = Stream.prototype.on.call(this, ev, fn);
  const state = this._readableState;

  if (ev === 'data') {
    state[kState] |= kDataListening;

    // Update readableListening so that resume() may be a no-op
    // a few lines down. This is needed to support once('readable').
    state[kState] |= this.listenerCount('readable') > 0 ? kReadableListening : 0;

    // Try start flowing on next tick if stream isn't explicitly paused.
    if ((state[kState] & (kHasFlowing | kFlowing)) !== kHasFlowing) {
      this.resume();
    }
  } else if (ev === 'readable') {
    if ((state[kState] & (kEndEmitted | kReadableListening)) === 0) {
      state[kState] |= kReadableListening | kNeedReadable | kHasFlowing;
      state[kState] &= ~(kFlowing | kEmittedReadable);
      debug('on readable');
      if (state.length) {
        emitReadable(this);
      } else if ((state[kState] & kReading) === 0) {
        process.nextTick(nReadingNextTick, this);
      }
    }
  }

  return res;
};
Readable.prototype.addListener = Readable.prototype.on;

Readable.prototype.removeListener = function(ev, fn) {
  const state = this._readableState;

  const res = Stream.prototype.removeListener.call(this,
                                                   ev, fn);

  if (ev === 'readable') {
    // We need to check if there is someone still listening to
    // readable and reset the state. However this needs to happen
    // after readable has been emitted but before I/O (nextTick) to
    // support once('readable', fn) cycles. This means that calling
    // resume within the same tick will have no
    // effect.
    process.nextTick(updateReadableListening, this);
  } else if (ev === 'data' && this.listenerCount('data') === 0) {
    state[kState] &= ~kDataListening;
  }

  return res;
};
Readable.prototype.off = Readable.prototype.removeListener;

Readable.prototype.removeAllListeners = function(ev) {
  const res = Stream.prototype.removeAllListeners.apply(this,
                                                        arguments);

  if (ev === 'readable' || ev === undefined) {
    // We need to check if there is someone still listening to
    // readable and reset the state. However this needs to happen
    // after readable has been emitted but before I/O (nextTick) to
    // support once('readable', fn) cycles. This means that calling
    // resume within the same tick will have no
    // effect.
    process.nextTick(updateReadableListening, this);
  }

  return res;
};

function updateReadableListening(self) {
  const state = self._readableState;

  if (self.listenerCount('readable') > 0) {
    state[kState] |= kReadableListening;
  } else {
    state[kState] &= ~kReadableListening;
  }

  if ((state[kState] & (kHasPaused | kPaused | kResumeScheduled)) === (kHasPaused | kResumeScheduled)) {
    // Flowing needs to be set to true now, otherwise
    // the upcoming resume will not flow.
    state[kState] |= kHasFlowing | kFlowing;

    // Crude way to check if we should resume.
  } else if ((state[kState] & kDataListening) !== 0) {
    self.resume();
  } else if ((state[kState] & kReadableListening) === 0) {
    state[kState] &= ~(kHasFlowing | kFlowing);
  }
}

function nReadingNextTick(self) {
  debug('readable nexttick read 0');
  self.read(0);
}

// pause() and resume() are remnants of the legacy readable stream API
// If the user uses them, then switch into old mode.
Readable.prototype.resume = function() {
  const state = this._readableState;
  if ((state[kState] & kFlowing) === 0) {
    debug('resume');
    // We flow only if there is no one listening
    // for readable, but we still have to call
    // resume().
    state[kState] |= kHasFlowing;
    if ((state[kState] & kReadableListening) === 0) {
      state[kState] |= kFlowing;
    } else {
      state[kState] &= ~kFlowing;
    }
    resume(this, state);
  }
  state[kState] |= kHasPaused;
  state[kState] &= ~kPaused;
  return this;
};

function resume(stream, state) {
  if ((state[kState] & kResumeScheduled) === 0) {
    state[kState] |= kResumeScheduled;
    process.nextTick(resume_, stream, state);
  }
}

function resume_(stream, state) {
  debug('resume', (state[kState] & kReading) !== 0);
  if ((state[kState] & kReading) === 0) {
    stream.read(0);
  }

  state[kState] &= ~kResumeScheduled;
  stream.emit('resume');
  flow(stream);
  if ((state[kState] & (kFlowing | kReading)) === kFlowing)
    stream.read(0);
}

Readable.prototype.pause = function() {
  const state = this._readableState;
  debug('call pause');
  if ((state[kState] & (kHasFlowing | kFlowing)) !== kHasFlowing) {
    debug('pause');
    state[kState] |= kHasFlowing;
    state[kState] &= ~kFlowing;
    this.emit('pause');
  }
  state[kState] |= kHasPaused | kPaused;
  return this;
};

function flow(stream) {
  const state = stream._readableState;
  debug('flow');
  while ((state[kState] & kFlowing) !== 0 && stream.read() !== null);
}

// Wrap an old-style stream as the async data source.
// This is *not* part of the readable stream interface.
// It is an ugly unfortunate mess of history.
Readable.prototype.wrap = function(stream) {
  let paused = false;

  // TODO (ronag): Should this.destroy(err) emit
  // 'error' on the wrapped stream? Would require
  // a static factory method, e.g. Readable.wrap(stream).

  stream.on('data', (chunk) => {
    if (!this.push(chunk) && stream.pause) {
      paused = true;
      stream.pause();
    }
  });

  stream.on('end', () => {
    this.push(null);
  });

  stream.on('error', (err) => {
    errorOrDestroy(this, err);
  });

  stream.on('close', () => {
    this.destroy();
  });

  stream.on('destroy', () => {
    this.destroy();
  });

  this._read = () => {
    if (paused && stream.resume) {
      paused = false;
      stream.resume();
    }
  };

  // Proxy all the other methods. Important when wrapping filters and duplexes.
  const streamKeys = ObjectKeys(stream);
  for (let j = 1; j < streamKeys.length; j++) {
    const i = streamKeys[j];
    if (this[i] === undefined && typeof stream[i] === 'function') {
      this[i] = stream[i].bind(stream);
    }
  }

  return this;
};

Readable.prototype[SymbolAsyncIterator] = function() {
  return streamToAsyncIterator(this);
};

Readable.prototype.iterator = function(options) {
  if (options !== undefined) {
    validateObject(options, 'options');
  }
  return streamToAsyncIterator(this, options);
};

function streamToAsyncIterator(stream, options) {
  if (typeof stream.read !== 'function') {
    stream = Readable.wrap(stream, { objectMode: true });
  }

  const iter = createAsyncIterator(stream, options);
  iter.stream = stream;
  return iter;
}

async function* createAsyncIterator(stream, options) {
  let callback = nop;

  function next(resolve) {
    if (this === stream) {
      callback();
      callback = nop;
    } else {
      callback = resolve;
    }
  }

  stream.on('readable', next);

  let error;
  const cleanup = eos(stream, { writable: false }, (err) => {
    error = err ? aggregateTwoErrors(error, err) : null;
    callback();
    callback = nop;
  });

  try {
    while (true) {
      const chunk = stream.destroyed ? null : stream.read();
      if (chunk !== null) {
        yield chunk;
      } else if (error) {
        throw error;
      } else if (error === null) {
        return;
      } else {
        await new Promise(next);
      }
    }
  } catch (err) {
    error = aggregateTwoErrors(error, err);
    throw error;
  } finally {
    if (
      (error || options?.destroyOnReturn !== false) &&
      (error === undefined || stream._readableState.autoDestroy)
    ) {
      destroyImpl.destroyer(stream, null);
    } else {
      stream.off('readable', next);
      cleanup();
    }
  }
}

// Making it explicit these properties are not enumerable
// because otherwise some prototype manipulation in
// userland will fail.
ObjectDefineProperties(Readable.prototype, {
  readable: {
    __proto__: null,
    get() {
      const r = this._readableState;
      // r.readable === false means that this is part of a Duplex stream
      // where the readable side was disabled upon construction.
      // Compat. The user might manually disable readable side through
      // deprecated setter.
      return !!r && r.readable !== false && !r.destroyed && !r.errorEmitted &&
        !r.endEmitted;
    },
    set(val) {
      // Backwards compat.
      if (this._readableState) {
        this._readableState.readable = !!val;
      }
    },
  },

  readableDidRead: {
    __proto__: null,
    enumerable: false,
    get: function() {
      return this._readableState.dataEmitted;
    },
  },

  readableAborted: {
    __proto__: null,
    enumerable: false,
    get: function() {
      return !!(
        this._readableState.readable !== false &&
        (this._readableState.destroyed || this._readableState.errored) &&
        !this._readableState.endEmitted
      );
    },
  },

  readableHighWaterMark: {
    __proto__: null,
    enumerable: false,
    get: function() {
      return this._readableState.highWaterMark;
    },
  },

  readableBuffer: {
    __proto__: null,
    enumerable: false,
    get: function() {
      return this._readableState?.buffer;
    },
  },

  readableFlowing: {
    __proto__: null,
    enumerable: false,
    get: function() {
      return this._readableState.flowing;
    },
    set: function(state) {
      if (this._readableState) {
        this._readableState.flowing = state;
      }
    },
  },

  readableLength: {
    __proto__: null,
    enumerable: false,
    get() {
      return this._readableState.length;
    },
  },

  readableObjectMode: {
    __proto__: null,
    enumerable: false,
    get() {
      return this._readableState ? this._readableState.objectMode : false;
    },
  },

  readableEncoding: {
    __proto__: null,
    enumerable: false,
    get() {
      return this._readableState ? this._readableState.encoding : null;
    },
  },

  errored: {
    __proto__: null,
    enumerable: false,
    get() {
      return this._readableState ? this._readableState.errored : null;
    },
  },

  closed: {
    __proto__: null,
    get() {
      return this._readableState ? this._readableState.closed : false;
    },
  },

  destroyed: {
    __proto__: null,
    enumerable: false,
    get() {
      return this._readableState ? this._readableState.destroyed : false;
    },
    set(value) {
      // We ignore the value if the stream
      // has not been initialized yet.
      if (!this._readableState) {
        return;
      }

      // Backward compatibility, the user is explicitly
      // managing destroyed.
      this._readableState.destroyed = value;
    },
  },

  readableEnded: {
    __proto__: null,
    enumerable: false,
    get() {
      return this._readableState ? this._readableState.endEmitted : false;
    },
  },

});

ObjectDefineProperties(ReadableState.prototype, {
  // Legacy getter for `pipesCount`.
  pipesCount: {
    __proto__: null,
    get() {
      return this.pipes.length;
    },
  },

  // Legacy property for `paused`.
  paused: {
    __proto__: null,
    get() {
      return (this[kState] & kPaused) !== 0;
    },
    set(value) {
      this[kState] |= kHasPaused;
      if (value) {
        this[kState] |= kPaused;
      } else {
        this[kState] &= ~kPaused;
      }
    },
  },
});

// Exposed for testing purposes only.
Readable._fromList = fromList;

// Pluck off n bytes from an array of buffers.
// Length is the combined lengths of all the buffers in the list.
// This function is designed to be inlinable, so please take care when making
// changes to the function body.
function fromList(n, state) {
  // nothing buffered.
  if (state.length === 0)
    return null;

  let idx = state.bufferIndex;
  let ret;

  const buf = state.buffer;
  const len = buf.length;

  if ((state[kState] & kObjectMode) !== 0) {
    ret = buf[idx];
    buf[idx++] = null;
  } else if (!n || n >= state.length) {
    // Read it all, truncate the list.
    if ((state[kState] & kDecoder) !== 0) {
      ret = '';
      while (idx < len) {
        ret += buf[idx];
        buf[idx++] = null;
      }
    } else if (len - idx === 0) {
      ret = Buffer.alloc(0);
    } else if (len - idx === 1) {
      ret = buf[idx];
      buf[idx++] = null;
    } else {
      ret = Buffer.allocUnsafe(state.length);

      let i = 0;
      while (idx < len) {
        TypedArrayPrototypeSet(ret, buf[idx], i);
        i += buf[idx].length;
        buf[idx++] = null;
      }
    }
  } else if (n < buf[idx].length) {
    // `slice` is the same for buffers and strings.
    ret = buf[idx].slice(0, n);
    buf[idx] = buf[idx].slice(n);
  } else if (n === buf[idx].length) {
    // First chunk is a perfect match.
    ret = buf[idx];
    buf[idx++] = null;
  } else if ((state[kState] & kDecoder) !== 0) {
    ret = '';
    while (idx < len) {
      const str = buf[idx];
      if (n > str.length) {
        ret += str;
        n -= str.length;
        buf[idx++] = null;
      } else {
        if (n === buf.length) {
          ret += str;
          buf[idx++] = null;
        } else {
          ret += str.slice(0, n);
          buf[idx] = str.slice(n);
        }
        break;
      }
    }
  } else {
    ret = Buffer.allocUnsafe(n);

    const retLen = n;
    while (idx < len) {
      const data = buf[idx];
      if (n > data.length) {
        TypedArrayPrototypeSet(ret, data, retLen - n);
        n -= data.length;
        buf[idx++] = null;
      } else {
        if (n === data.length) {
          TypedArrayPrototypeSet(ret, data, retLen - n);
          buf[idx++] = null;
        } else {
          TypedArrayPrototypeSet(ret, new FastBuffer(data.buffer, data.byteOffset, n), retLen - n);
          buf[idx] = new FastBuffer(data.buffer, data.byteOffset + n, data.length - n);
        }
        break;
      }
    }
  }

  if (idx === len) {
    state.buffer.length = 0;
    state.bufferIndex = 0;
  } else if (idx > 1024) {
    state.buffer.splice(0, idx);
    state.bufferIndex = 0;
  } else {
    state.bufferIndex = idx;
  }

  return ret;
}

function endReadable(stream) {
  const state = stream._readableState;

  debug('endReadable');
  if ((state[kState] & kEndEmitted) === 0) {
    state[kState] |= kEnded;
    process.nextTick(endReadableNT, state, stream);
  }
}

function endReadableNT(state, stream) {
  debug('endReadableNT');

  // Check that we didn't get one last unshift.
  if ((state[kState] & (kErrored | kCloseEmitted | kEndEmitted)) === 0 && state.length === 0) {
    state[kState] |= kEndEmitted;
    stream.emit('end');

    if (stream.writable && stream.allowHalfOpen === false) {
      process.nextTick(endWritableNT, stream);
    } else if (state.autoDestroy) {
      // In case of duplex streams we need a way to detect
      // if the writable side is ready for autoDestroy as well.
      const wState = stream._writableState;
      const autoDestroy = !wState || (
        wState.autoDestroy &&
        // We don't expect the writable to ever 'finish'
        // if writable is explicitly set to false.
        (wState.finished || wState.writable === false)
      );

      if (autoDestroy) {
        stream.destroy();
      }
    }
  }
}

function endWritableNT(stream) {
  const writable = stream.writable && !stream.writableEnded &&
    !stream.destroyed;
  if (writable) {
    stream.end();
  }
}

Readable.from = function(iterable, opts) {
  return from(Readable, iterable, opts);
};

let webStreamsAdapters;

// Lazy to avoid circular references
function lazyWebStreams() {
  if (webStreamsAdapters === undefined)
    webStreamsAdapters = require('internal/webstreams/adapters');
  return webStreamsAdapters;
}

Readable.fromWeb = function(readableStream, options) {
  return lazyWebStreams().newStreamReadableFromReadableStream(
    readableStream,
    options);
};

Readable.toWeb = function(streamReadable, options) {
  return lazyWebStreams().newReadableStreamFromStreamReadable(
    streamReadable,
    options);
};

Readable.wrap = function(src, options) {
  return new Readable({
    objectMode: src.readableObjectMode ?? src.objectMode ?? true,
    ...options,
    destroy(err, callback) {
      destroyImpl.destroyer(src, err);
      callback(err);
    },
  }).wrap(src);
};
