'use strict';

const {
  ArrayBuffer,
  DataView,
  DataViewPrototypeGetBigInt64,
  DataViewPrototypeGetBigUint64,
  DataViewPrototypeGetUint8,
  DataViewPrototypeSetUint8,
  JSONStringify,
} = primordials;

const {
  codes: {
    ERR_ILLEGAL_CONSTRUCTOR,
    ERR_INVALID_ARG_TYPE,
  },
} = require('internal/errors');

const {
  isArrayBuffer,
} = require('util/types');

const { inspect } = require('internal/util/inspect');
const assert = require('internal/assert');

const {
  kFinishClose,
  kInspect,
  kPrivateConstructor,
  kWantsHeaders,
  kWantsTrailers,
} = require('internal/quic/symbols');

// This file defines the helper objects for accessing state for
// various QUIC objects. Each of these wraps a DataView.
// Some of the state properties are read only, others are mutable.
// An ArrayBuffer is shared with the C++ level to allow for more
// efficient communication of state across the C++/JS boundary.
// When the state object is no longer needed, it is closed to
// prevent further updates to the buffer.

const {
  IDX_STATE_SESSION_PATH_VALIDATION,
  IDX_STATE_SESSION_VERSION_NEGOTIATION,
  IDX_STATE_SESSION_DATAGRAM,
  IDX_STATE_SESSION_SESSION_TICKET,
  IDX_STATE_SESSION_CLOSING,
  IDX_STATE_SESSION_GRACEFUL_CLOSE,
  IDX_STATE_SESSION_SILENT_CLOSE,
  IDX_STATE_SESSION_STATELESS_RESET,
  IDX_STATE_SESSION_HANDSHAKE_COMPLETED,
  IDX_STATE_SESSION_HANDSHAKE_CONFIRMED,
  IDX_STATE_SESSION_STREAM_OPEN_ALLOWED,
  IDX_STATE_SESSION_PRIORITY_SUPPORTED,
  IDX_STATE_SESSION_WRAPPED,
  IDX_STATE_SESSION_LAST_DATAGRAM_ID,

  IDX_STATE_ENDPOINT_BOUND,
  IDX_STATE_ENDPOINT_RECEIVING,
  IDX_STATE_ENDPOINT_LISTENING,
  IDX_STATE_ENDPOINT_CLOSING,
  IDX_STATE_ENDPOINT_BUSY,
  IDX_STATE_ENDPOINT_PENDING_CALLBACKS,

  IDX_STATE_STREAM_ID,
  IDX_STATE_STREAM_PENDING,
  IDX_STATE_STREAM_FIN_SENT,
  IDX_STATE_STREAM_FIN_RECEIVED,
  IDX_STATE_STREAM_READ_ENDED,
  IDX_STATE_STREAM_WRITE_ENDED,
  IDX_STATE_STREAM_PAUSED,
  IDX_STATE_STREAM_RESET,
  IDX_STATE_STREAM_HAS_OUTBOUND,
  IDX_STATE_STREAM_HAS_READER,
  IDX_STATE_STREAM_WANTS_BLOCK,
  IDX_STATE_STREAM_WANTS_HEADERS,
  IDX_STATE_STREAM_WANTS_RESET,
  IDX_STATE_STREAM_WANTS_TRAILERS,
} = internalBinding('quic');

