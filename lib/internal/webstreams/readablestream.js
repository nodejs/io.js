'use strict';

const {
  ArrayBuffer,
  ArrayBufferPrototypeGetByteLength,
  ArrayBufferPrototypeGetDetached,
  ArrayBufferPrototypeSlice,
  ArrayBufferPrototypeTransfer,
  ArrayPrototypePush,
  ArrayPrototypeShift,
  DataView,
  FunctionPrototypeBind,
  FunctionPrototypeCall,
  MathMin,
  NumberIsInteger,
  ObjectDefineProperties,
  ObjectSetPrototypeOf,
  Promise,
  PromisePrototypeThen,
  PromiseReject,
  PromiseResolve,
  PromiseWithResolvers,
  SafePromiseAll,
  Symbol,
  SymbolAsyncIterator,
  SymbolDispose,
  SymbolToStringTag,
  TypedArrayPrototypeGetLength,
  Uint8Array,
} = primordials;

const {
  AbortError,
  codes: {
    ERR_ILLEGAL_CONSTRUCTOR,
    ERR_INVALID_ARG_TYPE,
    ERR_INVALID_ARG_VALUE,
    ERR_INVALID_STATE,
    ERR_INVALID_THIS,
    ERR_OUT_OF_RANGE,
  },
} = require('internal/errors');

const {
  DOMException,
} = internalBinding('messaging');

const {
  isArrayBufferView,
  isDataView,
} = require('internal/util/types');

const {
  customInspectSymbol: kInspect,
  kEmptyObject,
  kEnumerableProperty,
  SideEffectFreeRegExpPrototypeSymbolReplace,
} = require('internal/util');

const {
  validateAbortSignal,
  validateBuffer,
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
  structuredClone,
} = require('internal/worker/js_transferable');

const {
  queueMicrotask,
} = require('internal/process/task_queues');

const {
  kIsDisturbed,
  kIsErrored,
  kIsReadable,
  kIsClosedPromise,
  kControllerErrorFunction,
} = require('internal/streams/utils');

const {
  ArrayBufferViewGetBuffer,
  ArrayBufferViewGetByteLength,
  ArrayBufferViewGetByteOffset,
  AsyncIterator,
  canCopyArrayBuffer,
  cloneAsUint8Array,
  copyArrayBuffer,
  createPromiseCallback,
  customInspect,
  dequeueValue,
  enqueueValueWithSize,
  extractHighWaterMark,
  extractSizeAlgorithm,
  lazyTransfer,
  isBrandCheck,
  resetQueue,
  setPromiseHandled,
  nonOpCancel,
  nonOpPull,
  nonOpStart,
  getIterator,
  iteratorNext,
  kType,
  kState,
} = require('internal/webstreams/util');

const {
  WritableStreamDefaultWriter,

  isWritableStream,
  isWritableStreamLocked,
  isWritableStreamDefaultController,
  isWritableStreamDefaultWriter,

  writableStreamAbort,
  writableStreamCloseQueuedOrInFlight,
  writableStreamDefaultWriterCloseWithErrorPropagation,
  writableStreamDefaultWriterRelease,
  writableStreamDefaultWriterWrite,
} = require('internal/webstreams/writablestream');

const { Buffer } = require('buffer');

const assert = require('internal/assert');

const kCancel = Symbol('kCancel');
const kClose = Symbol('kClose');
const kChunk = Symbol('kChunk');
const kError = Symbol('kError');
const kPull = Symbol('kPull');
const kRelease = Symbol('kRelease');
const kSkipThrow = Symbol('kSkipThrow');

let releasedError;
let releasingError;
let addAbortListener;

