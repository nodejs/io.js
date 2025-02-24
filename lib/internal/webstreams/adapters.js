'use strict';

const {
  ArrayPrototypeFilter,
  ArrayPrototypeMap,
  Boolean,
  ObjectEntries,
  PromisePrototypeThen,
  PromiseResolve,
  PromiseWithResolvers,
  SafePromiseAll,
  SafePromisePrototypeFinally,
  SafeSet,
  TypeError,
  TypedArrayPrototypeGetBuffer,
  TypedArrayPrototypeGetByteLength,
  TypedArrayPrototypeGetByteOffset,
  Uint8Array,
} = primordials;

const { TextEncoder } = require('internal/encoding');

const {
  ReadableStream,
  isReadableStream,
} = require('internal/webstreams/readablestream');

const {
  WritableStream,
  isWritableStream,
} = require('internal/webstreams/writablestream');

const {
  CountQueuingStrategy,
  ByteLengthQueuingStrategy,
} = require('internal/webstreams/queuingstrategies');

const {
  Writable,
  Readable,
  Duplex,
  destroy,
} = require('stream');

const {
  isDestroyed,
  isReadable,
  isWritable,
  isWritableEnded,
} = require('internal/streams/utils');

const {
  Buffer,
} = require('buffer');

const {
  AbortError,
  ErrnoException,
  codes: {
    ERR_INVALID_ARG_TYPE,
    ERR_INVALID_ARG_VALUE,
    ERR_INVALID_STATE,
    ERR_STREAM_PREMATURE_CLOSE,
  },
} = require('internal/errors');

const {
  kEmptyObject,
  normalizeEncoding,
} = require('internal/util');

const {
  validateBoolean,
  validateFunction,
  validateObject,
} = require('internal/validators');

const {
  WriteWrap,
  ShutdownWrap,
  kReadBytesOrError,
  kLastWriteWasAsync,
  streamBaseState,
} = internalBinding('stream_wrap');

const finished = require('internal/streams/end-of-stream');

const { UV_EOF } = internalBinding('uv');

const encoder = new TextEncoder();

// Collect all negative (error) ZLIB codes and Z_NEED_DICT
const ZLIB_FAILURES = new SafeSet([
  ...ArrayPrototypeFilter(
    ArrayPrototypeMap(
      ObjectEntries(internalBinding('constants').zlib),
      ({ 0: code, 1: value }) => (value < 0 ? code : null),
    ),
    Boolean,
  ),
  'Z_NEED_DICT',
]);

/**
 * @param {Error|null} cause
 * @returns {Error|null}
 */
function handleKnownInternalErrors(cause) {
  switch (true) {
    case cause?.code === 'ERR_STREAM_PREMATURE_CLOSE': {
      return new AbortError(undefined, { cause });
    }
    case ZLIB_FAILURES.has(cause?.code): {
      // eslint-disable-next-line no-restricted-syntax
      const error = new TypeError(undefined, { cause });
      error.code = cause.code;
      return error;
    }
    default:
      return cause;
  }
}

/**
 * @typedef {import('../../stream').Writable} Writable
 * @typedef {import('../../stream').Readable} Readable
 * @typedef {import('./writablestream').WritableStream} WritableStream
 * @typedef {import('./readablestream').ReadableStream} ReadableStream
 */

/**
 * @typedef {import('../abort_controller').AbortSignal} AbortSignal
 */

/**
 * @param {Writable} streamWritable
 * @returns {WritableStream}
 */