assert(IDX_STATE_SESSION_PATH_VALIDATION !== undefined);
assert(IDX_STATE_SESSION_VERSION_NEGOTIATION !== undefined);
assert(IDX_STATE_SESSION_DATAGRAM !== undefined);
assert(IDX_STATE_SESSION_SESSION_TICKET !== undefined);
assert(IDX_STATE_SESSION_CLOSING !== undefined);
assert(IDX_STATE_SESSION_GRACEFUL_CLOSE !== undefined);
assert(IDX_STATE_SESSION_SILENT_CLOSE !== undefined);
assert(IDX_STATE_SESSION_STATELESS_RESET !== undefined);
assert(IDX_STATE_SESSION_HANDSHAKE_COMPLETED !== undefined);
assert(IDX_STATE_SESSION_HANDSHAKE_CONFIRMED !== undefined);
assert(IDX_STATE_SESSION_STREAM_OPEN_ALLOWED !== undefined);
assert(IDX_STATE_SESSION_PRIORITY_SUPPORTED !== undefined);
assert(IDX_STATE_SESSION_WRAPPED !== undefined);
assert(IDX_STATE_SESSION_LAST_DATAGRAM_ID !== undefined);
assert(IDX_STATE_ENDPOINT_BOUND !== undefined);
assert(IDX_STATE_ENDPOINT_RECEIVING !== undefined);
assert(IDX_STATE_ENDPOINT_LISTENING !== undefined);
assert(IDX_STATE_ENDPOINT_CLOSING !== undefined);
assert(IDX_STATE_ENDPOINT_BUSY !== undefined);
assert(IDX_STATE_ENDPOINT_PENDING_CALLBACKS !== undefined);
assert(IDX_STATE_STREAM_ID !== undefined);
assert(IDX_STATE_STREAM_PENDING !== undefined);
assert(IDX_STATE_STREAM_FIN_SENT !== undefined);
assert(IDX_STATE_STREAM_FIN_RECEIVED !== undefined);
assert(IDX_STATE_STREAM_READ_ENDED !== undefined);
assert(IDX_STATE_STREAM_WRITE_ENDED !== undefined);
assert(IDX_STATE_STREAM_PAUSED !== undefined);
assert(IDX_STATE_STREAM_RESET !== undefined);
assert(IDX_STATE_STREAM_HAS_OUTBOUND !== undefined);
assert(IDX_STATE_STREAM_HAS_READER !== undefined);
assert(IDX_STATE_STREAM_WANTS_BLOCK !== undefined);
assert(IDX_STATE_STREAM_WANTS_HEADERS !== undefined);
assert(IDX_STATE_STREAM_WANTS_RESET !== undefined);
assert(IDX_STATE_STREAM_WANTS_TRAILERS !== undefined);

class QuicEndpointState {
  /** @type {DataView} */
  #handle;

  /**
   * @param {symbol} privateSymbol
   * @param {ArrayBuffer} buffer
   */
  constructor(privateSymbol, buffer) {
    if (privateSymbol !== kPrivateConstructor) {
      throw new ERR_ILLEGAL_CONSTRUCTOR();
    }
    if (!isArrayBuffer(buffer)) {
      throw new ERR_INVALID_ARG_TYPE('buffer', ['ArrayBuffer'], buffer);
    }
    this.#handle = new DataView(buffer);
  }