const userModuleRegExp = /^ {4}at (?:[^/\\(]+ \()(?!node:(.+):\d+:\d+\)$).*/gm;

function lazyReadableReleasedError() {
  if (releasedError) {
    return releasedError;
  }

  releasedError = new ERR_INVALID_STATE.TypeError('Reader released');
  // Avoid V8 leak and remove userland stackstrace
  releasedError.stack = SideEffectFreeRegExpPrototypeSymbolReplace(userModuleRegExp, releasedError.stack, '');
  return releasedError;
}

function lazyReadableReleasingError() {
  if (releasingError) {
    return releasingError;
  }
  releasingError = new ERR_INVALID_STATE.TypeError('Releasing reader');
  // Avoid V8 leak and remove userland stackstrace
  releasingError.stack = SideEffectFreeRegExpPrototypeSymbolReplace(userModuleRegExp, releasingError.stack, '');
  return releasingError;
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
 * @typedef {import('./queuingstrategies').QueuingStrategy} QueuingStrategy
 * @typedef {import('./queuingstrategies').QueuingStrategySize
 * } QueuingStrategySize
 * @typedef {import('./writablestream').WritableStream} WritableStream
 */

/**
 * @typedef {ReadableStreamDefaultController | ReadableByteStreamController
 * } ReadableStreamController
 */

/**
 * @typedef {ReadableStreamDefaultReader | ReadableStreamBYOBReader
 * } ReadableStreamReader
 */

/**
 * @callback UnderlyingSourceStartCallback
 * @param {ReadableStreamController} controller
 * @returns { any | Promise<void> }
 */

/**
 * @callback UnderlyingSourcePullCallback
 * @param {ReadableStreamController} controller
 * @returns { Promise<void> }
 */

/**
 * @callback UnderlyingSourceCancelCallback
 * @param {any} reason
 * @returns { Promise<void> }
 */

/**
 * @typedef {{
 *   readable: ReadableStream,
 *   writable: WritableStream,
 * }} ReadableWritablePair
 */

/**
 * @typedef {{
 *   preventClose? : boolean,
 *   preventAbort? : boolean,
 *   preventCancel? : boolean,
 *   signal? : AbortSignal,
 * }} StreamPipeOptions
 */

/**
 * @typedef {{
 *   start? : UnderlyingSourceStartCallback,
 *   pull? : UnderlyingSourcePullCallback,
 *   cancel? : UnderlyingSourceCancelCallback,
 *   type? : "bytes",
 *   autoAllocateChunkSize? : number
 * }} UnderlyingSource
 */

class ReadableStream {
  [kType] = 'ReadableStream';

  /**
   * @param {UnderlyingSource} [source]
   * @param {QueuingStrategy} [strategy]
   */
  constructor(source = kEmptyObject, strategy = kEmptyObject) {
    markTransferMode(this, false, true);
    validateObject(source, 'source', kValidateObjectAllowObjects);
    validateObject(strategy, 'strategy', kValidateObjectAllowObjectsAndNull);
    this[kState] = createReadableStreamState();

    this[kIsClosedPromise] = PromiseWithResolvers();
    this[kControllerErrorFunction] = () => {};

    // The spec requires handling of the strategy first
    // here. Specifically, if getting the size and
    // highWaterMark from the strategy fail, that has
    // to trigger a throw before getting the details
    // from the source. So be sure to keep these in
    // this order.
    const size = strategy?.size;
    const highWaterMark = strategy?.highWaterMark;
    const type = source.type;

    if (`${type}` === 'bytes') {
      if (size !== undefined)
        throw new ERR_INVALID_ARG_VALUE.RangeError('strategy.size', size);
      setupReadableByteStreamControllerFromSource(
        this,
        source,
        extractHighWaterMark(highWaterMark, 0));
    } else {
      if (type !== undefined)
        throw new ERR_INVALID_ARG_VALUE('source.type', type);
      setupReadableStreamDefaultControllerFromSource(
        this,
        source,
        extractHighWaterMark(highWaterMark, 1),
        extractSizeAlgorithm(size));
    }
  }

  get [kIsDisturbed]() {
    return this[kState].disturbed;
  }

  get [kIsErrored]() {
    return this[kState].state === 'errored';
  }

  get [kIsReadable]() {
    return this[kState].state === 'readable';
  }

  /**
   * @readonly
   * @type {boolean}
   */
  get locked() {
    if (!isReadableStream(this))
      throw new ERR_INVALID_THIS('ReadableStream');
    return isReadableStreamLocked(this);
  }

  static from(iterable) {
    return readableStreamFromIterable(iterable);
  }

  /**
   * @param {any} [reason]
   * @returns { Promise<void> }
   */
  cancel(reason = undefined) {
    if (!isReadableStream(this))
      return PromiseReject(new ERR_INVALID_THIS('ReadableStream'));
    if (isReadableStreamLocked(this)) {
      return PromiseReject(
        new ERR_INVALID_STATE.TypeError('ReadableStream is locked'));
    }
    return readableStreamCancel(this, reason);
  }

  /**
   * @param {{
   *   mode? : "byob"
   * }} [options]
   * @returns {ReadableStreamReader}
   */
  getReader(options = kEmptyObject) {
    if (!isReadableStream(this))
      throw new ERR_INVALID_THIS('ReadableStream');
    validateObject(options, 'options', kValidateObjectAllowObjectsAndNull);
    const mode = options?.mode;

    if (mode === undefined)
      // eslint-disable-next-line no-use-before-define
      return new ReadableStreamDefaultReader(this);

    if (`${mode}` !== 'byob')
      throw new ERR_INVALID_ARG_VALUE('options.mode', mode);
    // eslint-disable-next-line no-use-before-define
    return new ReadableStreamBYOBReader(this);
  }

  /**
   * @param {ReadableWritablePair} transform
   * @param {StreamPipeOptions} [options]
   * @returns {ReadableStream}
   */
  pipeThrough(transform, options = kEmptyObject) {
    if (!isReadableStream(this))
      throw new ERR_INVALID_THIS('ReadableStream');
    const readable = transform?.readable;
    if (!isReadableStream(readable)) {
      throw new ERR_INVALID_ARG_TYPE(
        'transform.readable',
        'ReadableStream',
        readable);
    }
    const writable = transform?.writable;
    if (!isWritableStream(writable)) {
      throw new ERR_INVALID_ARG_TYPE(
        'transform.writable',
        'WritableStream',
        writable);
    }

    // The web platform tests require that these be handled one at a
    // time and in a specific order. options can be null or undefined.
    validateObject(options, 'options', kValidateObjectAllowObjectsAndNull);
    const preventAbort = options?.preventAbort;
    const preventCancel = options?.preventCancel;
    const preventClose = options?.preventClose;
    const signal = options?.signal;

    if (signal !== undefined) {
      validateAbortSignal(signal, 'options.signal');
    }

    if (isReadableStreamLocked(this))
      throw new ERR_INVALID_STATE.TypeError('The ReadableStream is locked');
    if (isWritableStreamLocked(writable))
      throw new ERR_INVALID_STATE.TypeError('The WritableStream is locked');

    const promise = readableStreamPipeTo(
      this,
      writable,
      !!preventClose,
      !!preventAbort,
      !!preventCancel,
      signal);
    setPromiseHandled(promise);

    return readable;
  }

  /**
   * @param {WritableStream} destination
   * @param {StreamPipeOptions} [options]
   * @returns {Promise<void>}
   */
  pipeTo(destination, options = kEmptyObject) {
    try {
      if (!isReadableStream(this))
        throw new ERR_INVALID_THIS('ReadableStream');
      if (!isWritableStream(destination)) {
        throw new ERR_INVALID_ARG_TYPE(
          'transform.writable',
          'WritableStream',
          destination);
      }

      validateObject(options, 'options', kValidateObjectAllowObjectsAndNull);
      const preventAbort = options?.preventAbort;
      const preventCancel = options?.preventCancel;
      const preventClose = options?.preventClose;
      const signal = options?.signal;

      if (signal !== undefined) {
        validateAbortSignal(signal, 'options.signal');
      }

      if (isReadableStreamLocked(this))
        throw new ERR_INVALID_STATE.TypeError('The ReadableStream is locked');
      if (isWritableStreamLocked(destination))
        throw new ERR_INVALID_STATE.TypeError('The WritableStream is locked');

      return readableStreamPipeTo(
        this,
        destination,
        !!preventClose,
        !!preventAbort,
        !!preventCancel,
        signal);
    } catch (error) {
      return PromiseReject(error);
    }
  }

  /**
   * @returns {ReadableStream[]}
   */
  tee() {
    if (!isReadableStream(this))
      throw new ERR_INVALID_THIS('ReadableStream');
    return readableStreamTee(this, false);
  }

  /**
   * @param {{
   *   preventCancel? : boolean,
   * }} [options]
   * @returns {AsyncIterable}
   */
  values(options = kEmptyObject) {
    if (!isReadableStream(this))
      throw new ERR_INVALID_THIS('ReadableStream');
    validateObject(options, 'options', kValidateObjectAllowObjectsAndNull);
    const preventCancel = !!(options?.preventCancel);

    // eslint-disable-next-line no-use-before-define
    const reader = new ReadableStreamDefaultReader(this);

    // No __proto__ here to avoid the performance hit.
    const state = {
      done: false,
      current: undefined,
    };
    let started = false;

    // The nextSteps function is not an async function in order
    // to make it more efficient. Because nextSteps explicitly
    // creates a Promise and returns it in the common case,
    // making it an async function just causes two additional
    // unnecessary Promise allocations to occur, which just add
    // cost.
    function nextSteps() {
      if (state.done)
        return PromiseResolve({ done: true, value: undefined });

      if (reader[kState].stream === undefined) {
        return PromiseReject(
          new ERR_INVALID_STATE.TypeError(
            'The reader is not bound to a ReadableStream'));
      }
      const promise = PromiseWithResolvers();

      // eslint-disable-next-line no-use-before-define
      readableStreamDefaultReaderRead(reader, new ReadableStreamAsyncIteratorReadRequest(reader, state, promise));
      return promise.promise;
    }

    async function returnSteps(value) {
      if (state.done)
        return { done: true, value }; // eslint-disable-line node-core/avoid-prototype-pollution
      state.done = true;

      if (reader[kState].stream === undefined) {
        throw new ERR_INVALID_STATE.TypeError(
          'The reader is not bound to a ReadableStream');
      }
      assert(!reader[kState].readRequests.length);
      if (!preventCancel) {
        const result = readableStreamReaderGenericCancel(reader, value);
        readableStreamReaderGenericRelease(reader);
        await result;
        return { done: true, value }; // eslint-disable-line node-core/avoid-prototype-pollution
      }

      readableStreamReaderGenericRelease(reader);
      return { done: true, value }; // eslint-disable-line node-core/avoid-prototype-pollution
    }

    // TODO(@jasnell): Explore whether an async generator
    // can be used here instead of a custom iterator object.
    return ObjectSetPrototypeOf({
      // Changing either of these functions (next or return)
      // to async functions causes a failure in the streams
      // Web Platform Tests that check for use of a modified
      // Promise.prototype.then. Since the await keyword
      // uses Promise.prototype.then, it is open to prototype
      // pollution, which causes the test to fail. The other
      // await uses here do not trigger that failure because
      // the test that fails does not trigger those code paths.
      next() {
        // If this is the first read, delay by one microtask
        // to ensure that the controller has had an opportunity
        // to properly start and perform the initial pull.
        // TODO(@jasnell): The spec doesn't call this out so
        // need to investigate if it's a bug in our impl or
        // the spec.
        if (!started) {
          state.current = PromiseResolve();
          started = true;
        }
        state.current = state.current !== undefined ?
          PromisePrototypeThen(state.current, nextSteps, nextSteps) :
          nextSteps();
        return state.current;
      },

      return(error) {
        started = true;
        state.current = state.current !== undefined ?
          PromisePrototypeThen(
            state.current,
            () => returnSteps(error),
            () => returnSteps(error)) :
          returnSteps(error);
        return state.current;
      },

      [SymbolAsyncIterator]() { return this; },
    }, AsyncIterator);
  }

  [kInspect](depth, options) {
    return customInspect(depth, options, this[kType], {
      locked: this.locked,
      state: this[kState].state,
      supportsBYOB:
        // eslint-disable-next-line no-use-before-define
        this[kState].controller instanceof ReadableByteStreamController,
    });
  }

  [kTransfer]() {
    if (!isReadableStream(this))
      throw new ERR_INVALID_THIS('ReadableStream');
    if (this.locked) {
      this[kState].transfer.port1?.close();
      this[kState].transfer.port1 = undefined;
      this[kState].transfer.port2 = undefined;
      throw new DOMException(
        'Cannot transfer a locked ReadableStream',
        'DataCloneError');
    }

    const {
      writable,
      promise,
    } = lazyTransfer().newCrossRealmWritableSink(
      this,
      this[kState].transfer.port1);

    this[kState].transfer.writable = writable;
    this[kState].transfer.promise = promise;

    return {
      data: { port: this[kState].transfer.port2 },
      deserializeInfo:
        'internal/webstreams/readablestream:TransferredReadableStream',
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
    setupReadableStreamDefaultControllerFromSource(
      this,
      // The MessagePort is set to be referenced when reading.
      // After two MessagePorts are closed, there is a problem with
      // lingering promise not being properly resolved.
      // https://github.com/nodejs/node/issues/51486
      new transfer.CrossRealmTransformReadableSource(port, true),
      0, () => 1);
  }
}

ObjectDefineProperties(ReadableStream.prototype, {
  [SymbolAsyncIterator]: {
    __proto__: null,
    configurable: true,
    enumerable: false,
    writable: true,
    value: ReadableStream.prototype.values,
  },
  locked: kEnumerableProperty,
  cancel: kEnumerableProperty,
  getReader: kEnumerableProperty,
  pipeThrough: kEnumerableProperty,
  pipeTo: kEnumerableProperty,
  tee: kEnumerableProperty,
  values: kEnumerableProperty,
  [SymbolToStringTag]: getNonWritablePropertyDescriptor(ReadableStream.name),
});
ObjectDefineProperties(ReadableStream, {
  from: kEnumerableProperty,
});

function InternalTransferredReadableStream() {
  ObjectSetPrototypeOf(this, ReadableStream.prototype);
  markTransferMode(this, false, true);
  this[kType] = 'ReadableStream';
  this[kState] = createReadableStreamState();

  this[kIsClosedPromise] = PromiseWithResolvers();
}

ObjectSetPrototypeOf(InternalTransferredReadableStream.prototype, ReadableStream.prototype);
ObjectSetPrototypeOf(InternalTransferredReadableStream, ReadableStream);

function TransferredReadableStream() {
  const stream = new InternalTransferredReadableStream();

  stream.constructor = ReadableStream;

  return stream;
}

TransferredReadableStream.prototype[kDeserialize] = () => {};

class ReadableStreamBYOBRequest {
  [kType] = 'ReadableStreamBYOBRequest';

  constructor(skipThrowSymbol = undefined) {
    if (skipThrowSymbol !== kSkipThrow) {
      throw new ERR_ILLEGAL_CONSTRUCTOR();
    }
  }

  /**
   * @readonly
   * @type {ArrayBufferView}
   */
  get view() {
    if (!isReadableStreamBYOBRequest(this))
      throw new ERR_INVALID_THIS('ReadableStreamBYOBRequest');
    return this[kState].view;
  }

  /**
   * @param {number} bytesWritten
   */
  respond(bytesWritten) {
    if (!isReadableStreamBYOBRequest(this))
      throw new ERR_INVALID_THIS('ReadableStreamBYOBRequest');
    const {
      view,
      controller,
    } = this[kState];
    if (controller === undefined) {
      throw new ERR_INVALID_STATE.TypeError(
        'This BYOB request has been invalidated');
    }

    const viewByteLength = ArrayBufferViewGetByteLength(view);
    const viewBuffer = ArrayBufferViewGetBuffer(view);
    const viewBufferByteLength = ArrayBufferPrototypeGetByteLength(viewBuffer);

    if (ArrayBufferPrototypeGetDetached(viewBuffer)) {
      throw new ERR_INVALID_STATE.TypeError('Viewed ArrayBuffer is detached');
    }

    assert(viewByteLength > 0);
    assert(viewBufferByteLength > 0);

    readableByteStreamControllerRespond(controller, bytesWritten);
  }

  /**
   * @param {ArrayBufferView} view
   */
  respondWithNewView(view) {
    if (!isReadableStreamBYOBRequest(this))
      throw new ERR_INVALID_THIS('ReadableStreamBYOBRequest');
    const {
      controller,
    } = this[kState];

    if (controller === undefined) {
      throw new ERR_INVALID_STATE.TypeError(
        'This BYOB request has been invalidated');
    }

    validateBuffer(view, 'view');

    if (ArrayBufferPrototypeGetDetached(view.buffer)) {
      throw new ERR_INVALID_STATE.TypeError('Viewed ArrayBuffer is detached');
    }

    readableByteStreamControllerRespondWithNewView(controller, view);
  }

  [kInspect](depth, options) {
    return customInspect(depth, options, this[kType], {
      view: this.view,
      controller: this[kState].controller,
    });
  }
}

ObjectDefineProperties(ReadableStreamBYOBRequest.prototype, {
  view: kEnumerableProperty,
  respond: kEnumerableProperty,
  respondWithNewView: kEnumerableProperty,
  [SymbolToStringTag]: getNonWritablePropertyDescriptor(ReadableStreamBYOBRequest.name),
});

function createReadableStreamBYOBRequest(controller, view) {
  const stream = new ReadableStreamBYOBRequest(kSkipThrow);

  stream[kState] = {
    controller,
    view,
  };

  return stream;
}

class ReadableStreamAsyncIteratorReadRequest {
  constructor(reader, state, promise) {
    this.reader = reader;
    this.state = state;
    this.promise = promise;
  }

  [kChunk](chunk) {
    this.state.current = undefined;
    this.promise.resolve({ value: chunk, done: false });
  }

  [kClose]() {
    this.state.current = undefined;
    this.state.done = true;
    readableStreamReaderGenericRelease(this.reader);
    this.promise.resolve({ done: true, value: undefined });
  }

  [kError](error) {
    this.state.current = undefined;
    this.state.done = true;
    readableStreamReaderGenericRelease(this.reader);
    this.promise.reject(error);
  }
}

class DefaultReadRequest {
  constructor() {
    this[kState] = PromiseWithResolvers();
  }

  [kChunk](value) {
    this[kState].resolve?.({ value, done: false });
  }

  [kClose]() {
    this[kState].resolve?.({ value: undefined, done: true });
  }

  [kError](error) {
    this[kState].reject?.(error);
  }

  get promise() { return this[kState].promise; }
}

class ReadIntoRequest {
  constructor() {
    this[kState] = PromiseWithResolvers();
  }

  [kChunk](value) {
    this[kState].resolve?.({ value, done: false });
  }

  [kClose](value) {
    this[kState].resolve?.({ value, done: true });
  }

  [kError](error) {
    this[kState].reject?.(error);
  }

  get promise() { return this[kState].promise; }
}

class ReadableStreamDefaultReader {
  [kType] = 'ReadableStreamDefaultReader';

  /**
   * @param {ReadableStream} stream
   */
  constructor(stream) {
    if (!isReadableStream(stream))
      throw new ERR_INVALID_ARG_TYPE('stream', 'ReadableStream', stream);
    this[kState] = {
      readRequests: [],
      stream: undefined,
      close: {
        promise: undefined,
        resolve: undefined,
        reject: undefined,
      },
    };
    setupReadableStreamDefaultReader(this, stream);
  }

  /**
   * @returns {Promise<{
   *   value : any,
   *   done : boolean
   * }>}
   */
  read() {
    if (!isReadableStreamDefaultReader(this))
      return PromiseReject(new ERR_INVALID_THIS('ReadableStreamDefaultReader'));
    if (this[kState].stream === undefined) {
      return PromiseReject(
        new ERR_INVALID_STATE.TypeError(
          'The reader is not attached to a stream'));
    }
    const readRequest = new DefaultReadRequest();
    readableStreamDefaultReaderRead(this, readRequest);
    return readRequest.promise;
  }

  releaseLock() {
    if (!isReadableStreamDefaultReader(this))
      throw new ERR_INVALID_THIS('ReadableStreamDefaultReader');
    if (this[kState].stream === undefined)
      return;
    readableStreamDefaultReaderRelease(this);
  }

  /**
   * @readonly
   * @type {Promise<void>}
   */
  get closed() {
    if (!isReadableStreamDefaultReader(this))
      return PromiseReject(new ERR_INVALID_THIS('ReadableStreamDefaultReader'));
    return this[kState].close.promise;
  }

  /**
   * @param {any} [reason]
   * @returns {Promise<void>}
   */
  cancel(reason = undefined) {
    if (!isReadableStreamDefaultReader(this))
      return PromiseReject(new ERR_INVALID_THIS('ReadableStreamDefaultReader'));
    if (this[kState].stream === undefined) {
      return PromiseReject(new ERR_INVALID_STATE.TypeError(
        'The reader is not attached to a stream'));
    }
    return readableStreamReaderGenericCancel(this, reason);
  }

  [kInspect](depth, options) {
    return customInspect(depth, options, this[kType], {
      stream: this[kState].stream,
      readRequests: this[kState].readRequests.length,
      close: this[kState].close.promise,
    });
  }
}

ObjectDefineProperties(ReadableStreamDefaultReader.prototype, {
  closed: kEnumerableProperty,
  read: kEnumerableProperty,
  releaseLock: kEnumerableProperty,
  cancel: kEnumerableProperty,
  [SymbolToStringTag]: getNonWritablePropertyDescriptor(ReadableStreamDefaultReader.name),
});

class ReadableStreamBYOBReader {
  [kType] = 'ReadableStreamBYOBReader';

  /**
   * @param {ReadableStream} stream
   */
  constructor(stream) {
    if (!isReadableStream(stream))
      throw new ERR_INVALID_ARG_TYPE('stream', 'ReadableStream', stream);
    this[kState] = {
      stream: undefined,
      readIntoRequests: [],
      close: {
        promise: undefined,
        resolve: undefined,
        reject: undefined,
      },
    };
    setupReadableStreamBYOBReader(this, stream);
  }

  /**
   * @param {ArrayBufferView} view
   * @param {{
   *   min? : number
   * }} [options]
   * @returns {Promise<{
   *   value : ArrayBufferView,
   *   done : boolean,
   * }>}
   */
  async read(view, options = kEmptyObject) {
    if (!isReadableStreamBYOBReader(this))
      throw new ERR_INVALID_THIS('ReadableStreamBYOBReader');
    if (!isArrayBufferView(view)) {
      throw new ERR_INVALID_ARG_TYPE(
        'view',
        [
          'Buffer',
          'TypedArray',
          'DataView',
        ],
        view,
      );
    }
    validateObject(options, 'options', kValidateObjectAllowObjectsAndNull);

    const viewByteLength = ArrayBufferViewGetByteLength(view);
    const viewBuffer = ArrayBufferViewGetBuffer(view);
    const viewBufferByteLength = ArrayBufferPrototypeGetByteLength(viewBuffer);

    if (viewByteLength === 0 || viewBufferByteLength === 0) {
      throw new ERR_INVALID_STATE.TypeError(
        'View or Viewed ArrayBuffer is zero-length or detached');
    }

    // Supposed to assert here that the view's buffer is not
    // detached, but there's no API available to use to check that.

    const min = options?.min ?? 1;
    if (typeof min !== 'number')
      throw new ERR_INVALID_ARG_TYPE('options.min', 'number', min);
    if (!NumberIsInteger(min))
      throw new ERR_INVALID_ARG_VALUE('options.min', min, 'must be an integer');
    if (min <= 0)
      throw new ERR_INVALID_ARG_VALUE('options.min', min, 'must be greater than 0');
    if (!isDataView(view)) {
      if (min > TypedArrayPrototypeGetLength(view)) {
        throw new ERR_OUT_OF_RANGE('options.min', '<= view.length', min);
      }
    } else if (min > viewByteLength) {
      throw new ERR_OUT_OF_RANGE('options.min', '<= view.byteLength', min);
    }

    if (this[kState].stream === undefined) {
      throw new ERR_INVALID_STATE.TypeError('The reader is not attached to a stream');
    }
    const readIntoRequest = new ReadIntoRequest();
    readableStreamBYOBReaderRead(this, view, min, readIntoRequest);
    return readIntoRequest.promise;
  }

  releaseLock() {
    if (!isReadableStreamBYOBReader(this))
      throw new ERR_INVALID_THIS('ReadableStreamBYOBReader');
    if (this[kState].stream === undefined)
      return;
    readableStreamBYOBReaderRelease(this);
  }

  /**
   * @readonly
   * @type {Promise<void>}
   */
  get closed() {
    if (!isReadableStreamBYOBReader(this))
      return PromiseReject(new ERR_INVALID_THIS('ReadableStreamBYOBReader'));
    return this[kState].close.promise;
  }

  /**
   * @param {any} [reason]
   * @returns {Promise<void>}
   */
  cancel(reason = undefined) {
    if (!isReadableStreamBYOBReader(this))
      return PromiseReject(new ERR_INVALID_THIS('ReadableStreamBYOBReader'));
    if (this[kState].stream === undefined) {
      return PromiseReject(new ERR_INVALID_STATE.TypeError(
        'The reader is not attached to a stream'));
    }
    return readableStreamReaderGenericCancel(this, reason);
  }

  [kInspect](depth, options) {
    return customInspect(depth, options, this[kType], {
      stream: this[kState].stream,
      readIntoRequests: this[kState].readIntoRequests.length,
      close: this[kState].close.promise,
    });
  }
}

ObjectDefineProperties(ReadableStreamBYOBReader.prototype, {
  closed: kEnumerableProperty,
  read: kEnumerableProperty,
  releaseLock: kEnumerableProperty,
  cancel: kEnumerableProperty,
  [SymbolToStringTag]: getNonWritablePropertyDescriptor(ReadableStreamBYOBReader.name),
});

class ReadableStreamDefaultController {
  [kType] = 'ReadableStreamDefaultController';
  [kState] = {};

  constructor(skipThrowSymbol = undefined) {
    if (skipThrowSymbol !== kSkipThrow) {
      throw new ERR_ILLEGAL_CONSTRUCTOR();
    }
  }

  /**
   * @readonly
   * @type {number}
   */
  get desiredSize() {
    return readableStreamDefaultControllerGetDesiredSize(this);
  }

  close() {
    if (!readableStreamDefaultControllerCanCloseOrEnqueue(this))
      throw new ERR_INVALID_STATE.TypeError('Controller is already closed');
    readableStreamDefaultControllerClose(this);
  }

  /**
   * @param {any} [chunk]
   */
  enqueue(chunk = undefined) {
    if (!readableStreamDefaultControllerCanCloseOrEnqueue(this))
      throw new ERR_INVALID_STATE.TypeError('Controller is already closed');
    readableStreamDefaultControllerEnqueue(this, chunk);
  }

  /**
   * @param {any} [error]
   */
  error(error = undefined) {
    readableStreamDefaultControllerError(this, error);
  }

  [kCancel](reason) {
    return readableStreamDefaultControllerCancelSteps(this, reason);
  }

  [kPull](readRequest) {
    readableStreamDefaultControllerPullSteps(this, readRequest);
  }

  [kRelease]() {}

  [kInspect](depth, options) {
    return customInspect(depth, options, this[kType], { });
  }
}

ObjectDefineProperties(ReadableStreamDefaultController.prototype, {
  desiredSize: kEnumerableProperty,
  close: kEnumerableProperty,
  enqueue: kEnumerableProperty,
  error: kEnumerableProperty,
  [SymbolToStringTag]: getNonWritablePropertyDescriptor(ReadableStreamDefaultController.name),
});

class ReadableByteStreamController {
  [kType] = 'ReadableByteStreamController';
  [kState] = {};

  constructor(skipThrowSymbol = undefined) {
    if (skipThrowSymbol !== kSkipThrow) {
      throw new ERR_ILLEGAL_CONSTRUCTOR();
    }
  }

  /**
   * @readonly
   * @type {ReadableStreamBYOBRequest}
   */
  get byobRequest() {
    if (!isReadableByteStreamController(this))
      throw new ERR_INVALID_THIS('ReadableByteStreamController');
    if (this[kState].byobRequest === null &&
        this[kState].pendingPullIntos.length) {
      const {
        buffer,
        byteOffset,
        bytesFilled,
        byteLength,
      } = this[kState].pendingPullIntos[0];
      const view =
        new Uint8Array(
          buffer,
          byteOffset + bytesFilled,
          byteLength - bytesFilled);
      this[kState].byobRequest = createReadableStreamBYOBRequest(this, view);
    }
    return this[kState].byobRequest;
  }

  /**
   * @readonly
   * @type {number}
   */
  get desiredSize() {
    if (!isReadableByteStreamController(this))
      throw new ERR_INVALID_THIS('ReadableByteStreamController');
    return readableByteStreamControllerGetDesiredSize(this);
  }

  close() {
    if (!isReadableByteStreamController(this))
      throw new ERR_INVALID_THIS('ReadableByteStreamController');
    if (this[kState].closeRequested)
      throw new ERR_INVALID_STATE.TypeError('Controller is already closed');
    if (this[kState].stream[kState].state !== 'readable')
      throw new ERR_INVALID_STATE.TypeError('ReadableStream is already closed');
    readableByteStreamControllerClose(this);
  }

  /**
   * @param {ArrayBufferView} chunk
   */
  enqueue(chunk) {
    if (!isReadableByteStreamController(this))
      throw new ERR_INVALID_THIS('ReadableByteStreamController');
    validateBuffer(chunk);
    const chunkByteLength = ArrayBufferViewGetByteLength(chunk);
    const chunkBuffer = ArrayBufferViewGetBuffer(chunk);
    const chunkBufferByteLength = ArrayBufferPrototypeGetByteLength(chunkBuffer);
    if (chunkByteLength === 0 || chunkBufferByteLength === 0) {
      throw new ERR_INVALID_STATE.TypeError(
        'chunk ArrayBuffer is zero-length or detached');
    }
    if (this[kState].closeRequested)
      throw new ERR_INVALID_STATE.TypeError('Controller is already closed');
    if (this[kState].stream[kState].state !== 'readable')
      throw new ERR_INVALID_STATE.TypeError('ReadableStream is already closed');
    readableByteStreamControllerEnqueue(this, chunk);
  }

  /**
   * @param {any} [error]
   */
  error(error = undefined) {
    if (!isReadableByteStreamController(this))
      throw new ERR_INVALID_THIS('ReadableByteStreamController');
    readableByteStreamControllerError(this, error);
  }

  [kCancel](reason) {
    return readableByteStreamControllerCancelSteps(this, reason);
  }

  [kPull](readRequest) {
    readableByteStreamControllerPullSteps(this, readRequest);
  }

  [kRelease]() {
    const {
      pendingPullIntos,
    } = this[kState];
    if (pendingPullIntos.length > 0) {
      const firstPendingPullInto = pendingPullIntos[0];
      firstPendingPullInto.type = 'none';
      this[kState].pendingPullIntos = [firstPendingPullInto];
    }
  }

  [kInspect](depth, options) {
    return customInspect(depth, options, this[kType], { });
  }
}

ObjectDefineProperties(ReadableByteStreamController.prototype, {
  byobRequest: kEnumerableProperty,
  desiredSize: kEnumerableProperty,
  close: kEnumerableProperty,
  enqueue: kEnumerableProperty,
  error: kEnumerableProperty,
  [SymbolToStringTag]: getNonWritablePropertyDescriptor(ReadableByteStreamController.name),
});

function InternalReadableStream(start, pull, cancel, highWaterMark, size) {
  ObjectSetPrototypeOf(this, ReadableStream.prototype);
  markTransferMode(this, false, true);
  this[kType] = 'ReadableStream';
  this[kState] = createReadableStreamState();
  this[kIsClosedPromise] = PromiseWithResolvers();
  const controller = new ReadableStreamDefaultController(kSkipThrow);
  setupReadableStreamDefaultController(
    this,
    controller,
    start,
    pull,
    cancel,
    highWaterMark,
    size);
}

ObjectSetPrototypeOf(InternalReadableStream.prototype, ReadableStream.prototype);
ObjectSetPrototypeOf(InternalReadableStream, ReadableStream);

function createReadableStream(start, pull, cancel, highWaterMark = 1, size = () => 1) {
  const stream = new InternalReadableStream(start, pull, cancel, highWaterMark, size);

  // For spec compliance the InternalReadableStream must be a ReadableStream
  stream.constructor = ReadableStream;
  return stream;
}

function InternalReadableByteStream(start, pull, cancel) {
  ObjectSetPrototypeOf(this, ReadableStream.prototype);
  markTransferMode(this, false, true);
  this[kType] = 'ReadableStream';
  this[kState] = createReadableStreamState();
  this[kIsClosedPromise] = PromiseWithResolvers();
  const controller = new ReadableByteStreamController(kSkipThrow);
  setupReadableByteStreamController(
    this,
    controller,
    start,
    pull,
    cancel,
    0,
    undefined);
}

ObjectSetPrototypeOf(InternalReadableByteStream.prototype, ReadableStream.prototype);
ObjectSetPrototypeOf(InternalReadableByteStream, ReadableStream);

function createReadableByteStream(start, pull, cancel) {
  const stream = new InternalReadableByteStream(start, pull, cancel);

  // For spec compliance the InternalReadableByteStream must be a ReadableStream
  stream.constructor = ReadableStream;
  return stream;
}

const isReadableStream =
  isBrandCheck('ReadableStream');
const isReadableByteStreamController =
  isBrandCheck('ReadableByteStreamController');
const isReadableStreamBYOBRequest =
  isBrandCheck('ReadableStreamBYOBRequest');
const isReadableStreamDefaultReader =
  isBrandCheck('ReadableStreamDefaultReader');
const isReadableStreamBYOBReader =
  isBrandCheck('ReadableStreamBYOBReader');

// ---- ReadableStream Implementation

function createReadableStreamState() {
  return {
    __proto__: null,
    disturbed: false,
    reader: undefined,
    state: 'readable',
    storedError: undefined,
    transfer: {
      __proto__: null,
      writable: undefined,
      port1: undefined,
      port2: undefined,
      promise: undefined,
    },
  };
}

function readableStreamFromIterable(iterable) {
  let stream;
  const iteratorRecord = getIterator(iterable, 'async');

  const startAlgorithm = nonOpStart;

  async function pullAlgorithm() {
    const nextResult = iteratorNext(iteratorRecord);
    const nextPromise = PromiseResolve(nextResult);
    return PromisePrototypeThen(nextPromise, (iterResult) => {
      if (typeof iterResult !== 'object' || iterResult === null) {
        throw new ERR_INVALID_STATE.TypeError(
          'The promise returned by the iterator.next() method must fulfill with an object');
      }
      if (iterResult.done) {
        readableStreamDefaultControllerClose(stream[kState].controller);
      } else {
        readableStreamDefaultControllerEnqueue(stream[kState].controller, iterResult.value);
      }
    });
  }

  async function cancelAlgorithm(reason) {
    const iterator = iteratorRecord.iterator;
    const returnMethod = iterator.return;
    if (returnMethod === undefined) {
      return PromiseResolve();
    }
    const returnResult = FunctionPrototypeCall(returnMethod, iterator, reason);
    const returnPromise = PromiseResolve(returnResult);
    return PromisePrototypeThen(returnPromise, (iterResult) => {
      if (typeof iterResult !== 'object' || iterResult === null) {
        throw new ERR_INVALID_STATE.TypeError(
          'The promise returned by the iterator.return() method must fulfill with an object');
      }
      return undefined;
    });
  }

  stream = createReadableStream(
    startAlgorithm,
    pullAlgorithm,
    cancelAlgorithm,
    0,
  );

  return stream;
}

function readableStreamPipeTo(
  source,
  dest,
  preventClose,
  preventAbort,
  preventCancel,
  signal) {

  let reader;
  let writer;
  let disposable;
  // Both of these can throw synchronously. We want to capture
  // the error and return a rejected promise instead.
  try {
    reader = new ReadableStreamDefaultReader(source);
    writer = new WritableStreamDefaultWriter(dest);
  } catch (error) {
    return PromiseReject(error);
  }

  source[kState].disturbed = true;

  let shuttingDown = false;

  if (signal !== undefined) {
    try {
      validateAbortSignal(signal, 'options.signal');
    } catch (error) {
      return PromiseReject(error);
    }
  }

  const promise = PromiseWithResolvers();

  const state = {
    currentWrite: PromiseResolve(),
  };

  // The error here can be undefined. The rejected arg
  // tells us that the promise must be rejected even
  // when error is undefine.
  function finalize(rejected, error) {
    writableStreamDefaultWriterRelease(writer);
    readableStreamReaderGenericRelease(reader);
    if (signal !== undefined)
      disposable?.[SymbolDispose]();
    if (rejected)
      promise.reject(error);
    else
      promise.resolve();
  }

  async function waitForCurrentWrite() {
    const write = state.currentWrite;
    await write;
    if (write !== state.currentWrite)
      await waitForCurrentWrite();
  }

  function shutdownWithAnAction(action, rejected, originalError) {
    if (shuttingDown) return;
    shuttingDown = true;
    if (dest[kState].state === 'writable' &&
        !writableStreamCloseQueuedOrInFlight(dest)) {
      PromisePrototypeThen(
        waitForCurrentWrite(),
        complete,
        (error) => finalize(true, error));
      return;
    }
    complete();

    function complete() {
      PromisePrototypeThen(
        action(),
        () => finalize(rejected, originalError),
        (error) => finalize(true, error));
    }
  }

  function shutdown(rejected, error) {
    if (shuttingDown) return;
    shuttingDown = true;
    if (dest[kState].state === 'writable' &&
        !writableStreamCloseQueuedOrInFlight(dest)) {
      PromisePrototypeThen(
        waitForCurrentWrite(),
        () => finalize(rejected, error),
        (error) => finalize(true, error));
      return;
    }
    finalize(rejected, error);
  }

  function abortAlgorithm() {
    let error;
    if (signal.reason instanceof AbortError) {
      // Cannot use the AbortError class here. It must be a DOMException.
      error = new DOMException(signal.reason.message, 'AbortError');
    } else {
      error = signal.reason;
    }

    const actions = [];
    if (!preventAbort) {
      ArrayPrototypePush(
        actions,
        () => {
          if (dest[kState].state === 'writable')
            return writableStreamAbort(dest, error);
          return PromiseResolve();
        });
    }
    if (!preventCancel) {
      ArrayPrototypePush(
        actions,
        () => {
          if (source[kState].state === 'readable')
            return readableStreamCancel(source, error);
          return PromiseResolve();
        });
    }

    shutdownWithAnAction(
      () => SafePromiseAll(actions, (action) => action()),
      true,
      error);
  }

  function watchErrored(stream, promise, action) {
    if (stream[kState].state === 'errored')
      action(stream[kState].storedError);
    else
      PromisePrototypeThen(promise, undefined, action);
  }

  function watchClosed(stream, promise, action) {
    if (stream[kState].state === 'closed')
      action();
    else
      PromisePrototypeThen(promise, action, () => {});
  }

  async function step() {
    if (shuttingDown)
      return true;

    await writer[kState].ready.promise;

    const promise = PromiseWithResolvers();
    // eslint-disable-next-line no-use-before-define
    readableStreamDefaultReaderRead(reader, new PipeToReadableStreamReadRequest(writer, state, promise));

    return promise.promise;
  }

  async function run() {
    // Run until step resolves as true
    while (!await step());
  }

  if (signal !== undefined) {
    if (signal.aborted) {
      abortAlgorithm();
      return promise.promise;
    }
    addAbortListener ??= require('internal/events/abort_listener').addAbortListener;
    disposable = addAbortListener(signal, abortAlgorithm);
  }

  setPromiseHandled(run());

  watchErrored(source, reader[kState].close.promise, (error) => {
    if (!preventAbort) {
      return shutdownWithAnAction(
        () => writableStreamAbort(dest, error),
        true,
        error);
    }
    shutdown(true, error);
  });

  watchErrored(dest, writer[kState].close.promise, (error) => {
    if (!preventCancel) {
      return shutdownWithAnAction(
        () => readableStreamCancel(source, error),
        true,
        error);
    }
    shutdown(true, error);
  });

  watchClosed(source, reader[kState].close.promise, () => {
    if (!preventClose) {
      return shutdownWithAnAction(
        () => writableStreamDefaultWriterCloseWithErrorPropagation(writer));
    }
    shutdown();
  });

  if (writableStreamCloseQueuedOrInFlight(dest) ||
      dest[kState].state === 'closed') {
    const error = new ERR_INVALID_STATE.TypeError(
      'Destination WritableStream is closed');
    if (!preventCancel) {
      shutdownWithAnAction(
        () => readableStreamCancel(source, error), true, error);
    } else {
      shutdown(true, error);
    }
  }

  return promise.promise;
}

class PipeToReadableStreamReadRequest {
  constructor(writer, state, promise) {
    this.writer = writer;
    this.state = state;
    this.promise = promise;
  }

  [kChunk](chunk) {
    this.state.currentWrite = writableStreamDefaultWriterWrite(this.writer, chunk);
    setPromiseHandled(this.state.currentWrite);
    this.promise.resolve(false);
  }

  [kClose]() {
    this.promise.resolve(true);
  }

  [kError](error) {
    this.promise.reject(error);
  }
}

function readableStreamTee(stream, cloneForBranch2) {
  if (isReadableByteStreamController(stream[kState].controller)) {
    return readableByteStreamTee(stream);
  }
  return readableStreamDefaultTee(stream, cloneForBranch2);
}

function readableStreamDefaultTee(stream, cloneForBranch2) {
  const reader = new ReadableStreamDefaultReader(stream);
  let reading = false;
  let canceled1 = false;
  let canceled2 = false;
  let reason1;
  let reason2;
  let branch1;
  let branch2;
  const cancelPromise = PromiseWithResolvers();

  async function pullAlgorithm() {
    if (reading) return;
    reading = true;
    const readRequest = {
      [kChunk](value) {
        queueMicrotask(() => {
          reading = false;
          const value1 = value;
          let value2 = value;
          if (!canceled2 && cloneForBranch2) {
            value2 = structuredClone(value2);
          }
          if (!canceled1) {
            readableStreamDefaultControllerEnqueue(
              branch1[kState].controller,
              value1);
          }
          if (!canceled2) {
            readableStreamDefaultControllerEnqueue(
              branch2[kState].controller,
              value2);
          }
        });
      },
      [kClose]() {
        // The `process.nextTick()` is not part of the spec.
        // This approach was needed to avoid a race condition working with esm
        // Further information, see: https://github.com/nodejs/node/issues/39758
        process.nextTick(() => {
          reading = false;
          if (!canceled1)
            readableStreamDefaultControllerClose(branch1[kState].controller);
          if (!canceled2)
            readableStreamDefaultControllerClose(branch2[kState].controller);
          if (!canceled1 || !canceled2)
            cancelPromise.resolve();
        });
      },
      [kError]() {
        reading = false;
      },
    };
    readableStreamDefaultReaderRead(reader, readRequest);
  }

  function cancel1Algorithm(reason) {
    canceled1 = true;
    reason1 = reason;
    if (canceled2) {
      const compositeReason = [reason1, reason2];
      cancelPromise.resolve(readableStreamCancel(stream, compositeReason));
    }
    return cancelPromise.promise;
  }

  function cancel2Algorithm(reason) {
    canceled2 = true;
    reason2 = reason;
    if (canceled1) {
      const compositeReason = [reason1, reason2];
      cancelPromise.resolve(readableStreamCancel(stream, compositeReason));
    }
    return cancelPromise.promise;
  }

  branch1 =
    createReadableStream(nonOpStart, pullAlgorithm, cancel1Algorithm);
  branch2 =
    createReadableStream(nonOpStart, pullAlgorithm, cancel2Algorithm);

  PromisePrototypeThen(
    reader[kState].close.promise,
    undefined,
    (error) => {
      readableStreamDefaultControllerError(branch1[kState].controller, error);
      readableStreamDefaultControllerError(branch2[kState].controller, error);
      if (!canceled1 || !canceled2)
        cancelPromise.resolve();
    });

  return [branch1, branch2];
}

function readableByteStreamTee(stream) {
  assert(isReadableStream(stream));
  assert(isReadableByteStreamController(stream[kState].controller));

  let reader = new ReadableStreamDefaultReader(stream);
  let reading = false;
  let readAgainForBranch1 = false;
  let readAgainForBranch2 = false;
  let canceled1 = false;
  let canceled2 = false;
  let reason1;
  let reason2;
  let branch1;
  let branch2;
  const cancelDeferred = PromiseWithResolvers();

  function forwardReaderError(thisReader) {
    PromisePrototypeThen(
      thisReader[kState].close.promise,
      undefined,
      (error) => {
        if (thisReader !== reader) {
          return;
        }
        readableStreamDefaultControllerError(branch1[kState].controller, error);
        readableStreamDefaultControllerError(branch2[kState].controller, error);
        if (!canceled1 || !canceled2) {
          cancelDeferred.resolve();
        }
      },
    );
  }

  function pullWithDefaultReader() {
    if (isReadableStreamBYOBReader(reader)) {
      readableStreamBYOBReaderRelease(reader);
      reader = new ReadableStreamDefaultReader(stream);
      forwardReaderError(reader);
    }

    const readRequest = {
      [kChunk](chunk) {
        queueMicrotask(() => {
          readAgainForBranch1 = false;
          readAgainForBranch2 = false;
          const chunk1 = chunk;
          let chunk2 = chunk;

          if (!canceled1 && !canceled2) {
            try {
              chunk2 = cloneAsUint8Array(chunk);
            } catch (error) {
              readableByteStreamControllerError(
                branch1[kState].controller,
                error,
              );
              readableByteStreamControllerError(
                branch2[kState].controller,
                error,
              );
              cancelDeferred.resolve(readableStreamCancel(stream, error));
              return;
            }
          }
          if (!canceled1) {
            readableByteStreamControllerEnqueue(
              branch1[kState].controller,
              chunk1,
            );
          }
          if (!canceled2) {
            readableByteStreamControllerEnqueue(
              branch2[kState].controller,
              chunk2,
            );
          }
          reading = false;

          if (readAgainForBranch1) {
            pull1Algorithm();
          } else if (readAgainForBranch2) {
            pull2Algorithm();
          }
        });
      },
      [kClose]() {
        reading = false;

        if (!canceled1) {
          readableByteStreamControllerClose(branch1[kState].controller);
        }
        if (!canceled2) {
          readableByteStreamControllerClose(branch2[kState].controller);
        }
        if (branch1[kState].controller[kState].pendingPullIntos.length > 0) {
          readableByteStreamControllerRespond(branch1[kState].controller, 0);
        }
        if (branch2[kState].controller[kState].pendingPullIntos.length > 0) {
          readableByteStreamControllerRespond(branch2[kState].controller, 0);
        }
        if (!canceled1 || !canceled2) {
          cancelDeferred.resolve();
        }
      },
      [kError]() {
        reading = false;
      },
    };

    readableStreamDefaultReaderRead(reader, readRequest);
  }

  function pullWithBYOBReader(view, forBranch2) {
    if (isReadableStreamDefaultReader(reader)) {
      readableStreamDefaultReaderRelease(reader);
      reader = new ReadableStreamBYOBReader(stream);
      forwardReaderError(reader);
    }

    const byobBranch = forBranch2 === true ? branch2 : branch1;
    const otherBranch = forBranch2 === false ? branch2 : branch1;
    const readIntoRequest = {
      [kChunk](chunk) {
        queueMicrotask(() => {
          readAgainForBranch1 = false;
          readAgainForBranch2 = false;
          const byobCanceled = forBranch2 === true ? canceled2 : canceled1;
          const otherCanceled = forBranch2 === false ? canceled2 : canceled1;

          if (!otherCanceled) {
            let clonedChunk;

            try {
              clonedChunk = cloneAsUint8Array(chunk);
            } catch (error) {
              readableByteStreamControllerError(
                byobBranch[kState].controller,
                error,
              );
              readableByteStreamControllerError(
                otherBranch[kState].controller,
                error,
              );
              cancelDeferred.resolve(readableStreamCancel(stream, error));
              return;
            }
            if (!byobCanceled) {
              readableByteStreamControllerRespondWithNewView(
                byobBranch[kState].controller,
                chunk,
              );
            }

            readableByteStreamControllerEnqueue(
              otherBranch[kState].controller,
              clonedChunk,
            );
          } else if (!byobCanceled) {
            readableByteStreamControllerRespondWithNewView(
              byobBranch[kState].controller,
              chunk,
            );
          }
          reading = false;

          if (readAgainForBranch1) {
            pull1Algorithm();
          } else if (readAgainForBranch2) {
            pull2Algorithm();
          }
        });
      },
      [kClose](chunk) {
        reading = false;

        const byobCanceled = forBranch2 === true ? canceled2 : canceled1;
        const otherCanceled = forBranch2 === false ? canceled2 : canceled1;

        if (!byobCanceled) {
          readableByteStreamControllerClose(byobBranch[kState].controller);
        }
        if (!otherCanceled) {
          readableByteStreamControllerClose(otherBranch[kState].controller);
        }
        if (chunk !== undefined) {
          if (!byobCanceled) {
            readableByteStreamControllerRespondWithNewView(
              byobBranch[kState].controller,
              chunk,
            );
          }
          if (
            !otherCanceled &&
            otherBranch[kState].controller[kState].pendingPullIntos.length > 0
          ) {
            readableByteStreamControllerRespond(
              otherBranch[kState].controller,
              0,
            );
          }
        }
        if (!byobCanceled || !otherCanceled) {
          cancelDeferred.resolve();
        }
      },
      [kError]() {
        reading = false;
      },
    };
    readableStreamBYOBReaderRead(reader, view, 1, readIntoRequest);
  }

  function pull1Algorithm() {
    if (reading) {
      readAgainForBranch1 = true;
      return PromiseResolve();
    }
    reading = true;

    const byobRequest = branch1[kState].controller.byobRequest;
    if (byobRequest === null) {
      pullWithDefaultReader();
    } else {
      pullWithBYOBReader(byobRequest[kState].view, false);
    }
    return PromiseResolve();
  }

  function pull2Algorithm() {
    if (reading) {
      readAgainForBranch2 = true;
      return PromiseResolve();
    }
    reading = true;

    const byobRequest = branch2[kState].controller.byobRequest;
    if (byobRequest === null) {
      pullWithDefaultReader();
    } else {
      pullWithBYOBReader(byobRequest[kState].view, true);
    }
    return PromiseResolve();
  }

  function cancel1Algorithm(reason) {
    canceled1 = true;
    reason1 = reason;
    if (canceled2) {
      cancelDeferred.resolve(readableStreamCancel(stream, [reason1, reason2]));
    }
    return cancelDeferred.promise;
  }

  function cancel2Algorithm(reason) {
    canceled2 = true;
    reason2 = reason;
    if (canceled1) {
      cancelDeferred.resolve(readableStreamCancel(stream, [reason1, reason2]));
    }
    return cancelDeferred.promise;
  }

  branch1 =
    createReadableByteStream(nonOpStart, pull1Algorithm, cancel1Algorithm);
  branch2 =
    createReadableByteStream(nonOpStart, pull2Algorithm, cancel2Algorithm);

  forwardReaderError(reader);

  return [branch1, branch2];
}

function readableByteStreamControllerConvertPullIntoDescriptor(desc) {
  const {
    buffer,
    bytesFilled,
    byteLength,
    byteOffset,
    ctor,
    elementSize,
  } = desc;
  if (bytesFilled > byteLength)
    throw new ERR_INVALID_STATE.RangeError('The buffer size is invalid');
  assert(!(bytesFilled % elementSize));
  const transferredBuffer = ArrayBufferPrototypeTransfer(buffer);

  if (ctor === Buffer) {
    return Buffer.from(transferredBuffer, byteOffset, bytesFilled / elementSize);
  }

  return new ctor(transferredBuffer, byteOffset, bytesFilled / elementSize);
}

function isReadableStreamLocked(stream) {
  return stream[kState].reader !== undefined;
}

function readableStreamCancel(stream, reason) {
  stream[kState].disturbed = true;
  switch (stream[kState].state) {
    case 'closed':
      return PromiseResolve();
    case 'errored':
      return PromiseReject(stream[kState].storedError);
  }
  readableStreamClose(stream);
  const {
    reader,
  } = stream[kState];
  if (reader !== undefined && readableStreamHasBYOBReader(stream)) {
    for (let n = 0; n < reader[kState].readIntoRequests.length; n++)
      reader[kState].readIntoRequests[n][kClose]();
    reader[kState].readIntoRequests = [];
  }

  return PromisePrototypeThen(
    stream[kState].controller[kCancel](reason),
    () => {});
}

function readableStreamClose(stream) {
  assert(stream[kState].state === 'readable');
  stream[kState].state = 'closed';
  stream[kIsClosedPromise].resolve();
  const {
    reader,
  } = stream[kState];

  if (reader === undefined)
    return;

  reader[kState].close.resolve();

  if (readableStreamHasDefaultReader(stream)) {
    for (let n = 0; n < reader[kState].readRequests.length; n++)
      reader[kState].readRequests[n][kClose]();
    reader[kState].readRequests = [];
  }
}

function readableStreamError(stream, error) {
  assert(stream[kState].state === 'readable');
  stream[kState].state = 'errored';
  stream[kState].storedError = error;
  setPromiseHandled(stream[kIsClosedPromise].promise);
  stream[kIsClosedPromise].reject(error);

  const {
    reader,
  } = stream[kState];

  if (reader === undefined)
    return;

  setPromiseHandled(reader[kState].close.promise);
  reader[kState].close.reject(error);

  if (readableStreamHasDefaultReader(stream)) {
    for (let n = 0; n < reader[kState].readRequests.length; n++)
      reader[kState].readRequests[n][kError](error);
    reader[kState].readRequests = [];
  } else {
    assert(readableStreamHasBYOBReader(stream));
    for (let n = 0; n < reader[kState].readIntoRequests.length; n++)
      reader[kState].readIntoRequests[n][kError](error);
    reader[kState].readIntoRequests = [];
  }
}

function readableStreamHasDefaultReader(stream) {
  const {
    reader,
  } = stream[kState];

  if (reader === undefined)
    return false;

  return reader[kState] !== undefined &&
         reader[kType] === 'ReadableStreamDefaultReader';
}

function readableStreamGetNumReadRequests(stream) {
  assert(readableStreamHasDefaultReader(stream));
  return stream[kState].reader[kState].readRequests.length;
}

function readableStreamHasBYOBReader(stream) {
  const {
    reader,
  } = stream[kState];

  if (reader === undefined)
    return false;

  return reader[kState] !== undefined &&
         reader[kType] === 'ReadableStreamBYOBReader';
}

function readableStreamGetNumReadIntoRequests(stream) {
  assert(readableStreamHasBYOBReader(stream));
  return stream[kState].reader[kState].readIntoRequests.length;
}

function readableStreamFulfillReadRequest(stream, chunk, done) {
  assert(readableStreamHasDefaultReader(stream));
  const {
    reader,
  } = stream[kState];
  assert(reader[kState].readRequests.length);
  const readRequest = ArrayPrototypeShift(reader[kState].readRequests);

  // TODO(@jasnell): It's not clear under what exact conditions done
  // will be true here. The spec requires this check but none of the
  // WPT's or other tests trigger it. Will need to investigate how to
  // get coverage for this.
  if (done)
    readRequest[kClose]();
  else
    readRequest[kChunk](chunk);
}

function readableStreamFulfillReadIntoRequest(stream, chunk, done) {
  assert(readableStreamHasBYOBReader(stream));
  const {
    reader,
  } = stream[kState];
  assert(reader[kState].readIntoRequests.length);
  const readIntoRequest = ArrayPrototypeShift(reader[kState].readIntoRequests);
  if (done)
    readIntoRequest[kClose](chunk);
  else
    readIntoRequest[kChunk](chunk);
}

function readableStreamAddReadRequest(stream, readRequest) {
  assert(readableStreamHasDefaultReader(stream));
  assert(stream[kState].state === 'readable');
  ArrayPrototypePush(stream[kState].reader[kState].readRequests, readRequest);
}

function readableStreamAddReadIntoRequest(stream, readIntoRequest) {
  assert(readableStreamHasBYOBReader(stream));
  assert(stream[kState].state !== 'errored');
  ArrayPrototypePush(
    stream[kState].reader[kState].readIntoRequests,
    readIntoRequest);
}

function readableStreamReaderGenericCancel(reader, reason) {
  const {
    stream,
  } = reader[kState];
  assert(stream !== undefined);
  return readableStreamCancel(stream, reason);
}

function readableStreamReaderGenericInitialize(reader, stream) {
  reader[kState].stream = stream;
  stream[kState].reader = reader;
  switch (stream[kState].state) {
    case 'readable':
      reader[kState].close = PromiseWithResolvers();
      break;
    case 'closed':
      reader[kState].close = {
        promise: PromiseResolve(),
        resolve: undefined,
        reject: undefined,
      };
      break;
    case 'errored':
      reader[kState].close = {
        promise: PromiseReject(stream[kState].storedError),
        resolve: undefined,
        reject: undefined,
      };
      setPromiseHandled(reader[kState].close.promise);
      break;
  }
}

function readableStreamDefaultReaderRelease(reader) {
  readableStreamReaderGenericRelease(reader);
  readableStreamDefaultReaderErrorReadRequests(
    reader,
    lazyReadableReleasingError(),
  );
}

function readableStreamDefaultReaderErrorReadRequests(reader, e) {
  for (let n = 0; n < reader[kState].readRequests.length; ++n) {
    reader[kState].readRequests[n][kError](e);
  }
  reader[kState].readRequests = [];
}

function readableStreamBYOBReaderRelease(reader) {
  readableStreamReaderGenericRelease(reader);
  readableStreamBYOBReaderErrorReadIntoRequests(
    reader,
    lazyReadableReleasingError(),
  );
}

function readableStreamBYOBReaderErrorReadIntoRequests(reader, e) {
  for (let n = 0; n < reader[kState].readIntoRequests.length; ++n) {
    reader[kState].readIntoRequests[n][kError](e);
  }
  reader[kState].readIntoRequests = [];
}

function readableStreamReaderGenericRelease(reader) {
  const {
    stream,
  } = reader[kState];
  assert(stream !== undefined);
  assert(stream[kState].reader === reader);

  const releasedStateError = lazyReadableReleasedError();
  if (stream[kState].state === 'readable') {
    reader[kState].close.reject?.(releasedStateError);
  } else {
    reader[kState].close = {
      promise: PromiseReject(releasedStateError),
      resolve: undefined,
      reject: undefined,
    };
  }
  setPromiseHandled(reader[kState].close.promise);

  stream[kState].controller[kRelease]();

  stream[kState].reader = undefined;
  reader[kState].stream = undefined;
}

function readableStreamBYOBReaderRead(reader, view, min, readIntoRequest) {
  const {
    stream,
  } = reader[kState];
  assert(stream !== undefined);
  stream[kState].disturbed = true;
  if (stream[kState].state === 'errored') {
    readIntoRequest[kError](stream[kState].storedError);
    return;
  }
  readableByteStreamControllerPullInto(
    stream[kState].controller,
    view,
    min,
    readIntoRequest);
}

function readableStreamDefaultReaderRead(reader, readRequest) {
  const {
    stream,
  } = reader[kState];
  assert(stream !== undefined);
  stream[kState].disturbed = true;
  switch (stream[kState].state) {
    case 'closed':
      readRequest[kClose]();
      break;
    case 'errored':
      readRequest[kError](stream[kState].storedError);
      break;
    case 'readable':
      stream[kState].controller[kPull](readRequest);
  }
}

function setupReadableStreamBYOBReader(reader, stream) {
  if (isReadableStreamLocked(stream))
    throw new ERR_INVALID_STATE.TypeError('ReadableStream is locked');
  const {
    controller,
  } = stream[kState];
  if (!isReadableByteStreamController(controller))
    throw new ERR_INVALID_ARG_VALUE('stream', stream, 'must be a byte stream');
  readableStreamReaderGenericInitialize(reader, stream);
  reader[kState].readIntoRequests = [];
}

function setupReadableStreamDefaultReader(reader, stream) {
  if (isReadableStreamLocked(stream))
    throw new ERR_INVALID_STATE.TypeError('ReadableStream is locked');
  readableStreamReaderGenericInitialize(reader, stream);
  reader[kState].readRequests = [];
}

function readableStreamDefaultControllerClose(controller) {
  if (!readableStreamDefaultControllerCanCloseOrEnqueue(controller))
    return;
  controller[kState].closeRequested = true;
  if (!controller[kState].queue.length) {
    readableStreamDefaultControllerClearAlgorithms(controller);
    readableStreamClose(controller[kState].stream);
  }
}

function readableStreamDefaultControllerEnqueue(controller, chunk) {
  if (!readableStreamDefaultControllerCanCloseOrEnqueue(controller))
    return;

  const {
    stream,
  } = controller[kState];

  if (isReadableStreamLocked(stream) &&
      readableStreamGetNumReadRequests(stream)) {
    readableStreamFulfillReadRequest(stream, chunk, false);
  } else {
    try {
      const chunkSize =
        FunctionPrototypeCall(
          controller[kState].sizeAlgorithm,
          undefined,
          chunk);
      enqueueValueWithSize(controller, chunk, chunkSize);
    } catch (error) {
      readableStreamDefaultControllerError(controller, error);
      throw error;
    }
  }
  readableStreamDefaultControllerCallPullIfNeeded(controller);
}

function readableStreamDefaultControllerHasBackpressure(controller) {
  return !readableStreamDefaultControllerShouldCallPull(controller);
}

function readableStreamDefaultControllerCanCloseOrEnqueue(controller) {
  const {
    stream,
  } = controller[kState];
  return !controller[kState].closeRequested &&
         stream[kState].state === 'readable';
}

function readableStreamDefaultControllerGetDesiredSize(controller) {
  const {
    stream,
    highWaterMark,
    queueTotalSize,
  } = controller[kState];
  switch (stream[kState].state) {
    case 'errored': return null;
    case 'closed': return 0;
    default:
      return highWaterMark - queueTotalSize;
  }
}

function readableStreamDefaultControllerShouldCallPull(controller) {
  const {
    stream,
  } = controller[kState];
  if (!readableStreamDefaultControllerCanCloseOrEnqueue(controller) ||
      !controller[kState].started)
    return false;

  if (isReadableStreamLocked(stream) &&
      readableStreamGetNumReadRequests(stream)) {
    return true;
  }

  const desiredSize = readableStreamDefaultControllerGetDesiredSize(controller);
  assert(desiredSize !== null);

  return desiredSize > 0;
}

function readableStreamDefaultControllerCallPullIfNeeded(controller) {
  if (!readableStreamDefaultControllerShouldCallPull(controller))
    return;
  if (controller[kState].pulling) {
    controller[kState].pullAgain = true;
    return;
  }
  assert(!controller[kState].pullAgain);
  controller[kState].pulling = true;
  PromisePrototypeThen(
    controller[kState].pullAlgorithm(controller),
    () => {
      controller[kState].pulling = false;
      if (controller[kState].pullAgain) {
        controller[kState].pullAgain = false;
        readableStreamDefaultControllerCallPullIfNeeded(controller);
      }
    },
    (error) => readableStreamDefaultControllerError(controller, error));
}

function readableStreamDefaultControllerClearAlgorithms(controller) {
  controller[kState].pullAlgorithm = undefined;
  controller[kState].cancelAlgorithm = undefined;
  controller[kState].sizeAlgorithm = undefined;
}

function readableStreamDefaultControllerError(controller, error) {
  const {
    stream,
  } = controller[kState];
  if (stream[kState].state === 'readable') {
    resetQueue(controller);
    readableStreamDefaultControllerClearAlgorithms(controller);
    readableStreamError(stream, error);
  }
}

function readableStreamDefaultControllerCancelSteps(controller, reason) {
  resetQueue(controller);
  const result = controller[kState].cancelAlgorithm(reason);
  readableStreamDefaultControllerClearAlgorithms(controller);
  return result;
}

function readableStreamDefaultControllerPullSteps(controller, readRequest) {
  const {
    stream,
    queue,
  } = controller[kState];
  if (queue.length) {
    const chunk = dequeueValue(controller);
    if (controller[kState].closeRequested && !queue.length) {
      readableStreamDefaultControllerClearAlgorithms(controller);
      readableStreamClose(stream);
    } else {
      readableStreamDefaultControllerCallPullIfNeeded(controller);
    }
    readRequest[kChunk](chunk);
    return;
  }
  readableStreamAddReadRequest(stream, readRequest);
  readableStreamDefaultControllerCallPullIfNeeded(controller);
}

function setupReadableStreamDefaultController(
  stream,
  controller,
  startAlgorithm,
  pullAlgorithm,
  cancelAlgorithm,
  highWaterMark,
  sizeAlgorithm) {
  assert(stream[kState].controller === undefined);
  controller[kState] = {
    cancelAlgorithm,
    closeRequested: false,
    highWaterMark,
    pullAgain: false,
    pullAlgorithm,
    pulling: false,
    queue: [],
    queueTotalSize: 0,
    started: false,
    sizeAlgorithm,
    stream,
  };
  stream[kState].controller = controller;
  stream[kControllerErrorFunction] = FunctionPrototypeBind(controller.error, controller);

  const startResult = startAlgorithm();

  PromisePrototypeThen(
    new Promise((r) => r(startResult)),
    () => {
      controller[kState].started = true;
      assert(!controller[kState].pulling);
      assert(!controller[kState].pullAgain);
      readableStreamDefaultControllerCallPullIfNeeded(controller);
    },
    (error) => readableStreamDefaultControllerError(controller, error));
}

function setupReadableStreamDefaultControllerFromSource(
  stream,
  source,
  highWaterMark,
  sizeAlgorithm) {
  const controller = new ReadableStreamDefaultController(kSkipThrow);
  const start = source?.start;
  const pull = source?.pull;
  const cancel = source?.cancel;
  const startAlgorithm = start ?
    FunctionPrototypeBind(start, source, controller) :
    nonOpStart;
  const pullAlgorithm = pull ?
    createPromiseCallback('source.pull', pull, source) :
    nonOpPull;
  const cancelAlgorithm = cancel ?
    createPromiseCallback('source.cancel', cancel, source) :
    nonOpCancel;

  setupReadableStreamDefaultController(
    stream,
    controller,
    startAlgorithm,
    pullAlgorithm,
    cancelAlgorithm,
    highWaterMark,
    sizeAlgorithm);
}

function readableByteStreamControllerClose(controller) {
  const {
    closeRequested,
    pendingPullIntos,
    queueTotalSize,
    stream,
  } = controller[kState];

  if (closeRequested || stream[kState].state !== 'readable')
    return;

  if (queueTotalSize) {
    controller[kState].closeRequested = true;
    return;
  }

  if (pendingPullIntos.length) {
    const firstPendingPullInto = pendingPullIntos[0];
    if (firstPendingPullInto.bytesFilled % firstPendingPullInto.elementSize !== 0) {
      const error = new ERR_INVALID_STATE.TypeError('Partial read');
      readableByteStreamControllerError(controller, error);
      throw error;
    }
  }

  readableByteStreamControllerClearAlgorithms(controller);
  readableStreamClose(stream);
}

function readableByteStreamControllerCommitPullIntoDescriptor(stream, desc) {
  assert(stream[kState].state !== 'errored');
  assert(desc.type !== 'none');

  let done = false;
  if (stream[kState].state === 'closed') {
    assert(desc.bytesFilled % desc.elementSize === 0);
    done = true;
  }

  const filledView =
    readableByteStreamControllerConvertPullIntoDescriptor(desc);

  if (desc.type === 'default') {
    readableStreamFulfillReadRequest(stream, filledView, done);
  } else {
    assert(desc.type === 'byob');
    readableStreamFulfillReadIntoRequest(stream, filledView, done);
  }
}

function readableByteStreamControllerCommitPullIntoDescriptors(stream, descriptors) {
  for (let i = 0; i < descriptors.length; ++i) {
    readableByteStreamControllerCommitPullIntoDescriptor(
      stream,
      descriptors[i],
    );
  }
}

function readableByteStreamControllerInvalidateBYOBRequest(controller) {
  if (controller[kState].byobRequest === null)
    return;
  controller[kState].byobRequest[kState].controller = undefined;
  controller[kState].byobRequest[kState].view = null;
  controller[kState].byobRequest = null;
}

function readableByteStreamControllerClearAlgorithms(controller) {
  controller[kState].pullAlgorithm = undefined;
  controller[kState].cancelAlgorithm = undefined;
}

function readableByteStreamControllerClearPendingPullIntos(controller) {
  readableByteStreamControllerInvalidateBYOBRequest(controller);
  controller[kState].pendingPullIntos = [];
}

function readableByteStreamControllerGetDesiredSize(controller) {
  const {
    stream,
    highWaterMark,
    queueTotalSize,
  } = controller[kState];
  switch (stream[kState].state) {
    case 'errored': return null;
    case 'closed': return 0;
    default: return highWaterMark - queueTotalSize;
  }
}

function readableByteStreamControllerShouldCallPull(controller) {
  const {
    stream,
  } = controller[kState];
  if (stream[kState].state !== 'readable' ||
      controller[kState].closeRequested ||
      !controller[kState].started) {
    return false;
  }
  if (readableStreamHasDefaultReader(stream) &&
      readableStreamGetNumReadRequests(stream) > 0) {
    return true;
  }

  if (readableStreamHasBYOBReader(stream) &&
      readableStreamGetNumReadIntoRequests(stream) > 0) {
    return true;
  }

  const desiredSize = readableByteStreamControllerGetDesiredSize(controller);
  assert(desiredSize !== null);

  return desiredSize > 0;
}

function readableByteStreamControllerHandleQueueDrain(controller) {
  const {
    closeRequested,
    queueTotalSize,
    stream,
  } = controller[kState];
  assert(stream[kState].state === 'readable');
  if (!queueTotalSize && closeRequested) {
    readableByteStreamControllerClearAlgorithms(controller);
    readableStreamClose(stream);
    return;
  }
  readableByteStreamControllerCallPullIfNeeded(controller);
}

function readableByteStreamControllerPullInto(
  controller,
  view,
  min,
  readIntoRequest) {
  const {
    closeRequested,
    stream,
    pendingPullIntos,
  } = controller[kState];
  let elementSize = 1;
  let ctor = DataView;
  if (isArrayBufferView(view) && !isDataView(view)) {
    elementSize = view.constructor.BYTES_PER_ELEMENT;
    ctor = view.constructor;
  }

  const minimumFill = min * elementSize;
  assert(minimumFill >= elementSize && minimumFill <= view.byteLength);
  assert(minimumFill % elementSize === 0);

  const buffer = ArrayBufferViewGetBuffer(view);
  const byteOffset = ArrayBufferViewGetByteOffset(view);
  const byteLength = ArrayBufferViewGetByteLength(view);
  const bufferByteLength = ArrayBufferPrototypeGetByteLength(buffer);

  let transferredBuffer;
  try {
    transferredBuffer = ArrayBufferPrototypeTransfer(buffer);
  } catch (error) {
    readIntoRequest[kError](error);
    return;
  }
  const desc = {
    buffer: transferredBuffer,
    bufferByteLength,
    byteOffset,
    byteLength,
    bytesFilled: 0,
    minimumFill,
    elementSize,
    ctor,
    type: 'byob',
  };
  if (pendingPullIntos.length) {
    ArrayPrototypePush(pendingPullIntos, desc);
    readableStreamAddReadIntoRequest(stream, readIntoRequest);
    return;
  }
  if (stream[kState].state === 'closed') {
    const emptyView = new ctor(desc.buffer, byteOffset, 0);
    readIntoRequest[kClose](emptyView);
    return;
  }
  if (controller[kState].queueTotalSize) {
    if (readableByteStreamControllerFillPullIntoDescriptorFromQueue(
      controller,
      desc)) {
      const filledView =
        readableByteStreamControllerConvertPullIntoDescriptor(desc);
      readableByteStreamControllerHandleQueueDrain(controller);
      readIntoRequest[kChunk](filledView);
      return;
    }
    if (closeRequested) {
      const error = new ERR_INVALID_STATE.TypeError('ReadableStream closed');
      readableByteStreamControllerError(controller, error);
      readIntoRequest[kError](error);
      return;
    }
  }
  ArrayPrototypePush(pendingPullIntos, desc);
  readableStreamAddReadIntoRequest(stream, readIntoRequest);
  readableByteStreamControllerCallPullIfNeeded(controller);
}

function readableByteStreamControllerRespondInternal(controller, bytesWritten) {
  const {
    stream,
    pendingPullIntos,
  } = controller[kState];
  const desc = pendingPullIntos[0];
  readableByteStreamControllerInvalidateBYOBRequest(controller);
  if (stream[kState].state === 'closed') {
    if (bytesWritten)
      throw new ERR_INVALID_STATE.TypeError(
        'Controller is closed but view is not zero-length');
    readableByteStreamControllerRespondInClosedState(controller, desc);
  } else {
    assert(stream[kState].state === 'readable');
    if (!bytesWritten)
      throw new ERR_INVALID_STATE.TypeError('View cannot be zero-length');
    readableByteStreamControllerRespondInReadableState(
      controller,
      bytesWritten,
      desc);
  }
  readableByteStreamControllerCallPullIfNeeded(controller);
}

function readableByteStreamControllerRespond(controller, bytesWritten) {
  const {
    pendingPullIntos,
    stream,
  } = controller[kState];
  assert(pendingPullIntos.length);
  const desc = pendingPullIntos[0];

  if (stream[kState].state === 'closed') {
    if (bytesWritten !== 0)
      throw new ERR_INVALID_ARG_VALUE('bytesWritten', bytesWritten);
  } else {
    assert(stream[kState].state === 'readable');

    if (!bytesWritten)
      throw new ERR_INVALID_ARG_VALUE('bytesWritten', bytesWritten);

    if ((desc.bytesFilled + bytesWritten) > desc.byteLength)
      throw new ERR_INVALID_ARG_VALUE.RangeError('bytesWritten', bytesWritten);
  }

  desc.buffer = ArrayBufferPrototypeTransfer(desc.buffer);

  readableByteStreamControllerRespondInternal(controller, bytesWritten);
}

function readableByteStreamControllerRespondInClosedState(controller, desc) {
  assert(desc.bytesFilled % desc.elementSize === 0);
  if (desc.type === 'none') {
    readableByteStreamControllerShiftPendingPullInto(controller);
  }
  const {
    stream,
  } = controller[kState];
  if (readableStreamHasBYOBReader(stream)) {
    const filledPullIntos = [];
    for (let i = 0; i < readableStreamGetNumReadIntoRequests(stream); ++i) {
      ArrayPrototypePush(filledPullIntos, readableByteStreamControllerShiftPendingPullInto(controller));
    }
    readableByteStreamControllerCommitPullIntoDescriptors(stream, filledPullIntos);
  }
}

function readableByteStreamControllerFillHeadPullIntoDescriptor(
  controller,
  size,
  desc) {
  const {
    pendingPullIntos,
    byobRequest,
  } = controller[kState];
  assert(!pendingPullIntos.length || pendingPullIntos[0] === desc);
  assert(byobRequest === null);
  desc.bytesFilled += size;
}

function readableByteStreamControllerEnqueue(controller, chunk) {
  const {
    closeRequested,
    pendingPullIntos,
    queue,
    stream,
  } = controller[kState];

  const buffer = ArrayBufferViewGetBuffer(chunk);
  const byteOffset = ArrayBufferViewGetByteOffset(chunk);
  const byteLength = ArrayBufferViewGetByteLength(chunk);

  if (closeRequested || stream[kState].state !== 'readable')
    return;

  const transferredBuffer = ArrayBufferPrototypeTransfer(buffer);

  if (pendingPullIntos.length) {
    const firstPendingPullInto = pendingPullIntos[0];

    if (ArrayBufferPrototypeGetDetached(firstPendingPullInto.buffer)) {
      throw new ERR_INVALID_STATE.TypeError(
        'Destination ArrayBuffer is detached',
      );
    }

    readableByteStreamControllerInvalidateBYOBRequest(controller);

    firstPendingPullInto.buffer = ArrayBufferPrototypeTransfer(
      firstPendingPullInto.buffer,
    );

    if (firstPendingPullInto.type === 'none') {
      readableByteStreamControllerEnqueueDetachedPullIntoToQueue(
        controller,
        firstPendingPullInto,
      );
    }
  }

  if (readableStreamHasDefaultReader(stream)) {
    readableByteStreamControllerProcessReadRequestsUsingQueue(controller);
    if (!readableStreamGetNumReadRequests(stream)) {
      readableByteStreamControllerEnqueueChunkToQueue(
        controller,
        transferredBuffer,
        byteOffset,
        byteLength);
    } else {
      assert(!queue.length);
      if (pendingPullIntos.length) {
        assert(pendingPullIntos[0].type === 'default');
        readableByteStreamControllerShiftPendingPullInto(controller);
      }
      const transferredView =
        new Uint8Array(transferredBuffer, byteOffset, byteLength);
      readableStreamFulfillReadRequest(stream, transferredView, false);
    }
  } else if (readableStreamHasBYOBReader(stream)) {
    readableByteStreamControllerEnqueueChunkToQueue(
      controller,
      transferredBuffer,
      byteOffset,
      byteLength);
    const filledPullIntos = readableByteStreamControllerProcessPullIntoDescriptorsUsingQueue(
      controller);
    readableByteStreamControllerCommitPullIntoDescriptors(stream, filledPullIntos);
  } else {
    assert(!isReadableStreamLocked(stream));
    readableByteStreamControllerEnqueueChunkToQueue(
      controller,
      transferredBuffer,
      byteOffset,
      byteLength);
  }
  readableByteStreamControllerCallPullIfNeeded(controller);
}

function readableByteStreamControllerEnqueueClonedChunkToQueue(
  controller,
  buffer,
  byteOffset,
  byteLength,
) {
  let cloneResult;
  try {
    cloneResult = ArrayBufferPrototypeSlice(
      buffer,
      byteOffset,
      byteOffset + byteLength,
    );
  } catch (error) {
    readableByteStreamControllerError(controller, error);
    throw error;
  }
  readableByteStreamControllerEnqueueChunkToQueue(
    controller,
    cloneResult,
    0,
    byteLength,
  );
}

function readableByteStreamControllerEnqueueChunkToQueue(
  controller,
  buffer,
  byteOffset,
  byteLength) {
  ArrayPrototypePush(
    controller[kState].queue,
    {
      buffer,
      byteOffset,
      byteLength,
    });
  controller[kState].queueTotalSize += byteLength;
}

function readableByteStreamControllerEnqueueDetachedPullIntoToQueue(
  controller,
  desc,
) {
  const {
    buffer,
    byteOffset,
    bytesFilled,
    type,
  } = desc;
  assert(type === 'none');

  if (bytesFilled > 0) {
    readableByteStreamControllerEnqueueClonedChunkToQueue(
      controller,
      buffer,
      byteOffset,
      bytesFilled,
    );
  }
  readableByteStreamControllerShiftPendingPullInto(controller);
}

function readableByteStreamControllerFillPullIntoDescriptorFromQueue(
  controller,
  desc) {
  const {
    buffer,
    byteLength,
    byteOffset,
    bytesFilled,
    minimumFill,
    elementSize,
  } = desc;
  const maxBytesToCopy = MathMin(
    controller[kState].queueTotalSize,
    byteLength - bytesFilled);
  const maxBytesFilled = bytesFilled + maxBytesToCopy;
  const maxAlignedBytes = maxBytesFilled - (maxBytesFilled % elementSize);
  let totalBytesToCopyRemaining = maxBytesToCopy;
  let ready = false;
  assert(!ArrayBufferPrototypeGetDetached(buffer));
  assert(bytesFilled < minimumFill);
  if (maxAlignedBytes >= minimumFill) {
    totalBytesToCopyRemaining = maxAlignedBytes - bytesFilled;
    ready = true;
  }
  const {
    queue,
  } = controller[kState];

  while (totalBytesToCopyRemaining) {
    const headOfQueue = queue[0];
    const bytesToCopy = MathMin(
      totalBytesToCopyRemaining,
      headOfQueue.byteLength);
    const destStart = byteOffset + desc.bytesFilled;
    assert(canCopyArrayBuffer(
      buffer,
      destStart,
      headOfQueue.buffer,
      headOfQueue.byteOffset,
      bytesToCopy));
    copyArrayBuffer(
      buffer,
      destStart,
      headOfQueue.buffer,
      headOfQueue.byteOffset,
      bytesToCopy);
    if (headOfQueue.byteLength === bytesToCopy) {
      ArrayPrototypeShift(queue);
    } else {
      headOfQueue.byteOffset += bytesToCopy;
      headOfQueue.byteLength -= bytesToCopy;
    }
    controller[kState].queueTotalSize -= bytesToCopy;
    readableByteStreamControllerFillHeadPullIntoDescriptor(
      controller,
      bytesToCopy,
      desc);
    totalBytesToCopyRemaining -= bytesToCopy;
  }

  if (!ready) {
    assert(!controller[kState].queueTotalSize);
    assert(desc.bytesFilled > 0);
    assert(desc.bytesFilled < minimumFill);
  }
  return ready;
}

function readableByteStreamControllerProcessPullIntoDescriptorsUsingQueue(
  controller) {
  const {
    closeRequested,
    pendingPullIntos,
  } = controller[kState];
  assert(!closeRequested);
  const filledPullIntos = [];
  while (pendingPullIntos.length) {
    if (!controller[kState].queueTotalSize)
      break;
    const desc = pendingPullIntos[0];
    if (readableByteStreamControllerFillPullIntoDescriptorFromQueue(
      controller,
      desc)) {
      readableByteStreamControllerShiftPendingPullInto(controller);
      ArrayPrototypePush(filledPullIntos, desc);
    }
  }
  return filledPullIntos;
}

function readableByteStreamControllerRespondInReadableState(
  controller,
  bytesWritten,
  desc) {
  const {
    stream,
  } = controller[kState];
  const {
    buffer,
    bytesFilled,
    byteLength,
    type,
  } = desc;

  if (bytesFilled + bytesWritten > byteLength)
    throw new ERR_INVALID_STATE.RangeError('The buffer size is invalid');

  readableByteStreamControllerFillHeadPullIntoDescriptor(
    controller,
    bytesWritten,
    desc);

  if (type === 'none') {
    readableByteStreamControllerEnqueueDetachedPullIntoToQueue(
      controller,
      desc,
    );
    const filledPullIntos = readableByteStreamControllerProcessPullIntoDescriptorsUsingQueue(
      controller,
    );
    readableByteStreamControllerCommitPullIntoDescriptors(stream, filledPullIntos);
    return;
  }

  if (desc.bytesFilled < desc.minimumFill)
    return;

  readableByteStreamControllerShiftPendingPullInto(controller);

  const remainderSize = desc.bytesFilled % desc.elementSize;

  if (remainderSize) {
    const end = desc.byteOffset + desc.bytesFilled;
    const start = end - remainderSize;
    const remainder =
      ArrayBufferPrototypeSlice(
        buffer,
        start,
        end);
    readableByteStreamControllerEnqueueChunkToQueue(
      controller,
      remainder,
      0,
      ArrayBufferPrototypeGetByteLength(remainder));
  }
  desc.bytesFilled -= remainderSize;
  const filledPullIntos = readableByteStreamControllerProcessPullIntoDescriptorsUsingQueue(controller);

  readableByteStreamControllerCommitPullIntoDescriptor(stream, desc);
  readableByteStreamControllerCommitPullIntoDescriptors(stream, filledPullIntos);
}

function readableByteStreamControllerRespondWithNewView(controller, view) {
  const {
    stream,
    pendingPullIntos,
  } = controller[kState];
  assert(pendingPullIntos.length);

  const desc = pendingPullIntos[0];
  assert(stream[kState].state !== 'errored');

  const viewByteLength = ArrayBufferViewGetByteLength(view);
  const viewByteOffset = ArrayBufferViewGetByteOffset(view);
  const viewBuffer = ArrayBufferViewGetBuffer(view);
  const viewBufferByteLength = ArrayBufferPrototypeGetByteLength(viewBuffer);

  if (stream[kState].state === 'closed') {
    if (viewByteLength !== 0)
      throw new ERR_INVALID_STATE.TypeError('View is not zero-length');
  } else {
    assert(stream[kState].state === 'readable');
    if (viewByteLength === 0)
      throw new ERR_INVALID_STATE.TypeError('View is zero-length');
  }

  const {
    byteOffset,
    byteLength,
    bytesFilled,
    bufferByteLength,
  } = desc;

  if (byteOffset + bytesFilled !== viewByteOffset)
    throw new ERR_INVALID_ARG_VALUE.RangeError('view', view);

  if (bytesFilled + viewByteLength > byteLength)
    throw new ERR_INVALID_ARG_VALUE.RangeError('view', view);

  if (bufferByteLength !== viewBufferByteLength)
    throw new ERR_INVALID_ARG_VALUE.RangeError('view', view);

  desc.buffer = ArrayBufferPrototypeTransfer(viewBuffer);

  readableByteStreamControllerRespondInternal(controller, viewByteLength);
}

function readableByteStreamControllerShiftPendingPullInto(controller) {
  assert(controller[kState].byobRequest === null);
  return ArrayPrototypeShift(controller[kState].pendingPullIntos);
}

function readableByteStreamControllerCallPullIfNeeded(controller) {
  if (!readableByteStreamControllerShouldCallPull(controller))
    return;
  if (controller[kState].pulling) {
    controller[kState].pullAgain = true;
    return;
  }
  assert(!controller[kState].pullAgain);
  controller[kState].pulling = true;
  PromisePrototypeThen(
    controller[kState].pullAlgorithm(controller),
    () => {
      controller[kState].pulling = false;
      if (controller[kState].pullAgain) {
        controller[kState].pullAgain = false;
        readableByteStreamControllerCallPullIfNeeded(controller);
      }
    },
    (error) => readableByteStreamControllerError(controller, error));
}

function readableByteStreamControllerError(controller, error) {
  const {
    stream,
  } = controller[kState];
  if (stream[kState].state !== 'readable')
    return;
  readableByteStreamControllerClearPendingPullIntos(controller);
  resetQueue(controller);
  readableByteStreamControllerClearAlgorithms(controller);
  readableStreamError(stream, error);
}

function readableByteStreamControllerCancelSteps(controller, reason) {
  readableByteStreamControllerClearPendingPullIntos(controller);
  resetQueue(controller);
  const result = controller[kState].cancelAlgorithm(reason);
  readableByteStreamControllerClearAlgorithms(controller);
  return result;
}

function readableByteStreamControllerFillReadRequestFromQueue(controller, readRequest) {
  const {
    queue,
    queueTotalSize,
  } = controller[kState];
  assert(queueTotalSize > 0);
  const {
    buffer,
    byteOffset,
    byteLength,
  } = ArrayPrototypeShift(queue);

  controller[kState].queueTotalSize -= byteLength;
  readableByteStreamControllerHandleQueueDrain(controller);
  const view = new Uint8Array(buffer, byteOffset, byteLength);
  readRequest[kChunk](view);
}

function readableByteStreamControllerProcessReadRequestsUsingQueue(controller) {
  const {
    stream,
    queueTotalSize,
  } = controller[kState];
  const { reader } = stream[kState];
  assert(isReadableStreamDefaultReader(reader));

  while (reader[kState].readRequests.length > 0) {
    if (queueTotalSize === 0) {
      return;
    }
    readableByteStreamControllerFillReadRequestFromQueue(
      controller,
      ArrayPrototypeShift(reader[kState].readRequests),
    );
  }
}

function readableByteStreamControllerPullSteps(controller, readRequest) {
  const {
    pendingPullIntos,
    queueTotalSize,
    stream,
  } = controller[kState];
  assert(readableStreamHasDefaultReader(stream));
  if (queueTotalSize) {
    assert(!readableStreamGetNumReadRequests(stream));
    readableByteStreamControllerFillReadRequestFromQueue(
      controller,
      readRequest,
    );
    return;
  }
  const {
    autoAllocateChunkSize,
  } = controller[kState];
  if (autoAllocateChunkSize !== undefined) {
    try {
      const buffer = new ArrayBuffer(autoAllocateChunkSize);
      ArrayPrototypePush(
        pendingPullIntos,
        {
          buffer,
          bufferByteLength: autoAllocateChunkSize,
          byteOffset: 0,
          byteLength: autoAllocateChunkSize,
          bytesFilled: 0,
          minimumFill: 1,
          elementSize: 1,
          ctor: Uint8Array,
          type: 'default',
        });
    } catch (error) {
      readRequest[kError](error);
      return;
    }
  }

  readableStreamAddReadRequest(stream, readRequest);
  readableByteStreamControllerCallPullIfNeeded(controller);
}

function setupReadableByteStreamController(
  stream,
  controller,
  startAlgorithm,
  pullAlgorithm,
  cancelAlgorithm,
  highWaterMark,
  autoAllocateChunkSize) {
  assert(stream[kState].controller === undefined);
  if (autoAllocateChunkSize !== undefined) {
    assert(NumberIsInteger(autoAllocateChunkSize));
    assert(autoAllocateChunkSize > 0);
  }
  controller[kState] = {
    byobRequest: null,
    closeRequested: false,
    pullAgain: false,
    pulling: false,
    started: false,
    stream,
    queue: [],
    queueTotalSize: 0,
    highWaterMark,
    pullAlgorithm,
    cancelAlgorithm,
    autoAllocateChunkSize,
    pendingPullIntos: [],
  };
  stream[kState].controller = controller;

  const startResult = startAlgorithm();

  PromisePrototypeThen(
    new Promise((r) => r(startResult)),
    () => {
      controller[kState].started = true;
      assert(!controller[kState].pulling);
      assert(!controller[kState].pullAgain);
      readableByteStreamControllerCallPullIfNeeded(controller);
    },
    (error) => readableByteStreamControllerError(controller, error));
}

function setupReadableByteStreamControllerFromSource(
  stream,
  source,
  highWaterMark) {
  const controller = new ReadableByteStreamController(kSkipThrow);
  const start = source?.start;
  const pull = source?.pull;
  const cancel = source?.cancel;
  const autoAllocateChunkSize = source?.autoAllocateChunkSize;
  const startAlgorithm = start ?
    FunctionPrototypeBind(start, source, controller) :
    nonOpStart;
  const pullAlgorithm = pull ?
    createPromiseCallback('source.pull', pull, source, controller) :
    nonOpPull;
  const cancelAlgorithm = cancel ?
    createPromiseCallback('source.cancel', cancel, source) :
    nonOpCancel;

  if (autoAllocateChunkSize === 0) {
    throw new ERR_INVALID_ARG_VALUE(
      'source.autoAllocateChunkSize',
      autoAllocateChunkSize);
  }
  setupReadableByteStreamController(
    stream,
    controller,
    startAlgorithm,
    pullAlgorithm,
    cancelAlgorithm,
    highWaterMark,
    autoAllocateChunkSize);
}

module.exports = {
  ReadableStream,
  ReadableStreamDefaultReader,
  ReadableStreamBYOBReader,
  ReadableStreamBYOBRequest,
  ReadableByteStreamController,
  ReadableStreamDefaultController,
  TransferredReadableStream,

  // Exported Brand Checks
  isReadableStream,
  isReadableByteStreamController,
  isReadableStreamBYOBRequest,
  isReadableStreamDefaultReader,
  isReadableStreamBYOBReader,
  isWritableStreamDefaultWriter,
  isWritableStreamDefaultController,

  readableStreamPipeTo,
  readableStreamTee,
  readableByteStreamControllerConvertPullIntoDescriptor,
  isReadableStreamLocked,
  readableStreamCancel,
  readableStreamClose,
  readableStreamError,
  readableStreamHasDefaultReader,
  readableStreamGetNumReadRequests,
  readableStreamHasBYOBReader,
  readableStreamGetNumReadIntoRequests,
  readableStreamFulfillReadRequest,
  readableStreamFulfillReadIntoRequest,
  readableStreamAddReadRequest,
  readableStreamAddReadIntoRequest,
  readableStreamReaderGenericCancel,
  readableStreamReaderGenericInitialize,
  readableStreamReaderGenericRelease,
  readableStreamBYOBReaderRead,
  readableStreamDefaultReaderRead,
  setupReadableStreamBYOBReader,
  setupReadableStreamDefaultReader,
  readableStreamDefaultControllerClose,
  readableStreamDefaultControllerEnqueue,
  readableStreamDefaultControllerHasBackpressure,
  readableStreamDefaultControllerCanCloseOrEnqueue,
  readableStreamDefaultControllerGetDesiredSize,
  readableStreamDefaultControllerShouldCallPull,
  readableStreamDefaultControllerCallPullIfNeeded,
  readableStreamDefaultControllerClearAlgorithms,
  readableStreamDefaultControllerError,
  readableStreamDefaultControllerCancelSteps,
  readableStreamDefaultControllerPullSteps,
  setupReadableStreamDefaultController,
  setupReadableStreamDefaultControllerFromSource,
  readableByteStreamControllerClose,
  readableByteStreamControllerCommitPullIntoDescriptor,
  readableByteStreamControllerInvalidateBYOBRequest,
  readableByteStreamControllerClearAlgorithms,
  readableByteStreamControllerClearPendingPullIntos,
  readableByteStreamControllerGetDesiredSize,
  readableByteStreamControllerShouldCallPull,
  readableByteStreamControllerHandleQueueDrain,
  readableByteStreamControllerPullInto,
  readableByteStreamControllerRespondInternal,
  readableByteStreamControllerRespond,
  readableByteStreamControllerRespondInClosedState,
  readableByteStreamControllerFillHeadPullIntoDescriptor,
  readableByteStreamControllerEnqueue,
  readableByteStreamControllerEnqueueChunkToQueue,
  readableByteStreamControllerFillPullIntoDescriptorFromQueue,
  readableByteStreamControllerProcessPullIntoDescriptorsUsingQueue,
  readableByteStreamControllerRespondInReadableState,
  readableByteStreamControllerRespondWithNewView,
  readableByteStreamControllerShiftPendingPullInto,
  readableByteStreamControllerCallPullIfNeeded,
  readableByteStreamControllerError,
  readableByteStreamControllerCancelSteps,
  readableByteStreamControllerPullSteps,
  setupReadableByteStreamController,
  setupReadableByteStreamControllerFromSource,
  createReadableStream,
  createReadableByteStream,
};