function newWritableStreamFromStreamWritable(streamWritable) {
  // Not using the internal/streams/utils isWritableNodeStream utility
  // here because it will return false if streamWritable is a Duplex
  // whose writable option is false. For a Duplex that is not writable,
  // we want it to pass this check but return a closed WritableStream.
  // We check if the given stream is a stream.Writable or http.OutgoingMessage
  const checkIfWritableOrOutgoingMessage =
    streamWritable &&
    typeof streamWritable?.write === 'function' &&
    typeof streamWritable?.on === 'function';
  if (!checkIfWritableOrOutgoingMessage) {
    throw new ERR_INVALID_ARG_TYPE(
      'streamWritable',
      'stream.Writable',
      streamWritable,
    );
  }

  if (isDestroyed(streamWritable) || !isWritable(streamWritable)) {
    const writable = new WritableStream();
    writable.close();
    return writable;
  }

  const highWaterMark = streamWritable.writableHighWaterMark;
  const strategy =
    streamWritable.writableObjectMode ?
      new CountQueuingStrategy({ highWaterMark }) :
      { highWaterMark };

  let controller;
  let backpressurePromise;
  let closed;

  function onDrain() {
    if (backpressurePromise !== undefined)
      backpressurePromise.resolve();
  }

  const cleanup = finished(streamWritable, (error) => {
    error = handleKnownInternalErrors(error);

    cleanup();
    // This is a protection against non-standard, legacy streams
    // that happen to emit an error event again after finished is called.
    streamWritable.on('error', () => {});
    if (error != null) {
      if (backpressurePromise !== undefined)
        backpressurePromise.reject(error);
      // If closed is not undefined, the error is happening
      // after the WritableStream close has already started.
      // We need to reject it here.
      if (closed !== undefined) {
        closed.reject(error);
        closed = undefined;
      }
      controller.error(error);
      controller = undefined;
      return;
    }

    if (closed !== undefined) {
      closed.resolve();
      closed = undefined;
      return;
    }
    controller.error(new AbortError());
    controller = undefined;
  });

  streamWritable.on('drain', onDrain);

  return new WritableStream({
    start(c) { controller = c; },

    write(chunk) {
      if (streamWritable.writableNeedDrain || !streamWritable.write(chunk)) {
        backpressurePromise = PromiseWithResolvers();
        return SafePromisePrototypeFinally(
          backpressurePromise.promise, () => {
            backpressurePromise = undefined;
          });
      }
    },

    abort(reason) {
      destroy(streamWritable, reason);
    },

    close() {
      if (closed === undefined && !isWritableEnded(streamWritable)) {
        closed = PromiseWithResolvers();
        streamWritable.end();
        return closed.promise;
      }

      controller = undefined;
      return PromiseResolve();
    },
  }, strategy);
}

/**
 * @param {WritableStream} writableStream
 * @param {{
 *   decodeStrings? : boolean,
 *   highWaterMark? : number,
 *   objectMode? : boolean,
 *   signal? : AbortSignal,
 * }} [options]
 * @returns {Writable}
 */
function newStreamWritableFromWritableStream(writableStream, options = kEmptyObject) {
  if (!isWritableStream(writableStream)) {
    throw new ERR_INVALID_ARG_TYPE(
      'writableStream',
      'WritableStream',
      writableStream);
  }

  validateObject(options, 'options');
  const {
    highWaterMark,
    decodeStrings = true,
    objectMode = false,
    signal,
  } = options;

  validateBoolean(objectMode, 'options.objectMode');
  validateBoolean(decodeStrings, 'options.decodeStrings');

  const writer = writableStream.getWriter();
  let closed = false;

  const writable = new Writable({
    highWaterMark,
    objectMode,
    decodeStrings,
    signal,

    writev(chunks, callback) {
      function done(error) {
        error = error.filter((e) => e);
        try {
          callback(error.length === 0 ? undefined : error);
        } catch (error) {
          // In a next tick because this is happening within
          // a promise context, and if there are any errors
          // thrown we don't want those to cause an unhandled
          // rejection. Let's just escape the promise and
          // handle it separately.
          process.nextTick(() => destroy(writable, error));
        }
      }

      PromisePrototypeThen(
        writer.ready,
        () => {
          return PromisePrototypeThen(
            SafePromiseAll(
              chunks,
              (data) => writer.write(data.chunk)),
            done,
            done);
        },
        done);
    },

    write(chunk, encoding, callback) {
      if (typeof chunk === 'string' && decodeStrings && !objectMode) {
        const enc = normalizeEncoding(encoding);

        if (enc === 'utf8') {
          chunk = encoder.encode(chunk);
        } else {
          chunk = Buffer.from(chunk, encoding);
          chunk = new Uint8Array(
            TypedArrayPrototypeGetBuffer(chunk),
            TypedArrayPrototypeGetByteOffset(chunk),
            TypedArrayPrototypeGetByteLength(chunk),
          );
        }
      }

      function done(error) {
        try {
          callback(error);
        } catch (error) {
          destroy(writable, error);
        }
      }

      PromisePrototypeThen(
        writer.ready,
        () => {
          return PromisePrototypeThen(
            writer.write(chunk),
            done,
            done);
        },
        done);
    },

    destroy(error, callback) {
      function done() {
        try {
          callback(error);
        } catch (error) {
          // In a next tick because this is happening within
          // a promise context, and if there are any errors
          // thrown we don't want those to cause an unhandled
          // rejection. Let's just escape the promise and
          // handle it separately.
          process.nextTick(() => { throw error; });
        }
      }

      if (!closed) {
        if (error != null) {
          PromisePrototypeThen(
            writer.abort(error),
            done,
            done);
        } else {
          PromisePrototypeThen(
            writer.close(),
            done,
            done);
        }
        return;
      }

      done();
    },

    final(callback) {
      function done(error) {
        try {
          callback(error);
        } catch (error) {
          // In a next tick because this is happening within
          // a promise context, and if there are any errors
          // thrown we don't want those to cause an unhandled
          // rejection. Let's just escape the promise and
          // handle it separately.
          process.nextTick(() => destroy(writable, error));
        }
      }

      if (!closed) {
        PromisePrototypeThen(
          writer.close(),
          done,
          done);
      }
    },
  });

  PromisePrototypeThen(
    writer.closed,
    () => {
      // If the WritableStream closes before the stream.Writable has been
      // ended, we signal an error on the stream.Writable.
      closed = true;
      if (!isWritableEnded(writable))
        destroy(writable, new ERR_STREAM_PREMATURE_CLOSE());
    },
    (error) => {
      // If the WritableStream errors before the stream.Writable has been
      // destroyed, signal an error on the stream.Writable.
      closed = true;
      destroy(writable, error);
    });

  return writable;
}