  /** @type {boolean} */
  get isBound() {
    if (this.#handle.byteLength === 0) return undefined;
    return !!DataViewPrototypeGetUint8(this.#handle, IDX_STATE_ENDPOINT_BOUND);
  }

  /** @type {boolean} */
  get isReceiving() {
    if (this.#handle.byteLength === 0) return undefined;
    return !!DataViewPrototypeGetUint8(this.#handle, IDX_STATE_ENDPOINT_RECEIVING);
  }

  /** @type {boolean} */
  get isListening() {
    if (this.#handle.byteLength === 0) return undefined;
    return !!DataViewPrototypeGetUint8(this.#handle, IDX_STATE_ENDPOINT_LISTENING);
  }

  /** @type {boolean} */
  get isClosing() {
    if (this.#handle.byteLength === 0) return undefined;
    return !!DataViewPrototypeGetUint8(this.#handle, IDX_STATE_ENDPOINT_CLOSING);
  }

  /** @type {boolean} */
  get isBusy() {
    if (this.#handle.byteLength === 0) return undefined;
    return !!DataViewPrototypeGetUint8(this.#handle, IDX_STATE_ENDPOINT_BUSY);
  }

  /**
   * The number of underlying callbacks that are pending. If the session
   * is closing, these are the number of callbacks that the session is
   * waiting on before it can be closed.
   * @type {bigint}
   */
  get pendingCallbacks() {
    if (this.#handle.byteLength === 0) return undefined;
    return DataViewPrototypeGetBigUint64(this.#handle, IDX_STATE_ENDPOINT_PENDING_CALLBACKS);
  }

  toString() {
    return JSONStringify(this.toJSON());
  }

  toJSON() {
    if (this.#handle.byteLength === 0) return {};
    return {
      __proto__: null,
      isBound: this.isBound,
      isReceiving: this.isReceiving,
      isListening: this.isListening,
      isClosing: this.isClosing,
      isBusy: this.isBusy,
      pendingCallbacks: `${this.pendingCallbacks}`,
    };
  }

  [kInspect](depth, options) {
    if (depth < 0)
      return this;

    if (this.#handle.byteLength === 0) {
      return 'QuicEndpointState { <Closed> }';
    }

    const opts = {
      ...options,
      depth: options.depth == null ? null : options.depth - 1,
    };

    return `QuicEndpointState ${inspect({
      isBound: this.isBound,
      isReceiving: this.isReceiving,
      isListening: this.isListening,
      isClosing: this.isClosing,
      isBusy: this.isBusy,
      pendingCallbacks: this.pendingCallbacks,
    }, opts)}`;
  }

  [kFinishClose]() {
    // Snapshot the state into a new DataView since the underlying
    // buffer will be destroyed.
    if (this.#handle.byteLength === 0) return;
    this.#handle = new DataView(new ArrayBuffer(0));
  }
}

class QuicSessionState {
  /** @type {DataView} */
  #handle;

  /**
   * @param {symbol} privateSymbol
   * @param {ArrayBuffer} buffer
   */
  constructor(privateSymbol, buffer) {
    if (privateSymbol !== kPrivateConstructor) {
      throw new ERR_ILLEGAL_CONSTRUCTOR();
    }
    if (!isArrayBuffer(buffer)) {
      throw new ERR_INVALID_ARG_TYPE('buffer', ['ArrayBuffer'], buffer);
    }
    this.#handle = new DataView(buffer);
  }

  /** @type {boolean} */
  get hasPathValidationListener() {
    if (this.#handle.byteLength === 0) return undefined;
    return !!DataViewPrototypeGetUint8(this.#handle, IDX_STATE_SESSION_PATH_VALIDATION);
  }

  /** @type {boolean} */
  set hasPathValidationListener(val) {
    if (this.#handle.byteLength === 0) return;
    DataViewPrototypeSetUint8(this.#handle, IDX_STATE_SESSION_PATH_VALIDATION, val ? 1 : 0);
  }

  /** @type {boolean} */
  get hasVersionNegotiationListener() {
    if (this.#handle.byteLength === 0) return undefined;
    return !!DataViewPrototypeGetUint8(this.#handle, IDX_STATE_SESSION_VERSION_NEGOTIATION);
  }

  /** @type {boolean} */
  set hasVersionNegotiationListener(val) {
    if (this.#handle.byteLength === 0) return;
    DataViewPrototypeSetUint8(this.#handle, IDX_STATE_SESSION_VERSION_NEGOTIATION, val ? 1 : 0);
  }

  /** @type {boolean} */
  get hasDatagramListener() {
    if (this.#handle.byteLength === 0) return undefined;
    return !!DataViewPrototypeGetUint8(this.#handle, IDX_STATE_SESSION_DATAGRAM);
  }

  /** @type {boolean} */
  set hasDatagramListener(val) {
    if (this.#handle.byteLength === 0) return;
    DataViewPrototypeSetUint8(this.#handle, IDX_STATE_SESSION_DATAGRAM, val ? 1 : 0);
  }

  /** @type {boolean} */
  get hasSessionTicketListener() {
    if (this.#handle.byteLength === 0) return undefined;
    return !!DataViewPrototypeGetUint8(this.#handle, IDX_STATE_SESSION_SESSION_TICKET);
  }

  /** @type {boolean} */
  set hasSessionTicketListener(val) {
    if (this.#handle.byteLength === 0) return;
    DataViewPrototypeSetUint8(this.#handle, IDX_STATE_SESSION_SESSION_TICKET, val ? 1 : 0);
  }

  /** @type {boolean} */
  get isClosing() {
    if (this.#handle.byteLength === 0) return undefined;
    return !!DataViewPrototypeGetUint8(this.#handle, IDX_STATE_SESSION_CLOSING);
  }

  /** @type {boolean} */
  get isGracefulClose() {
    if (this.#handle.byteLength === 0) return undefined;
    return !!DataViewPrototypeGetUint8(this.#handle, IDX_STATE_SESSION_GRACEFUL_CLOSE);
  }

  /** @type {boolean} */
  get isSilentClose() {
    if (this.#handle.byteLength === 0) return undefined;
    return !!DataViewPrototypeGetUint8(this.#handle, IDX_STATE_SESSION_SILENT_CLOSE);
  }

  /** @type {boolean} */
  get isStatelessReset() {
    if (this.#handle.byteLength === 0) return undefined;
    return !!DataViewPrototypeGetUint8(this.#handle, IDX_STATE_SESSION_STATELESS_RESET);
  }

  /** @type {boolean} */
  get isHandshakeCompleted() {
    if (this.#handle.byteLength === 0) return undefined;
    return !!DataViewPrototypeGetUint8(this.#handle, IDX_STATE_SESSION_HANDSHAKE_COMPLETED);
  }

  /** @type {boolean} */
  get isHandshakeConfirmed() {
    if (this.#handle.byteLength === 0) return undefined;
    return !!DataViewPrototypeGetUint8(this.#handle, IDX_STATE_SESSION_HANDSHAKE_CONFIRMED);
  }

  /** @type {boolean} */
  get isStreamOpenAllowed() {
    if (this.#handle.byteLength === 0) return undefined;
    return !!DataViewPrototypeGetUint8(this.#handle, IDX_STATE_SESSION_STREAM_OPEN_ALLOWED);
  }

  /** @type {boolean} */
  get isPrioritySupported() {
    if (this.#handle.byteLength === 0) return undefined;
    return !!DataViewPrototypeGetUint8(this.#handle, IDX_STATE_SESSION_PRIORITY_SUPPORTED);
  }

  /** @type {boolean} */
  get isWrapped() {
    if (this.#handle.byteLength === 0) return undefined;
    return !!DataViewPrototypeGetUint8(this.#handle, IDX_STATE_SESSION_WRAPPED);
  }

  /** @type {bigint} */
  get lastDatagramId() {
    if (this.#handle.byteLength === 0) return undefined;
    return DataViewPrototypeGetBigUint64(this.#handle, IDX_STATE_SESSION_LAST_DATAGRAM_ID);
  }

  toString() {
    return JSONStringify(this.toJSON());
  }

  toJSON() {
    if (this.#handle.byteLength === 0) return {};
    return {
      __proto__: null,
      hasPathValidationListener: this.hasPathValidationListener,
      hasVersionNegotiationListener: this.hasVersionNegotiationListener,
      hasDatagramListener: this.hasDatagramListener,
      hasSessionTicketListener: this.hasSessionTicketListener,
      isClosing: this.isClosing,
      isGracefulClose: this.isGracefulClose,
      isSilentClose: this.isSilentClose,
      isStatelessReset: this.isStatelessReset,
      isDestroyed: this.isDestroyed,
      isHandshakeCompleted: this.isHandshakeCompleted,
      isHandshakeConfirmed: this.isHandshakeConfirmed,
      isStreamOpenAllowed: this.isStreamOpenAllowed,
      isPrioritySupported: this.isPrioritySupported,
      isWrapped: this.isWrapped,
      lastDatagramId: `${this.lastDatagramId}`,
    };
  }

  [kInspect](depth, options) {
    if (depth < 0)
      return this;

    if (this.#handle.byteLength === 0) {
      return 'QuicSessionState { <Closed> }';
    }

    const opts = {
      ...options,
      depth: options.depth == null ? null : options.depth - 1,
    };

    return `QuicSessionState ${inspect({
      hasPathValidationListener: this.hasPathValidationListener,
      hasVersionNegotiationListener: this.hasVersionNegotiationListener,
      hasDatagramListener: this.hasDatagramListener,
      hasSessionTicketListener: this.hasSessionTicketListener,
      isClosing: this.isClosing,
      isGracefulClose: this.isGracefulClose,
      isSilentClose: this.isSilentClose,
      isStatelessReset: this.isStatelessReset,
      isDestroyed: this.isDestroyed,
      isHandshakeCompleted: this.isHandshakeCompleted,
      isHandshakeConfirmed: this.isHandshakeConfirmed,
      isStreamOpenAllowed: this.isStreamOpenAllowed,
      isPrioritySupported: this.isPrioritySupported,
      isWrapped: this.isWrapped,
      lastDatagramId: this.lastDatagramId,
    }, opts)}`;
  }

  [kFinishClose]() {
    // Snapshot the state into a new DataView since the underlying
    // buffer will be destroyed.
    if (this.#handle.byteLength === 0) return;
    this.#handle = new DataView(new ArrayBuffer(0));
  }
}

class QuicStreamState {
  /** @type {DataView} */
  #handle;

  /**
   * @param {symbol} privateSymbol
   * @param {ArrayBuffer} buffer
   */
  constructor(privateSymbol, buffer) {
    if (privateSymbol !== kPrivateConstructor) {
      throw new ERR_ILLEGAL_CONSTRUCTOR();
    }
    if (!isArrayBuffer(buffer)) {
      throw new ERR_INVALID_ARG_TYPE('buffer', ['ArrayBuffer'], buffer);
    }
    this.#handle = new DataView(buffer);
  }

  /** @type {bigint} */
  get id() {
    if (this.#handle.byteLength === 0) return undefined;
    return DataViewPrototypeGetBigInt64(this.#handle, IDX_STATE_STREAM_ID);
  }

  /** @type {boolean} */
  get pending() {
    if (this.#handle.byteLength === 0) return undefined;
    return !!DataViewPrototypeGetUint8(this.#handle, IDX_STATE_STREAM_PENDING);
  }

  /** @type {boolean} */
  get finSent() {
    if (this.#handle.byteLength === 0) return undefined;
    return !!DataViewPrototypeGetUint8(this.#handle, IDX_STATE_STREAM_FIN_SENT);
  }

  /** @type {boolean} */
  get finReceived() {
    if (this.#handle.byteLength === 0) return undefined;
    return !!DataViewPrototypeGetUint8(this.#handle, IDX_STATE_STREAM_FIN_RECEIVED);
  }

  /** @type {boolean} */
  get readEnded() {
    if (this.#handle.byteLength === 0) return undefined;
    return !!DataViewPrototypeGetUint8(this.#handle, IDX_STATE_STREAM_READ_ENDED);
  }

  /** @type {boolean} */
  get writeEnded() {
    if (this.#handle.byteLength === 0) return undefined;
    return !!DataViewPrototypeGetUint8(this.#handle, IDX_STATE_STREAM_WRITE_ENDED);
  }

  /** @type {boolean} */
  get paused() {
    if (this.#handle.byteLength === 0) return undefined;
    return !!DataViewPrototypeGetUint8(this.#handle, IDX_STATE_STREAM_PAUSED);
  }

  /** @type {boolean} */
  get reset() {
    if (this.#handle.byteLength === 0) return undefined;
    return !!DataViewPrototypeGetUint8(this.#handle, IDX_STATE_STREAM_RESET);
  }

  /** @type {boolean} */
  get hasOutbound() {
    if (this.#handle.byteLength === 0) return undefined;
    return !!DataViewPrototypeGetUint8(this.#handle, IDX_STATE_STREAM_HAS_OUTBOUND);
  }

  /** @type {boolean} */
  get hasReader() {
    if (this.#handle.byteLength === 0) return undefined;
    return !!DataViewPrototypeGetUint8(this.#handle, IDX_STATE_STREAM_HAS_READER);
  }

  /** @type {boolean} */
  get wantsBlock() {
    if (this.#handle.byteLength === 0) return undefined;
    return !!DataViewPrototypeGetUint8(this.#handle, IDX_STATE_STREAM_WANTS_BLOCK);
  }

  /** @type {boolean} */
  set wantsBlock(val) {
    if (this.#handle.byteLength === 0) return;
    DataViewPrototypeSetUint8(this.#handle, IDX_STATE_STREAM_WANTS_BLOCK, val ? 1 : 0);
  }

  /** @type {boolean} */
  get [kWantsHeaders]() {
    if (this.#handle.byteLength === 0) return undefined;
    return !!DataViewPrototypeGetUint8(this.#handle, IDX_STATE_STREAM_WANTS_HEADERS);
  }

  /** @type {boolean} */
  set [kWantsHeaders](val) {
    if (this.#handle.byteLength === 0) return;
    DataViewPrototypeSetUint8(this.#handle, IDX_STATE_STREAM_WANTS_HEADERS, val ? 1 : 0);
  }

  /** @type {boolean} */
  get wantsReset() {
    if (this.#handle.byteLength === 0) return undefined;
    return !!DataViewPrototypeGetUint8(this.#handle, IDX_STATE_STREAM_WANTS_RESET);
  }

  /** @type {boolean} */
  set wantsReset(val) {
    if (this.#handle.byteLength === 0) return;
    DataViewPrototypeSetUint8(this.#handle, IDX_STATE_STREAM_WANTS_RESET, val ? 1 : 0);
  }

  /** @type {boolean} */
  get [kWantsTrailers]() {
    if (this.#handle.byteLength === 0) return undefined;
    return !!DataViewPrototypeGetUint8(this.#handle, IDX_STATE_STREAM_WANTS_TRAILERS);
  }

  /** @type {boolean} */
  set [kWantsTrailers](val) {
    if (this.#handle.byteLength === 0) return;
    DataViewPrototypeSetUint8(this.#handle, IDX_STATE_STREAM_WANTS_TRAILERS, val ? 1 : 0);
  }

  toString() {
    return JSONStringify(this.toJSON());
  }

  toJSON() {
    if (this.#handle.byteLength === 0) return {};
    return {
      __proto__: null,
      id: `${this.id}`,
      pending: this.pending,
      finSent: this.finSent,
      finReceived: this.finReceived,
      readEnded: this.readEnded,
      writeEnded: this.writeEnded,
      paused: this.paused,
      reset: this.reset,
      hasOutbound: this.hasOutbound,
      hasReader: this.hasReader,
      wantsBlock: this.wantsBlock,
      wantsReset: this.wantsReset,
    };
  }

  [kInspect](depth, options) {
    if (depth < 0)
      return this;

    if (this.#handle.byteLength === 0) {
      return 'QuicStreamState { <Closed> }';
    }

    const opts = {
      ...options,
      depth: options.depth == null ? null : options.depth - 1,
    };

    return `QuicStreamState ${inspect({
      id: this.id,
      pending: this.pending,
      finSent: this.finSent,
      finReceived: this.finReceived,
      readEnded: this.readEnded,
      writeEnded: this.writeEnded,
      paused: this.paused,
      reset: this.reset,
      hasOutbound: this.hasOutbound,
      hasReader: this.hasReader,
      wantsBlock: this.wantsBlock,
      wantsReset: this.wantsReset,
    }, opts)}`;
  }

  [kFinishClose]() {
    // Snapshot the state into a new DataView since the underlying
    // buffer will be destroyed.
    if (this.#handle.byteLength === 0) return;
    this.#handle = new DataView(new ArrayBuffer(0));
  }
}

module.exports = {
  QuicEndpointState,
  QuicSessionState,
  QuicStreamState,
};
