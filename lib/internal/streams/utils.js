'use strict';

const {
  Symbol,
  SymbolAsyncIterator,
  SymbolFor,
  SymbolIterator,
} = primordials;

// We need to use SymbolFor to make these globally available
// for interoperability with readable-stream, i.e. readable-stream
// and node core needs to be able to read/write private state
// from each other for proper interoperability.
const kIsDestroyed = SymbolFor('nodejs.stream.destroyed');
const kIsErrored = SymbolFor('nodejs.stream.errored');
const kIsReadable = SymbolFor('nodejs.stream.readable');
const kIsWritable = SymbolFor('nodejs.stream.writable');
const kIsDisturbed = SymbolFor('nodejs.stream.disturbed');

const kOnConstructed = Symbol('kOnConstructed');

const kIsClosedPromise = SymbolFor('nodejs.webstream.isClosedPromise');
const kControllerErrorFunction = SymbolFor('nodejs.webstream.controllerErrorFunction');

const kState = Symbol('kState');
const kObjectMode = 1 << 0;
const kErrorEmitted = 1 << 1;
const kAutoDestroy = 1 << 2;
const kEmitClose = 1 << 3;
const kDestroyed = 1 << 4;
const kClosed = 1 << 5;
const kCloseEmitted = 1 << 6;
const kErrored = 1 << 7;
const kConstructed = 1 << 8;

function isReadableNodeStream(obj, strict = false) {
  return !!(
    obj &&
    typeof obj.pipe === 'function' &&
    typeof obj.on === 'function' &&
    (
      !strict ||
      (typeof obj.pause === 'function' && typeof obj.resume === 'function')
    ) &&
    (!obj._writableState || obj._readableState?.readable !== false) && // Duplex
    (!obj._writableState || obj._readableState) // Writable has .pipe.
  );
}

function isWritableNodeStream(obj) {
  return !!(
    obj &&
    typeof obj.write === 'function' &&
    typeof obj.on === 'function' &&
    (!obj._readableState || obj._writableState?.writable !== false) // Duplex
  );
}

function isDuplexNodeStream(obj) {
  return !!(
    obj &&
    (typeof obj.pipe === 'function' && obj._readableState) &&
    typeof obj.on === 'function' &&
    typeof obj.write === 'function'
  );
}

function isNodeStream(obj) {
  return (
    obj &&
    (
      obj._readableState ||
      obj._writableState ||
      (typeof obj.write === 'function' && typeof obj.on === 'function') ||
      (typeof obj.pipe === 'function' && typeof obj.on === 'function')
    )
  );
}

function isReadableStream(obj) {
  return !!(
    obj &&
    !isNodeStream(obj) &&
    typeof obj.pipeThrough === 'function' &&
    typeof obj.getReader === 'function' &&
    typeof obj.cancel === 'function'
  );
}

function isWritableStream(obj) {
  return !!(
    obj &&
    !isNodeStream(obj) &&
    typeof obj.getWriter === 'function' &&
    typeof obj.abort === 'function'
  );
}

function isTransformStream(obj) {
  return !!(
    obj &&
    !isNodeStream(obj) &&
    typeof obj.readable === 'object' &&
    typeof obj.writable === 'object'
  );
}

function isWebStream(obj) {
  return isReadableStream(obj) || isWritableStream(obj) || isTransformStream(obj);
}

function isIterable(obj, isAsync) {
  if (obj == null) return false;
  if (isAsync === true) return typeof obj[SymbolAsyncIterator] === 'function';
  if (isAsync === false) return typeof obj[SymbolIterator] === 'function';
  return typeof obj[SymbolAsyncIterator] === 'function' ||
    typeof obj[SymbolIterator] === 'function';
}

function isDestroyed(stream) {
  if (!isNodeStream(stream)) return null;
  const wState = stream._writableState;
  const rState = stream._readableState;
  const state = wState || rState;
  return !!(stream.destroyed || stream[kIsDestroyed] || state?.destroyed);
}

// Have been end():d.
function isWritableEnded(stream) {
  if (!isWritableNodeStream(stream)) return null;
  if (stream.writableEnded === true) return true;
  const wState = stream._writableState;
  if (wState?.errored) return false;
  if (typeof wState?.ended !== 'boolean') return null;
  return wState.ended;
}

// Have emitted 'finish'.
function isWritableFinished(stream, strict) {
  if (!isWritableNodeStream(stream)) return null;
  if (stream.writableFinished === true) return true;
  const wState = stream._writableState;
  if (wState?.errored) return false;
  if (typeof wState?.finished !== 'boolean') return null;
  return !!(
    wState.finished ||
    (strict === false && wState.ended === true && wState.length === 0)
  );
}