/**
 * @typedef {import('./queuingstrategies').QueuingStrategy} QueuingStrategy
 * @param {Readable} streamReadable
 * @param {{
 *  strategy : QueuingStrategy
 * }} [options]
 * @returns {ReadableStream}
 */
function newReadableStreamFromStreamReadable(streamReadable, options = kEmptyObject) {
  // Not using the internal/streams/utils isReadableNodeStream utility
  // here because it will return false if streamReadable is a Duplex
  // whose readable option is false. For a Duplex that is not readable,
  // we want it to pass this check but return a closed ReadableStream.
  if (typeof streamReadable?._readableState !== 'object') {
    throw new ERR_INVALID_ARG_TYPE(
      'streamReadable',
      'stream.Readable',
      streamReadable);
  }

  if (isDestroyed(streamReadable) || !isReadable(streamReadable)) {
    const readable = new ReadableStream();
    readable.cancel();
    return readable;
  }

  const objectMode = streamReadable.readableObjectMode;
  const highWaterMark = streamReadable.readableHighWaterMark;

  const evaluateStrategyOrFallback = (strategy) => {
    // If there is a strategy available, use it
    if (strategy)
      return strategy;

    if (objectMode) {
      // When running in objectMode explicitly but no strategy, we just fall
      // back to CountQueuingStrategy
      return new CountQueuingStrategy({ highWaterMark });
    }

    return new ByteLengthQueuingStrategy({ highWaterMark });
  };

  const strategy = evaluateStrategyOrFallback(options?.strategy);

  let controller;
  let wasCanceled = false;

  function onData(chunk) {
    // Copy the Buffer to detach it from the pool.
    if (Buffer.isBuffer(chunk) && !objectMode)
      chunk = new Uint8Array(chunk);
    controller.enqueue(chunk);
    if (controller.desiredSize <= 0)
      streamReadable.pause();
  }

  streamReadable.pause();

  const cleanup = finished(streamReadable, (error) => {
    error = handleKnownInternalErrors(error);

    cleanup();
    // This is a protection against non-standard, legacy streams
    // that happen to emit an error event again after finished is called.
    streamReadable.on('error', () => {});
    if (error)
      return controller.error(error);
    // Was already canceled
    if (wasCanceled) {
      return;
    }
    controller.close();
  });

  streamReadable.on('data', onData);

  return new ReadableStream({
    start(c) { controller = c; },

    pull() { streamReadable.resume(); },

    cancel(reason) {
      wasCanceled = true;
      destroy(streamReadable, reason);
    },
  }, strategy);
}

