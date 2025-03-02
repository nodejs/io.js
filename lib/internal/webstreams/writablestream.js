'use strict';

const {
  ArrayPrototypePush,
  ArrayPrototypeShift,
  FunctionPrototypeBind,
  FunctionPrototypeCall,
  ObjectDefineProperties,
  ObjectSetPrototypeOf,
  Promise,
  PromisePrototypeThen,
  PromiseReject,
  PromiseResolve,
  PromiseWithResolvers,
  Symbol,
  SymbolToStringTag,
} = primordials;

const {
  codes: {
    ERR_ILLEGAL_CONSTRUCTOR,
    ERR_INVALID_ARG_TYPE,
    ERR_INVALID_ARG_VALUE,
    ERR_INVALID_STATE,
    ERR_INVALID_THIS,
  },
} = require('internal/errors');

const {
  DOMException,
} = internalBinding('messaging');

const {
  customInspectSymbol: kInspect,
  kEmptyObject,
  kEnumerableProperty,
  SideEffectFreeRegExpPrototypeSymbolReplace,
} = require('internal/util');

const {
  validateObject,
  kValidateObjectAllowObjects,
  kValidateObjectAllowObjectsAndNull,
} = require('internal/validators');

const {
  MessageChannel,
} = require('internal/worker/io');

const {
  kDeserialize,
  kTransfer,
  kTransferList,
  markTransferMode,
} = require('internal/worker/js_transferable');

const {
  createPromiseCallback,
  customInspect,
  dequeueValue,
  enqueueValueWithSize,
  extractHighWaterMark,
  extractSizeAlgorithm,
  lazyTransfer,
  isBrandCheck,
  isPromisePending,
  peekQueueValue,
  resetQueue,
  setPromiseHandled,
  nonOpCancel,
  nonOpStart,
  nonOpWrite,
  kType,
  kState,
} = require('internal/webstreams/util');

const {
  kIsClosedPromise,
  kControllerErrorFunction,
} = require('internal/streams/utils');

const {
  AbortController,
} = require('internal/abort_controller');

const assert = require('internal/assert');

const kAbort = Symbol('kAbort');
const kCloseSentinel = Symbol('kCloseSentinel');
const kError = Symbol('kError');
const kSkipThrow = Symbol('kSkipThrow');

let releasedError;