// Have been push(null):d.
function isReadableEnded(stream) {
  if (!isReadableNodeStream(stream)) return null;
  if (stream.readableEnded === true) return true;
  const rState = stream._readableState;
  if (!rState || rState.errored) return false;
  if (typeof rState?.ended !== 'boolean') return null;
  return rState.ended;
}

// Have emitted 'end'.
function isReadableFinished(stream, strict) {
  if (!isReadableNodeStream(stream)) return null;
  const rState = stream._readableState;
  if (rState?.errored) return false;
  if (typeof rState?.endEmitted !== 'boolean') return null;
  return !!(
    rState.endEmitted ||
    (strict === false && rState.ended === true && rState.length === 0)
  );
}

function isReadable(stream) {
  if (stream && stream[kIsReadable] != null) return stream[kIsReadable];
  if (typeof stream?.readable !== 'boolean') return null;
  if (isDestroyed(stream)) return false;
  return isReadableNodeStream(stream) &&
    stream.readable &&
    !isReadableFinished(stream);
}

function isWritable(stream) {
  if (stream && stream[kIsWritable] != null) return stream[kIsWritable];
  if (typeof stream?.writable !== 'boolean') return null;
  if (isDestroyed(stream)) return false;
  return isWritableNodeStream(stream) &&
    stream.writable &&
    !isWritableEnded(stream);
}

function isFinished(stream, opts) {
  if (!isNodeStream(stream)) {
    return null;
  }

  if (isDestroyed(stream)) {
    return true;
  }

  if (opts?.readable !== false && isReadable(stream)) {
    return false;
  }

  if (opts?.writable !== false && isWritable(stream)) {
    return false;
  }

  return true;
}

function isWritableErrored(stream) {
  if (!isNodeStream(stream)) {
    return null;
  }

  if (stream.writableErrored) {
    return stream.writableErrored;
  }

  return stream._writableState?.errored ?? null;
}

function isReadableErrored(stream) {
  if (!isNodeStream(stream)) {
    return null;
  }

  if (stream.readableErrored) {
    return stream.readableErrored;
  }

  return stream._readableState?.errored ?? null;
}

function isClosed(stream) {
  if (!isNodeStream(stream)) {
    return null;
  }

  if (typeof stream.closed === 'boolean') {
    return stream.closed;
  }

  const wState = stream._writableState;
  const rState = stream._readableState;

  if (
    typeof wState?.closed === 'boolean' ||
    typeof rState?.closed === 'boolean'
  ) {
    return wState?.closed || rState?.closed;
  }

  if (typeof stream._closed === 'boolean' && isOutgoingMessage(stream)) {
    return stream._closed;
  }

  return null;
}

function isOutgoingMessage(stream) {
  return (
    typeof stream._closed === 'boolean' &&
    typeof stream._defaultKeepAlive === 'boolean' &&
    typeof stream._removedConnection === 'boolean' &&
    typeof stream._removedContLen === 'boolean'
  );
}

function isServerResponse(stream) {
  return (
    typeof stream._sent100 === 'boolean' &&
    isOutgoingMessage(stream)
  );
}

function isServerRequest(stream) {
  return (
    typeof stream._consuming === 'boolean' &&
    typeof stream._dumped === 'boolean' &&
    stream.req?.upgradeOrConnect === undefined
  );
}

function willEmitClose(stream) {
  if (!isNodeStream(stream)) return null;

  const wState = stream._writableState;
  const rState = stream._readableState;
  const state = wState || rState;

  return (!state && isServerResponse(stream)) || !!(
    state?.autoDestroy &&
    state.emitClose &&
    state.closed === false
  );
}

function isDisturbed(stream) {
  return !!(stream && (
    stream[kIsDisturbed] ??
    (stream.readableDidRead || stream.readableAborted)
  ));
}

function isErrored(stream) {
  return !!(stream && (
    stream[kIsErrored] ??
    stream.readableErrored ??
    stream.writableErrored ??
    stream._readableState?.errorEmitted ??
    stream._writableState?.errorEmitted ??
    stream._readableState?.errored ??
    stream._writableState?.errored
  ));
}

module.exports = {
  kOnConstructed,
  isDestroyed,
  kIsDestroyed,
  isDisturbed,
  kIsDisturbed,
  isErrored,
  kIsErrored,
  isReadable,
  kIsReadable,
  kIsClosedPromise,
  kControllerErrorFunction,
  kIsWritable,
  isClosed,
  isDuplexNodeStream,
  isFinished,
  isIterable,
  isReadableNodeStream,
  isReadableStream,
  isReadableEnded,
  isReadableFinished,
  isReadableErrored,
  isNodeStream,
  isWebStream,
  isWritable,
  isWritableNodeStream,
  isWritableStream,
  isWritableEnded,
  isWritableFinished,
  isWritableErrored,
  isServerRequest,
  isServerResponse,
  willEmitClose,
  isTransformStream,
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
};