/**
 * @param {ReadableStream} readableStream
 * @param {{
 *   highWaterMark? : number,
 *   encoding? : string,
 *   objectMode? : boolean,
 *   signal? : AbortSignal,
 * }} [options]
 * @returns {Readable}
 */
function newStreamReadableFromReadableStream(readableStream, options = kEmptyObject) {
  if (!isReadableStream(readableStream)) {
    throw new ERR_INVALID_ARG_TYPE(
      'readableStream',
      'ReadableStream',
      readableStream);
  }

  validateObject(options, 'options');
  const {
    highWaterMark,
    encoding,
    objectMode = false,
    signal,
  } = options;

  if (encoding !== undefined && !Buffer.isEncoding(encoding))
    throw new ERR_INVALID_ARG_VALUE('options.encoding', encoding);
  validateBoolean(objectMode, 'options.objectMode');

  const reader = readableStream.getReader();
  let closed = false;

  const readable = new Readable({
    objectMode,
    highWaterMark,
    encoding,
    signal,

    read() {
      PromisePrototypeThen(
        reader.read(),
        (chunk) => {
          if (chunk.done) {
            // Value should always be undefined here.
            readable.push(null);
          } else {
            readable.push(chunk.value);
          }
        },
        (error) => destroy(readable, error));
    },

    destroy(error, callback) {
      function done() {
        try {
          callback(error);
        } catch (error) {
          // In a next tick because this is happening within
          // a promise context, and if there are any errors
          // thrown we don't want those to cause an unhandled
          // rejection. Let's just escape the promise and
          // handle it separately.
          process.nextTick(() => { throw error; });
        }
      }

      if (!closed) {
        PromisePrototypeThen(
          reader.cancel(error),
          done,
          done);
        return;
      }
      done();
    },
  });

  PromisePrototypeThen(
    reader.closed,
    () => {
      closed = true;
    },
    (error) => {
      closed = true;
      destroy(readable, error);
    });

  return readable;
}

/**
 * @typedef {import('./readablestream').ReadableWritablePair
 * } ReadableWritablePair
 * @typedef {import('../../stream').Duplex} Duplex
 */

/**
 * @param {Duplex} duplex
 * @returns {ReadableWritablePair}
 */
function newReadableWritablePairFromDuplex(duplex) {
  // Not using the internal/streams/utils isWritableNodeStream and
  // isReadableNodeStream utilities here because they will return false
  // if the duplex was created with writable or readable options set to
  // false. Instead, we'll check the readable and writable state after
  // and return closed WritableStream or closed ReadableStream as
  // necessary.
  if (typeof duplex?._writableState !== 'object' ||
      typeof duplex?._readableState !== 'object') {
    throw new ERR_INVALID_ARG_TYPE('duplex', 'stream.Duplex', duplex);
  }

  if (isDestroyed(duplex)) {
    const writable = new WritableStream();
    const readable = new ReadableStream();
    writable.close();
    readable.cancel();
    return { readable, writable };
  }

  const writable =
    isWritable(duplex) ?
      newWritableStreamFromStreamWritable(duplex) :
      new WritableStream();

  if (!isWritable(duplex))
    writable.close();

  const readable =
    isReadable(duplex) ?
      newReadableStreamFromStreamReadable(duplex) :
      new ReadableStream();

  if (!isReadable(duplex))
    readable.cancel();

  return { writable, readable };
}

/**
 * @param {ReadableWritablePair} pair
 * @param {{
 *   allowHalfOpen? : boolean,
 *   decodeStrings? : boolean,
 *   encoding? : string,
 *   highWaterMark? : number,
 *   objectMode? : boolean,
 *   signal? : AbortSignal,
 * }} [options]
 * @returns {Duplex}
 */