function lazyWritableReleasedError() {
  if (releasedError) {
    return releasedError;
  }
  const userModuleRegExp = /^ {4}at (?:[^/\\(]+ \()(?!node:(.+):\d+:\d+\)$).*/gm;

  releasedError = new ERR_INVALID_STATE.TypeError('Writer has been released');
  // Avoid V8 leak and remove userland stackstrace
  releasedError.stack = SideEffectFreeRegExpPrototypeSymbolReplace(userModuleRegExp, releasedError.stack, '');
  return releasedError;
}

const getNonWritablePropertyDescriptor = (value) => {
  return {
    __proto__: null,
    configurable: true,
    value,
  };
};

/**
 * @typedef {import('../abort_controller').AbortSignal} AbortSignal
 * @typedef {import('./queuingstrategies').QueuingStrategy
 * } QueuingStrategy
 * @typedef {import('./queuingstrategies').QueuingStrategySize
 * } QueuingStrategySize
 */

/**
 * @callback UnderlyingSinkStartCallback
 * @param {WritableStreamDefaultController} controller
 */

/**
 * @callback UnderlyingSinkWriteCallback
 * @param {any} chunk
 * @param {WritableStreamDefaultController} controller
 * @returns {Promise<void>}
 */

/**
 * @callback UnderlyingSinkCloseCallback
 * @returns {Promise<void>}
 */

/**
 * @callback UnderlyingSinkAbortCallback
 * @param {any} reason
 * @returns {Promise<void>}
 */

/**
 * @typedef {{
 *   start? : UnderlyingSinkStartCallback,
 *   write? : UnderlyingSinkWriteCallback,
 *   close? : UnderlyingSinkCloseCallback,
 *   abort? : UnderlyingSinkAbortCallback,
 *   type? : any,
 * }} UnderlyingSink
 */

class WritableStream {
  [kType] = 'WritableStream';

  /**
   * @param {UnderlyingSink} [sink]
   * @param {QueuingStrategy} [strategy]
   */
  constructor(sink = kEmptyObject, strategy = kEmptyObject) {
    markTransferMode(this, false, true);
    validateObject(sink, 'sink', kValidateObjectAllowObjects);
    validateObject(strategy, 'strategy', kValidateObjectAllowObjectsAndNull);
    const type = sink?.type;
    if (type !== undefined)
      throw new ERR_INVALID_ARG_VALUE.RangeError('type', type);

    this[kState] = createWritableStreamState();

    this[kIsClosedPromise] = PromiseWithResolvers();
    this[kControllerErrorFunction] = () => {};

    const size = extractSizeAlgorithm(strategy?.size);
    const highWaterMark = extractHighWaterMark(strategy?.highWaterMark, 1);

    setupWritableStreamDefaultControllerFromSink(
      this,
      sink,
      highWaterMark,
      size);
  }

  /**
   * @readonly
   * @type {boolean}
   */
  get locked() {
    if (!isWritableStream(this))
      throw new ERR_INVALID_THIS('WritableStream');
    return isWritableStreamLocked(this);
  }

  /**
   * @param {any} [reason]
   * @returns {Promise<void>}
   */
  abort(reason = undefined) {
    if (!isWritableStream(this))
      return PromiseReject(new ERR_INVALID_THIS('WritableStream'));
    if (isWritableStreamLocked(this)) {
      return PromiseReject(
        new ERR_INVALID_STATE.TypeError('WritableStream is locked'));
    }
    return writableStreamAbort(this, reason);
  }

  /**
   * @returns {Promise<void>}
   */
  close() {
    if (!isWritableStream(this))
      return PromiseReject(new ERR_INVALID_THIS('WritableStream'));
    if (isWritableStreamLocked(this)) {
      return PromiseReject(
        new ERR_INVALID_STATE.TypeError('WritableStream is locked'));
    }
    if (writableStreamCloseQueuedOrInFlight(this)) {
      return PromiseReject(
        new ERR_INVALID_STATE.TypeError('Failure closing WritableStream'));
    }
    return writableStreamClose(this);
  }

  /**
   * @returns {WritableStreamDefaultWriter}
   */
  getWriter() {
    if (!isWritableStream(this))
      throw new ERR_INVALID_THIS('WritableStream');
    // eslint-disable-next-line no-use-before-define
    return new WritableStreamDefaultWriter(this);
  }

  [kInspect](depth, options) {
    return customInspect(depth, options, this[kType], {
      locked: this.locked,
      state: this[kState].state,
    });
  }

  [kTransfer]() {
    if (!isWritableStream(this))
      throw new ERR_INVALID_THIS('WritableStream');
    if (this.locked) {
      this[kState].transfer.port1?.close();
      this[kState].transfer.port1 = undefined;
      this[kState].transfer.port2 = undefined;
      throw new DOMException(
        'Cannot transfer a locked WritableStream',
        'DataCloneError');
    }

    const {
      readable,
      promise,
    } = lazyTransfer().newCrossRealmReadableStream(
      this,
      this[kState].transfer.port1);

    this[kState].transfer.readable = readable;
    this[kState].transfer.promise = promise;

    return {
      data: { port: this[kState].transfer.port2 },
      deserializeInfo:
        'internal/webstreams/writablestream:TransferredWritableStream',
    };
  }

  [kTransferList]() {
    const { port1, port2 } = new MessageChannel();
    this[kState].transfer.port1 = port1;
    this[kState].transfer.port2 = port2;
    return [ port2 ];
  }

  [kDeserialize]({ port }) {
    const transfer = lazyTransfer();
    setupWritableStreamDefaultControllerFromSink(
      this,
      // The MessagePort is set to be referenced when reading.
      // After two MessagePorts are closed, there is a problem with
      // lingering promise not being properly resolved.
      // https://github.com/nodejs/node/issues/51486
      new transfer.CrossRealmTransformWritableSink(port, true),
      1,
      () => 1);
  }
}

ObjectDefineProperties(WritableStream.prototype, {
  locked: kEnumerableProperty,
  abort: kEnumerableProperty,
  close: kEnumerableProperty,
  getWriter: kEnumerableProperty,
  [SymbolToStringTag]: getNonWritablePropertyDescriptor(WritableStream.name),
});

function InternalTransferredWritableStream() {
  ObjectSetPrototypeOf(this, WritableStream.prototype);
  markTransferMode(this, false, true);
  this[kType] = 'WritableStream';
  this[kState] = createWritableStreamState();

  this[kIsClosedPromise] = PromiseWithResolvers();
}

ObjectSetPrototypeOf(InternalTransferredWritableStream.prototype, WritableStream.prototype);
ObjectSetPrototypeOf(InternalTransferredWritableStream, WritableStream);

function TransferredWritableStream() {
  const stream = new InternalTransferredWritableStream();

  stream.constructor = WritableStream;

  return stream;
}

TransferredWritableStream.prototype[kDeserialize] = () => {};

class WritableStreamDefaultWriter {
  [kType] = 'WritableStreamDefaultWriter';

  /**
   * @param {WritableStream} stream
   */
  constructor(stream) {
    if (!isWritableStream(stream))
      throw new ERR_INVALID_ARG_TYPE('stream', 'WritableStream', stream);
    this[kState] = {
      stream: undefined,
      close: {
        promise: undefined,
        resolve: undefined,
        reject: undefined,
      },
      ready: {
        promise: undefined,
        resolve: undefined,
        reject: undefined,
      },
    };
    setupWritableStreamDefaultWriter(this, stream);
  }

  /**
   * @readonly
   * @type {Promise<void>}
   */
  get closed() {
    if (!isWritableStreamDefaultWriter(this))
      return PromiseReject(new ERR_INVALID_THIS('WritableStreamDefaultWriter'));
    return this[kState].close.promise;
  }

  /**
   * @readonly
   * @type {number}
   */
  get desiredSize() {
    if (!isWritableStreamDefaultWriter(this))
      throw new ERR_INVALID_THIS('WritableStreamDefaultWriter');
    if (this[kState].stream === undefined) {
      throw new ERR_INVALID_STATE.TypeError(
        'Writer is not bound to a WritableStream');
    }
    return writableStreamDefaultWriterGetDesiredSize(this);
  }

  /**
   * @readonly
   * @type {Promise<void>}
   */
  get ready() {
    if (!isWritableStreamDefaultWriter(this))
      return PromiseReject(new ERR_INVALID_THIS('WritableStreamDefaultWriter'));
    return this[kState].ready.promise;
  }

  /**
   * @param {any} [reason]
   * @returns {Promise<void>}
   */
  abort(reason = undefined) {
    if (!isWritableStreamDefaultWriter(this))
      return PromiseReject(new ERR_INVALID_THIS('WritableStreamDefaultWriter'));
    if (this[kState].stream === undefined) {
      return PromiseReject(
        new ERR_INVALID_STATE.TypeError(
          'Writer is not bound to a WritableStream'));
    }
    return writableStreamDefaultWriterAbort(this, reason);
  }

  /**
   * @returns {Promise<void>}
   */
  close() {
    if (!isWritableStreamDefaultWriter(this))
      return PromiseReject(new ERR_INVALID_THIS('WritableStreamDefaultWriter'));
    const {
      stream,
    } = this[kState];
    if (stream === undefined) {
      return PromiseReject(
        new ERR_INVALID_STATE.TypeError(
          'Writer is not bound to a WritableStream'));
    }
    if (writableStreamCloseQueuedOrInFlight(stream)) {
      return PromiseReject(
        new ERR_INVALID_STATE.TypeError('Failure to close WritableStream'));
    }
    return writableStreamDefaultWriterClose(this);
  }

  releaseLock() {
    if (!isWritableStreamDefaultWriter(this))
      throw new ERR_INVALID_THIS('WritableStreamDefaultWriter');
    const {
      stream,
    } = this[kState];
    if (stream === undefined)
      return;
    assert(stream[kState].writer !== undefined);
    writableStreamDefaultWriterRelease(this);
  }

  /**
   * @param {any} [chunk]
   * @returns {Promise<void>}
   */
  write(chunk = undefined) {
    if (!isWritableStreamDefaultWriter(this))
      return PromiseReject(new ERR_INVALID_THIS('WritableStreamDefaultWriter'));
    if (this[kState].stream === undefined) {
      return PromiseReject(
        new ERR_INVALID_STATE.TypeError(
          'Writer is not bound to a WritableStream'));
    }
    return writableStreamDefaultWriterWrite(this, chunk);
  }

  [kInspect](depth, options) {
    return customInspect(depth, options, this[kType], {
      stream: this[kState].stream,
      close: this[kState].close.promise,
      ready: this[kState].ready.promise,
      desiredSize: this.desiredSize,
    });
  }
}

ObjectDefineProperties(WritableStreamDefaultWriter.prototype, {
  closed: kEnumerableProperty,
  ready: kEnumerableProperty,
  desiredSize: kEnumerableProperty,
  abort: kEnumerableProperty,
  close: kEnumerableProperty,
  releaseLock: kEnumerableProperty,
  write: kEnumerableProperty,
  [SymbolToStringTag]: getNonWritablePropertyDescriptor(WritableStreamDefaultWriter.name),
});

class WritableStreamDefaultController {
  [kType] = 'WritableStreamDefaultController';

  constructor(skipThrowSymbol = undefined) {
    if (skipThrowSymbol !== kSkipThrow) {
      throw new ERR_ILLEGAL_CONSTRUCTOR();
    }
  }

  [kAbort](reason) {
    const result = this[kState].abortAlgorithm(reason);
    writableStreamDefaultControllerClearAlgorithms(this);
    return result;
  }

  [kError]() {
    resetQueue(this);
  }

  /**
   * @type {AbortSignal}
   */
  get signal() {
    if (!isWritableStreamDefaultController(this))
      throw new ERR_INVALID_THIS('WritableStreamDefaultController');
    return this[kState].abortController.signal;
  }

  /**
   * @param {any} [error]
   */
  error(error = undefined) {
    if (!isWritableStreamDefaultController(this))
      throw new ERR_INVALID_THIS('WritableStreamDefaultController');
    if (this[kState].stream[kState].state !== 'writable')
      return;
    writableStreamDefaultControllerError(this, error);
  }

  [kInspect](depth, options) {
    return customInspect(depth, options, this[kType], {
      stream: this[kState].stream,
    });
  }
}

ObjectDefineProperties(WritableStreamDefaultController.prototype, {
  signal: kEnumerableProperty,
  error: kEnumerableProperty,
  [SymbolToStringTag]: getNonWritablePropertyDescriptor(WritableStreamDefaultController.name),
});

function InternalWritableStream(start, write, close, abort, highWaterMark, size) {
  ObjectSetPrototypeOf(this, WritableStream.prototype);
  markTransferMode(this, false, true);
  this[kType] = 'WritableStream';
  this[kState] = createWritableStreamState();
  this[kIsClosedPromise] = PromiseWithResolvers();

  const controller = new WritableStreamDefaultController(kSkipThrow);
  setupWritableStreamDefaultController(
    this,
    controller,
    start,
    write,
    close,
    abort,
    highWaterMark,
    size,
  );
}

ObjectSetPrototypeOf(InternalWritableStream.prototype, WritableStream.prototype);
ObjectSetPrototypeOf(InternalWritableStream, WritableStream);

function createWritableStream(start, write, close, abort, highWaterMark = 1, size = () => 1) {
  const stream = new InternalWritableStream(start, write, close, abort, highWaterMark, size);

  // For spec compliance the InternalWritableStream must be a WritableStream
  stream.constructor = WritableStream;
  return stream;
}

const isWritableStream =
  isBrandCheck('WritableStream');
const isWritableStreamDefaultWriter =
  isBrandCheck('WritableStreamDefaultWriter');
const isWritableStreamDefaultController =
  isBrandCheck('WritableStreamDefaultController');

function createWritableStreamState() {
  return {
    __proto__: null,
    close: PromiseWithResolvers(),
    closeRequest: {
      __proto__: null,
      promise: undefined,
      resolve: undefined,
      reject: undefined,
    },
    inFlightWriteRequest: {
      __proto__: null,
      promise: undefined,
      resolve: undefined,
      reject: undefined,
    },
    inFlightCloseRequest: {
      __proto__: null,
      promise: undefined,
      resolve: undefined,
      reject: undefined,
    },
    pendingAbortRequest: {
      __proto__: null,
      abort: {
        __proto__: null,
        promise: undefined,
        resolve: undefined,
        reject: undefined,
      },
      reason: undefined,
      wasAlreadyErroring: false,
    },
    backpressure: false,
    controller: undefined,
    state: 'writable',
    storedError: undefined,
    writeRequests: [],
    writer: undefined,
    transfer: {
      __proto__: null,
      readable: undefined,
      port1: undefined,
      port2: undefined,
      promise: undefined,
    },
  };
}

function isWritableStreamLocked(stream) {
  return stream[kState].writer !== undefined;
}

function setupWritableStreamDefaultWriter(writer, stream) {
  if (isWritableStreamLocked(stream))
    throw new ERR_INVALID_STATE.TypeError('WritableStream is locked');
  writer[kState].stream = stream;
  stream[kState].writer = writer;
  switch (stream[kState].state) {
    case 'writable':
      if (!writableStreamCloseQueuedOrInFlight(stream) &&
          stream[kState].backpressure) {
        writer[kState].ready = PromiseWithResolvers();
      } else {
        writer[kState].ready = {
          promise: PromiseResolve(),
          resolve: undefined,
          reject: undefined,
        };
      }
      setClosedPromiseToNewPromise();
      break;
    case 'erroring':
      writer[kState].ready = {
        promise: PromiseReject(stream[kState].storedError),
        resolve: undefined,
        reject: undefined,
      };
      setPromiseHandled(writer[kState].ready.promise);
      setClosedPromiseToNewPromise();
      break;
    case 'closed':
      writer[kState].ready = {
        promise: PromiseResolve(),
        resolve: undefined,
        reject: undefined,
      };
      writer[kState].close = {
        promise: PromiseResolve(),
        resolve: undefined,
        reject: undefined,
      };
      break;
    default:
      writer[kState].ready = {
        promise: PromiseReject(stream[kState].storedError),
        resolve: undefined,
        reject: undefined,
      };
      writer[kState].close = {
        promise: PromiseReject(stream[kState].storedError),
        resolve: undefined,
        reject: undefined,
      };
      setPromiseHandled(writer[kState].ready.promise);
      setPromiseHandled(writer[kState].close.promise);
  }

  function setClosedPromiseToNewPromise() {
    writer[kState].close = PromiseWithResolvers();
  }
}

function writableStreamAbort(stream, reason) {
  const {
    state,
    controller,
  } = stream[kState];
  if (state === 'closed' || state === 'errored')
    return PromiseResolve();

  controller[kState].abortController.abort(reason);

  if (stream[kState].pendingAbortRequest.abort.promise !== undefined)
    return stream[kState].pendingAbortRequest.abort.promise;

  assert(state === 'writable' || state === 'erroring');

  let wasAlreadyErroring = false;
  if (state === 'erroring') {
    wasAlreadyErroring = true;
    reason = undefined;
  }

  const abort = PromiseWithResolvers();

  stream[kState].pendingAbortRequest = {
    abort,
    reason,
    wasAlreadyErroring,
  };

  if (!wasAlreadyErroring)
    writableStreamStartErroring(stream, reason);

  return abort.promise;
}

function writableStreamClose(stream) {
  const {
    state,
    writer,
    backpressure,
    controller,
  } = stream[kState];
  if (state === 'closed' || state === 'errored') {
    return PromiseReject(
      new ERR_INVALID_STATE.TypeError('WritableStream is closed'));
  }
  assert(state === 'writable' || state === 'erroring');
  assert(!writableStreamCloseQueuedOrInFlight(stream));
  stream[kState].closeRequest = PromiseWithResolvers();
  const { promise } = stream[kState].closeRequest;
  if (writer !== undefined && backpressure && state === 'writable')
    writer[kState].ready.resolve?.();
  writableStreamDefaultControllerClose(controller);
  return promise;
}

function writableStreamUpdateBackpressure(stream, backpressure) {
  assert(stream[kState].state === 'writable');
  assert(!writableStreamCloseQueuedOrInFlight(stream));
  const {
    writer,
  } = stream[kState];
  if (writer !== undefined && stream[kState].backpressure !== backpressure) {
    if (backpressure) {
      writer[kState].ready = PromiseWithResolvers();
    } else {
      writer[kState].ready.resolve?.();
    }
  }
  stream[kState].backpressure = backpressure;
}

function writableStreamStartErroring(stream, reason) {
  assert(stream[kState].storedError === undefined);
  assert(stream[kState].state === 'writable');
  const {
    controller,
    writer,
  } = stream[kState];
  assert(controller !== undefined);
  stream[kState].state = 'erroring';
  stream[kState].storedError = reason;
  if (writer !== undefined) {
    writableStreamDefaultWriterEnsureReadyPromiseRejected(writer, reason);
  }
  if (!writableStreamHasOperationMarkedInFlight(stream) &&
      controller[kState].started) {
    writableStreamFinishErroring(stream);
  }
}

function writableStreamRejectCloseAndClosedPromiseIfNeeded(stream) {
  assert(stream[kState].state === 'errored');
  if (stream[kState].closeRequest.promise !== undefined) {
    assert(stream[kState].inFlightCloseRequest.promise === undefined);
    stream[kState].closeRequest.reject?.(stream[kState].storedError);
    stream[kState].closeRequest = {
      promise: undefined,
      reject: undefined,
      resolve: undefined,
    };
  }

  setPromiseHandled(stream[kIsClosedPromise].promise);
  stream[kIsClosedPromise].reject(stream[kState]?.storedError);

  const {
    writer,
  } = stream[kState];
  if (writer !== undefined) {
    setPromiseHandled(writer[kState].close.promise);
    writer[kState].close.reject?.(stream[kState].storedError);
  }
}

function writableStreamMarkFirstWriteRequestInFlight(stream) {
  assert(stream[kState].inFlightWriteRequest.promise === undefined);
  assert(stream[kState].writeRequests.length);
  const writeRequest = ArrayPrototypeShift(stream[kState].writeRequests);
  stream[kState].inFlightWriteRequest = writeRequest;
}

function writableStreamMarkCloseRequestInFlight(stream) {
  assert(stream[kState].inFlightWriteRequest.promise === undefined);
  assert(stream[kState].closeRequest.promise !== undefined);
  stream[kState].inFlightCloseRequest = stream[kState].closeRequest;
  stream[kState].closeRequest = {
    promise: undefined,
    resolve: undefined,
    reject: undefined,
  };
}

function writableStreamHasOperationMarkedInFlight(stream) {
  const {
    inFlightWriteRequest,
    inFlightCloseRequest,
  } = stream[kState];
  if (inFlightWriteRequest.promise === undefined &&
      inFlightCloseRequest.promise === undefined) {
    return false;
  }
  return true;
}

function writableStreamFinishInFlightWriteWithError(stream, error) {
  assert(stream[kState].inFlightWriteRequest.promise !== undefined);
  stream[kState].inFlightWriteRequest.reject?.(error);
  stream[kState].inFlightWriteRequest = {
    promise: undefined,
    resolve: undefined,
    reject: undefined,
  };
  assert(stream[kState].state === 'writable' ||
         stream[kState].state === 'erroring');
  writableStreamDealWithRejection(stream, error);
}

function writableStreamFinishInFlightWrite(stream) {
  assert(stream[kState].inFlightWriteRequest.promise !== undefined);
  stream[kState].inFlightWriteRequest.resolve?.();
  stream[kState].inFlightWriteRequest = {
    promise: undefined,
    resolve: undefined,
    reject: undefined,
  };
}

function writableStreamFinishInFlightCloseWithError(stream, error) {
  assert(stream[kState].inFlightCloseRequest.promise !== undefined);
  stream[kState].inFlightCloseRequest.reject?.(error);
  stream[kState].inFlightCloseRequest = {
    promise: undefined,
    resolve: undefined,
    reject: undefined,
  };
  assert(stream[kState].state === 'writable' ||
         stream[kState].state === 'erroring');
  if (stream[kState].pendingAbortRequest.abort.promise !== undefined) {
    stream[kState].pendingAbortRequest.abort.reject?.(error);
    stream[kState].pendingAbortRequest = {
      abort: {
        promise: undefined,
        resolve: undefined,
        reject: undefined,
      },
      reason: undefined,
      wasAlreadyErroring: false,
    };
  }
  writableStreamDealWithRejection(stream, error);
}

function writableStreamFinishInFlightClose(stream) {
  assert(stream[kState].inFlightCloseRequest.promise !== undefined);
  stream[kState].inFlightCloseRequest.resolve?.();
  stream[kState].inFlightCloseRequest = {
    promise: undefined,
    resolve: undefined,
    reject: undefined,
  };
  if (stream[kState].state === 'erroring') {
    stream[kState].storedError = undefined;
    if (stream[kState].pendingAbortRequest.abort.promise !== undefined) {
      stream[kState].pendingAbortRequest.abort.resolve?.();
      stream[kState].pendingAbortRequest = {
        abort: {
          promise: undefined,
          resolve: undefined,
          reject: undefined,
        },
        reason: undefined,
        wasAlreadyErroring: false,
      };
    }
  }
  stream[kState].state = 'closed';
  if (stream[kState].writer !== undefined)
    stream[kState].writer[kState].close.resolve?.();
  stream[kIsClosedPromise].resolve?.();
  assert(stream[kState].pendingAbortRequest.abort.promise === undefined);
  assert(stream[kState].storedError === undefined);
}

function writableStreamFinishErroring(stream) {
  assert(stream[kState].state === 'erroring');
  assert(!writableStreamHasOperationMarkedInFlight(stream));
  stream[kState].state = 'errored';
  stream[kState].controller[kError]();
  const storedError = stream[kState].storedError;
  for (let n = 0; n < stream[kState].writeRequests.length; n++)
    stream[kState].writeRequests[n].reject?.(storedError);
  stream[kState].writeRequests = [];

  if (stream[kState].pendingAbortRequest.abort.promise === undefined) {
    writableStreamRejectCloseAndClosedPromiseIfNeeded(stream);
    return;
  }

  const abortRequest = stream[kState].pendingAbortRequest;
  stream[kState].pendingAbortRequest = {
    abort: {
      promise: undefined,
      resolve: undefined,
      reject: undefined,
    },
    reason: undefined,
    wasAlreadyErroring: false,
  };
  if (abortRequest.wasAlreadyErroring) {
    abortRequest.abort.reject?.(storedError);
    writableStreamRejectCloseAndClosedPromiseIfNeeded(stream);
    return;
  }
  PromisePrototypeThen(
    stream[kState].controller[kAbort](abortRequest.reason),
    () => {
      abortRequest.abort.resolve?.();
      writableStreamRejectCloseAndClosedPromiseIfNeeded(stream);
    },
    (error) => {
      abortRequest.abort.reject?.(error);
      writableStreamRejectCloseAndClosedPromiseIfNeeded(stream);
    });
}

function writableStreamDealWithRejection(stream, error) {
  const {
    state,
  } = stream[kState];
  if (state === 'writable') {
    writableStreamStartErroring(stream, error);
    return;
  }

  assert(state === 'erroring');
  writableStreamFinishErroring(stream);
}

function writableStreamCloseQueuedOrInFlight(stream) {
  if (stream[kState].closeRequest.promise === undefined &&
      stream[kState].inFlightCloseRequest.promise === undefined) {
    return false;
  }
  return true;
}

function writableStreamAddWriteRequest(stream) {
  assert(isWritableStreamLocked(stream));
  assert(stream[kState].state === 'writable');
  const {
    promise,
    resolve,
    reject,
  } = PromiseWithResolvers();
  ArrayPrototypePush(
    stream[kState].writeRequests,
    {
      promise,
      resolve,
      reject,
    });
  return promise;
}

function writableStreamDefaultWriterWrite(writer, chunk) {
  const {
    stream,
  } = writer[kState];
  assert(stream !== undefined);
  const {
    controller,
  } = stream[kState];
  const chunkSize = writableStreamDefaultControllerGetChunkSize(
    controller,
    chunk);
  if (stream !== writer[kState].stream) {
    return PromiseReject(
      new ERR_INVALID_STATE.TypeError('Mismatched WritableStreams'));
  }
  const {
    state,
  } = stream[kState];

  if (state === 'errored')
    return PromiseReject(stream[kState].storedError);

  if (writableStreamCloseQueuedOrInFlight(stream) || state === 'closed') {
    return PromiseReject(
      new ERR_INVALID_STATE.TypeError('WritableStream is closed'));
  }

  if (state === 'erroring')
    return PromiseReject(stream[kState].storedError);

  assert(state === 'writable');

  const promise = writableStreamAddWriteRequest(stream);
  writableStreamDefaultControllerWrite(controller, chunk, chunkSize);
  return promise;
}

function writableStreamDefaultWriterRelease(writer) {
  const {
    stream,
  } = writer[kState];
  assert(stream !== undefined);
  assert(stream[kState].writer === writer);
  const releasedStateError = lazyWritableReleasedError();
  writableStreamDefaultWriterEnsureReadyPromiseRejected(writer, releasedStateError);
  writableStreamDefaultWriterEnsureClosedPromiseRejected(writer, releasedStateError);
  stream[kState].writer = undefined;
  writer[kState].stream = undefined;
}

function writableStreamDefaultWriterGetDesiredSize(writer) {
  const {
    stream,
  } = writer[kState];
  switch (stream[kState].state) {
    case 'errored':
      // Fall through
    case 'erroring':
      return null;
    case 'closed':
      return 0;
  }
  return writableStreamDefaultControllerGetDesiredSize(
    stream[kState].controller);
}

function writableStreamDefaultWriterEnsureReadyPromiseRejected(writer, error) {
  if (isPromisePending(writer[kState].ready.promise)) {
    writer[kState].ready.reject?.(error);
  } else {
    writer[kState].ready = {
      promise: PromiseReject(error),
      resolve: undefined,
      reject: undefined,
    };
  }
  setPromiseHandled(writer[kState].ready.promise);
}

function writableStreamDefaultWriterEnsureClosedPromiseRejected(writer, error) {
  if (isPromisePending(writer[kState].close.promise)) {
    writer[kState].close.reject?.(error);
  } else {
    writer[kState].close = {
      promise: PromiseReject(error),
      resolve: undefined,
      reject: undefined,
    };
  }
  setPromiseHandled(writer[kState].close.promise);
}

function writableStreamDefaultWriterCloseWithErrorPropagation(writer) {
  const {
    stream,
  } = writer[kState];
  assert(stream !== undefined);
  const {
    state,
  } = stream[kState];
  if (writableStreamCloseQueuedOrInFlight(stream) || state === 'closed')
    return PromiseResolve();

  if (state === 'errored')
    return PromiseReject(stream[kState].storedError);

  assert(state === 'writable' || state === 'erroring');

  return writableStreamDefaultWriterClose(writer);
}

function writableStreamDefaultWriterClose(writer) {
  const {
    stream,
  } = writer[kState];
  assert(stream !== undefined);
  return writableStreamClose(stream);
}

function writableStreamDefaultWriterAbort(writer, reason) {
  const {
    stream,
  } = writer[kState];
  assert(stream !== undefined);
  return writableStreamAbort(stream, reason);
}

function writableStreamDefaultControllerWrite(controller, chunk, chunkSize) {
  try {
    enqueueValueWithSize(controller, chunk, chunkSize);
  } catch (error) {
    writableStreamDefaultControllerErrorIfNeeded(controller, error);
    return;
  }
  const {
    stream,
  } = controller[kState];
  if (!writableStreamCloseQueuedOrInFlight(stream) &&
      stream[kState].state === 'writable') {
    writableStreamUpdateBackpressure(
      stream,
      writableStreamDefaultControllerGetBackpressure(controller));
  }
  writableStreamDefaultControllerAdvanceQueueIfNeeded(controller);
}

function writableStreamDefaultControllerProcessWrite(controller, chunk) {
  const {
    stream,
    writeAlgorithm,
  } = controller[kState];
  writableStreamMarkFirstWriteRequestInFlight(stream);

  PromisePrototypeThen(
    writeAlgorithm(chunk, controller),
    () => {
      writableStreamFinishInFlightWrite(stream);
      const {
        state,
      } = stream[kState];
      assert(state === 'writable' || state === 'erroring');
      dequeueValue(controller);
      if (!writableStreamCloseQueuedOrInFlight(stream) &&
          state === 'writable') {
        writableStreamUpdateBackpressure(
          stream,
          writableStreamDefaultControllerGetBackpressure(controller));
      }
      writableStreamDefaultControllerAdvanceQueueIfNeeded(controller);
    },
    (error) => {
      if (stream[kState].state === 'writable')
        writableStreamDefaultControllerClearAlgorithms(controller);
      writableStreamFinishInFlightWriteWithError(stream, error);
    });

}

function writableStreamDefaultControllerProcessClose(controller) {
  const {
    closeAlgorithm,
    queue,
    stream,
  } = controller[kState];
  writableStreamMarkCloseRequestInFlight(stream);
  dequeueValue(controller);
  assert(!queue.length);
  const sinkClosePromise = closeAlgorithm();
  writableStreamDefaultControllerClearAlgorithms(controller);
  PromisePrototypeThen(
    sinkClosePromise,
    () => writableStreamFinishInFlightClose(stream),
    (error) => writableStreamFinishInFlightCloseWithError(stream, error));
}

function writableStreamDefaultControllerGetDesiredSize(controller) {
  const {
    highWaterMark,
    queueTotalSize,
  } = controller[kState];
  return highWaterMark - queueTotalSize;
}

function writableStreamDefaultControllerGetChunkSize(controller, chunk) {
  const {
    stream,
    sizeAlgorithm,
  } = controller[kState];
  if (sizeAlgorithm === undefined) {
    assert(stream[kState].state === 'closed' ||
           stream[kState].state === 'errored' ||
           stream[kState].state === 'erroring');
    return 1;
  }

  try {
    return FunctionPrototypeCall(
      sizeAlgorithm,
      undefined,
      chunk);
  } catch (error) {
    writableStreamDefaultControllerErrorIfNeeded(controller, error);
    return 1;
  }
}

function writableStreamDefaultControllerErrorIfNeeded(controller, error) {
  const {
    stream,
  } = controller[kState];
  if (stream[kState].state === 'writable')
    writableStreamDefaultControllerError(controller, error);
}

function writableStreamDefaultControllerError(controller, error) {
  const {
    stream,
  } = controller[kState];
  assert(stream[kState].state === 'writable');
  writableStreamDefaultControllerClearAlgorithms(controller);
  writableStreamStartErroring(stream, error);
}

function writableStreamDefaultControllerClose(controller) {
  enqueueValueWithSize(controller, kCloseSentinel, 0);
  writableStreamDefaultControllerAdvanceQueueIfNeeded(controller);
}

function writableStreamDefaultControllerClearAlgorithms(controller) {
  controller[kState].writeAlgorithm = undefined;
  controller[kState].closeAlgorithm = undefined;
  controller[kState].abortAlgorithm = undefined;
  controller[kState].sizeAlgorithm = undefined;
}

function writableStreamDefaultControllerGetBackpressure(controller) {
  return writableStreamDefaultControllerGetDesiredSize(controller) <= 0;
}

function writableStreamDefaultControllerAdvanceQueueIfNeeded(controller) {
  const {
    queue,
    started,
    stream,
  } = controller[kState];
  if (!started || stream[kState].inFlightWriteRequest.promise !== undefined)
    return;

  if (stream[kState].state === 'erroring') {
    writableStreamFinishErroring(stream);
    return;
  }

  if (!queue.length)
    return;

  const value = peekQueueValue(controller);
  if (value === kCloseSentinel)
    writableStreamDefaultControllerProcessClose(controller);
  else
    writableStreamDefaultControllerProcessWrite(controller, value);
}

function setupWritableStreamDefaultControllerFromSink(
  stream,
  sink,
  highWaterMark,
  sizeAlgorithm) {
  const controller = new WritableStreamDefaultController(kSkipThrow);
  const start = sink?.start;
  const write = sink?.write;
  const close = sink?.close;
  const abort = sink?.abort;
  const startAlgorithm = start ?
    FunctionPrototypeBind(start, sink, controller) :
    nonOpStart;
  const writeAlgorithm = write ?
    createPromiseCallback('sink.write', write, sink) :
    nonOpWrite;
  const closeAlgorithm = close ?
    createPromiseCallback('sink.close', close, sink) :
    nonOpCancel;
  const abortAlgorithm = abort ?
    createPromiseCallback('sink.abort', abort, sink) :
    nonOpCancel;
  setupWritableStreamDefaultController(
    stream,
    controller,
    startAlgorithm,
    writeAlgorithm,
    closeAlgorithm,
    abortAlgorithm,
    highWaterMark,
    sizeAlgorithm);
}

function setupWritableStreamDefaultController(
  stream,
  controller,
  startAlgorithm,
  writeAlgorithm,
  closeAlgorithm,
  abortAlgorithm,
  highWaterMark,
  sizeAlgorithm) {
  assert(isWritableStream(stream));
  assert(stream[kState].controller === undefined);
  controller[kState] = {
    abortAlgorithm,
    closeAlgorithm,
    highWaterMark,
    queue: [],
    queueTotalSize: 0,
    abortController: new AbortController(),
    sizeAlgorithm,
    started: false,
    stream,
    writeAlgorithm,
  };
  stream[kState].controller = controller;
  stream[kControllerErrorFunction] = FunctionPrototypeBind(controller.error, controller);

  writableStreamUpdateBackpressure(
    stream,
    writableStreamDefaultControllerGetBackpressure(controller));

  const startResult = startAlgorithm();

  PromisePrototypeThen(
    new Promise((r) => r(startResult)),
    () => {
      assert(stream[kState].state === 'writable' ||
             stream[kState].state === 'erroring');
      controller[kState].started = true;
      writableStreamDefaultControllerAdvanceQueueIfNeeded(controller);
    },
    (error) => {
      assert(stream[kState].state === 'writable' ||
             stream[kState].state === 'erroring');
      controller[kState].started = true;
      writableStreamDealWithRejection(stream, error);
    });
}

module.exports = {
  WritableStream,
  WritableStreamDefaultWriter,
  WritableStreamDefaultController,
  TransferredWritableStream,

  // Exported Brand Checks
  isWritableStream,
  isWritableStreamDefaultController,
  isWritableStreamDefaultWriter,

  isWritableStreamLocked,
  setupWritableStreamDefaultWriter,
  writableStreamAbort,
  writableStreamClose,
  writableStreamUpdateBackpressure,
  writableStreamStartErroring,
  writableStreamRejectCloseAndClosedPromiseIfNeeded,
  writableStreamMarkFirstWriteRequestInFlight,
  writableStreamMarkCloseRequestInFlight,
  writableStreamHasOperationMarkedInFlight,
  writableStreamFinishInFlightWriteWithError,
  writableStreamFinishInFlightWrite,
  writableStreamFinishInFlightCloseWithError,
  writableStreamFinishInFlightClose,
  writableStreamFinishErroring,
  writableStreamDealWithRejection,
  writableStreamCloseQueuedOrInFlight,
  writableStreamAddWriteRequest,
  writableStreamDefaultWriterWrite,
  writableStreamDefaultWriterRelease,
  writableStreamDefaultWriterGetDesiredSize,
  writableStreamDefaultWriterEnsureReadyPromiseRejected,
  writableStreamDefaultWriterEnsureClosedPromiseRejected,
  writableStreamDefaultWriterCloseWithErrorPropagation,
  writableStreamDefaultWriterClose,
  writableStreamDefaultWriterAbort,
  writableStreamDefaultControllerWrite,
  writableStreamDefaultControllerProcessWrite,
  writableStreamDefaultControllerProcessClose,
  writableStreamDefaultControllerGetDesiredSize,
  writableStreamDefaultControllerGetChunkSize,
  writableStreamDefaultControllerErrorIfNeeded,
  writableStreamDefaultControllerError,
  writableStreamDefaultControllerClose,
  writableStreamDefaultControllerClearAlgorithms,
  writableStreamDefaultControllerGetBackpressure,
  writableStreamDefaultControllerAdvanceQueueIfNeeded,
  setupWritableStreamDefaultControllerFromSink,
  setupWritableStreamDefaultController,
  createWritableStream,
};