function newStreamDuplexFromReadableWritablePair(pair = kEmptyObject, options = kEmptyObject) {
  validateObject(pair, 'pair');
  const {
    readable: readableStream,
    writable: writableStream,
  } = pair;

  if (!isReadableStream(readableStream)) {
    throw new ERR_INVALID_ARG_TYPE(
      'pair.readable',
      'ReadableStream',
      readableStream);
  }
  if (!isWritableStream(writableStream)) {
    throw new ERR_INVALID_ARG_TYPE(
      'pair.writable',
      'WritableStream',
      writableStream);
  }

  validateObject(options, 'options');
  const {
    allowHalfOpen = false,
    objectMode = false,
    encoding,
    decodeStrings = true,
    highWaterMark,
    signal,
  } = options;

  validateBoolean(objectMode, 'options.objectMode');
  if (encoding !== undefined && !Buffer.isEncoding(encoding))
    throw new ERR_INVALID_ARG_VALUE('options.encoding', encoding);

  const writer = writableStream.getWriter();
  const reader = readableStream.getReader();
  let writableClosed = false;
  let readableClosed = false;

  const duplex = new Duplex({
    allowHalfOpen,
    highWaterMark,
    objectMode,
    encoding,
    decodeStrings,
    signal,

    writev(chunks, callback) {
      function done(error) {
        error = error.filter((e) => e);
        try {
          callback(error.length === 0 ? undefined : error);
        } catch (error) {
          // In a next tick because this is happening within
          // a promise context, and if there are any errors
          // thrown we don't want those to cause an unhandled
          // rejection. Let's just escape the promise and
          // handle it separately.
          process.nextTick(() => destroy(duplex, error));
        }
      }

      PromisePrototypeThen(
        writer.ready,
        () => {
          return PromisePrototypeThen(
            SafePromiseAll(
              chunks,
              (data) => writer.write(data.chunk)),
            done,
            done);
        },
        done);
    },

    write(chunk, encoding, callback) {
      if (typeof chunk === 'string' && decodeStrings && !objectMode) {
        const enc = normalizeEncoding(encoding);

        if (enc === 'utf8') {
          chunk = encoder.encode(chunk);
        } else {
          chunk = Buffer.from(chunk, encoding);
          chunk = new Uint8Array(
            TypedArrayPrototypeGetBuffer(chunk),
            TypedArrayPrototypeGetByteOffset(chunk),
            TypedArrayPrototypeGetByteLength(chunk),
          );
        }
      }

      function done(error) {
        try {
          callback(error);
        } catch (error) {
          destroy(duplex, error);
        }
      }

      PromisePrototypeThen(
        writer.ready,
        () => {
          return PromisePrototypeThen(
            writer.write(chunk),
            done,
            done);
        },
        done);
    },

    final(callback) {
      function done(error) {
        try {
          callback(error);
        } catch (error) {
          // In a next tick because this is happening within
          // a promise context, and if there are any errors
          // thrown we don't want those to cause an unhandled
          // rejection. Let's just escape the promise and
          // handle it separately.
          process.nextTick(() => destroy(duplex, error));
        }
      }

      if (!writableClosed) {
        PromisePrototypeThen(
          writer.close(),
          done,
          done);
      }
    },

    read() {
      PromisePrototypeThen(
        reader.read(),
        (chunk) => {
          if (chunk.done) {
            duplex.push(null);
          } else {
            duplex.push(chunk.value);
          }
        },
        (error) => destroy(duplex, error));
    },

    destroy(error, callback) {
      function done() {
        try {
          callback(error);
        } catch (error) {
          // In a next tick because this is happening within
          // a promise context, and if there are any errors
          // thrown we don't want those to cause an unhandled
          // rejection. Let's just escape the promise and
          // handle it separately.
          process.nextTick(() => { throw error; });
        }
      }

      async function closeWriter() {
        if (!writableClosed)
          await writer.abort(error);
      }

      async function closeReader() {
        if (!readableClosed)
          await reader.cancel(error);
      }

      if (!writableClosed || !readableClosed) {
        PromisePrototypeThen(
          SafePromiseAll([
            closeWriter(),
            closeReader(),
          ]),
          done,
          done);
        return;
      }

      done();
    },
  });

  PromisePrototypeThen(
    writer.closed,
    () => {
      writableClosed = true;
      if (!isWritableEnded(duplex))
        destroy(duplex, new ERR_STREAM_PREMATURE_CLOSE());
    },
    (error) => {
      writableClosed = true;
      readableClosed = true;
      destroy(duplex, error);
    });

  PromisePrototypeThen(
    reader.closed,
    () => {
      readableClosed = true;
    },
    (error) => {
      writableClosed = true;
      readableClosed = true;
      destroy(duplex, error);
    });

  return duplex;
}

/**
 * @typedef {import('./queuingstrategies').QueuingStrategy} QueuingStrategy
 * @typedef {{}} StreamBase
 * @param {StreamBase} streamBase
 * @param {QueuingStrategy} strategy
 * @returns {WritableStream}
 */
function newWritableStreamFromStreamBase(streamBase, strategy) {
  validateObject(streamBase, 'streamBase');

  let current;

  function createWriteWrap(controller, promise) {
    const req = new WriteWrap();
    req.handle = streamBase;
    req.oncomplete = onWriteComplete;
    req.async = false;
    req.bytes = 0;
    req.buffer = null;
    req.controller = controller;
    req.promise = promise;
    return req;
  }

  function onWriteComplete(status) {
    if (status < 0) {
      const error = new ErrnoException(status, 'write', this.error);
      this.promise.reject(error);
      this.controller.error(error);
      return;
    }
    this.promise.resolve();
  }

  function doWrite(chunk, controller) {
    const promise = PromiseWithResolvers();
    let ret;
    let req;
    try {
      req = createWriteWrap(controller, promise);
      ret = streamBase.writeBuffer(req, chunk);
      if (streamBaseState[kLastWriteWasAsync])
        req.buffer = chunk;
      req.async = !!streamBaseState[kLastWriteWasAsync];
    } catch (error) {
      promise.reject(error);
    }

    if (ret !== 0)
      promise.reject(new ErrnoException(ret, 'write', req));
    else if (!req.async)
      promise.resolve();

    return promise.promise;
  }

  return new WritableStream({
    write(chunk, controller) {
      current = current !== undefined ?
        PromisePrototypeThen(
          current,
          () => doWrite(chunk, controller),
          (error) => controller.error(error)) :
        doWrite(chunk, controller);
      return current;
    },

    close() {
      const promise = PromiseWithResolvers();
      const req = new ShutdownWrap();
      req.oncomplete = () => promise.resolve();
      const err = streamBase.shutdown(req);
      if (err === 1)
        promise.resolve();
      return promise.promise;
    },
  }, strategy);
}

/**
 * @param {StreamBase} streamBase
 * @param {QueuingStrategy} strategy
 * @returns {ReadableStream}
 */
function newReadableStreamFromStreamBase(streamBase, strategy, options = kEmptyObject) {
  validateObject(streamBase, 'streamBase');
  validateObject(options, 'options');

  const {
    ondone = () => {},
  } = options;

  if (typeof streamBase.onread === 'function')
    throw new ERR_INVALID_STATE('StreamBase already has a consumer');

  validateFunction(ondone, 'options.ondone');

  let controller;

  streamBase.onread = (arrayBuffer) => {
    const nread = streamBaseState[kReadBytesOrError];

    if (nread === 0)
      return;

    try {
      if (nread === UV_EOF) {
        controller.close();
        streamBase.readStop();
        try {
          ondone();
        } catch (error) {
          controller.error(error);
        }
        return;
      }

      controller.enqueue(arrayBuffer);

      if (controller.desiredSize <= 0)
        streamBase.readStop();
    } catch (error) {
      controller.error(error);
      streamBase.readStop();
    }
  };

  return new ReadableStream({
    start(c) { controller = c; },

    pull() {
      streamBase.readStart();
    },

    cancel() {
      const promise = PromiseWithResolvers();
      try {
        ondone();
      } catch (error) {
        promise.reject(error);
        return promise.promise;
      }
      const req = new ShutdownWrap();
      req.oncomplete = () => promise.resolve();
      const err = streamBase.shutdown(req);
      if (err === 1)
        promise.resolve();
      return promise.promise;
    },
  }, strategy);
}

module.exports = {
  newWritableStreamFromStreamWritable,
  newReadableStreamFromStreamReadable,
  newStreamWritableFromWritableStream,
  newStreamReadableFromReadableStream,
  newReadableWritablePairFromDuplex,
  newStreamDuplexFromReadableWritablePair,
  newWritableStreamFromStreamBase,
  newReadableStreamFromStreamBase,
};
