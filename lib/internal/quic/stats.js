'use strict';

const {
  BigUint64Array,
  JSONStringify,
} = primordials;

const {
  isArrayBuffer,
} = require('util/types');

const {
  codes: {
    ERR_ILLEGAL_CONSTRUCTOR,
    ERR_INVALID_ARG_TYPE,
  },
} = require('internal/errors');

const { inspect } = require('internal/util/inspect');
const assert = require('internal/assert');

const {
  kFinishClose,
  kInspect,
  kPrivateConstructor,
} = require('internal/quic/symbols');

// This file defines the helper objects for accessing statistics collected
// by various QUIC objects. Each of these wraps a BigUint64Array. Every
// stats object is read-only via the API, and the underlying buffer is
// only updated by the QUIC internals. When the stats object is no longer
// needed, it is closed to prevent further updates to the buffer.

const {
  // All of the IDX_STATS_* constants are the index positions of the stats
  // fields in the relevant BigUint64Array's that underlie the *Stats objects.
  // These are not exposed to end users.
  IDX_STATS_ENDPOINT_CREATED_AT,
  IDX_STATS_ENDPOINT_DESTROYED_AT,
  IDX_STATS_ENDPOINT_BYTES_RECEIVED,
  IDX_STATS_ENDPOINT_BYTES_SENT,
  IDX_STATS_ENDPOINT_PACKETS_RECEIVED,
  IDX_STATS_ENDPOINT_PACKETS_SENT,
  IDX_STATS_ENDPOINT_SERVER_SESSIONS,
  IDX_STATS_ENDPOINT_CLIENT_SESSIONS,
  IDX_STATS_ENDPOINT_SERVER_BUSY_COUNT,
  IDX_STATS_ENDPOINT_RETRY_COUNT,
  IDX_STATS_ENDPOINT_VERSION_NEGOTIATION_COUNT,
  IDX_STATS_ENDPOINT_STATELESS_RESET_COUNT,
  IDX_STATS_ENDPOINT_IMMEDIATE_CLOSE_COUNT,

  IDX_STATS_SESSION_CREATED_AT,
  IDX_STATS_SESSION_CLOSING_AT,
  IDX_STATS_SESSION_HANDSHAKE_COMPLETED_AT,
  IDX_STATS_SESSION_HANDSHAKE_CONFIRMED_AT,
  IDX_STATS_SESSION_BYTES_RECEIVED,
  IDX_STATS_SESSION_BYTES_SENT,
  IDX_STATS_SESSION_BIDI_IN_STREAM_COUNT,
  IDX_STATS_SESSION_BIDI_OUT_STREAM_COUNT,
  IDX_STATS_SESSION_UNI_IN_STREAM_COUNT,
  IDX_STATS_SESSION_UNI_OUT_STREAM_COUNT,
  IDX_STATS_SESSION_MAX_BYTES_IN_FLIGHT,
  IDX_STATS_SESSION_BYTES_IN_FLIGHT,
  IDX_STATS_SESSION_BLOCK_COUNT,
  IDX_STATS_SESSION_CWND,
  IDX_STATS_SESSION_LATEST_RTT,
  IDX_STATS_SESSION_MIN_RTT,
  IDX_STATS_SESSION_RTTVAR,
  IDX_STATS_SESSION_SMOOTHED_RTT,
  IDX_STATS_SESSION_SSTHRESH,
  IDX_STATS_SESSION_DATAGRAMS_RECEIVED,
  IDX_STATS_SESSION_DATAGRAMS_SENT,
  IDX_STATS_SESSION_DATAGRAMS_ACKNOWLEDGED,
  IDX_STATS_SESSION_DATAGRAMS_LOST,

  IDX_STATS_STREAM_CREATED_AT,
  IDX_STATS_STREAM_OPENED_AT,
  IDX_STATS_STREAM_RECEIVED_AT,
  IDX_STATS_STREAM_ACKED_AT,
  IDX_STATS_STREAM_DESTROYED_AT,
  IDX_STATS_STREAM_BYTES_RECEIVED,
  IDX_STATS_STREAM_BYTES_SENT,
  IDX_STATS_STREAM_MAX_OFFSET,
  IDX_STATS_STREAM_MAX_OFFSET_ACK,
  IDX_STATS_STREAM_MAX_OFFSET_RECV,
  IDX_STATS_STREAM_FINAL_SIZE,
} = internalBinding('quic');

assert(IDX_STATS_ENDPOINT_CREATED_AT !== undefined);
assert(IDX_STATS_ENDPOINT_DESTROYED_AT !== undefined);
assert(IDX_STATS_ENDPOINT_BYTES_RECEIVED !== undefined);
assert(IDX_STATS_ENDPOINT_BYTES_SENT !== undefined);
assert(IDX_STATS_ENDPOINT_PACKETS_RECEIVED !== undefined);
assert(IDX_STATS_ENDPOINT_PACKETS_SENT !== undefined);
assert(IDX_STATS_ENDPOINT_SERVER_SESSIONS !== undefined);
assert(IDX_STATS_ENDPOINT_CLIENT_SESSIONS !== undefined);
assert(IDX_STATS_ENDPOINT_SERVER_BUSY_COUNT !== undefined);
assert(IDX_STATS_ENDPOINT_RETRY_COUNT !== undefined);
assert(IDX_STATS_ENDPOINT_VERSION_NEGOTIATION_COUNT !== undefined);
assert(IDX_STATS_ENDPOINT_STATELESS_RESET_COUNT !== undefined);
assert(IDX_STATS_ENDPOINT_IMMEDIATE_CLOSE_COUNT !== undefined);
assert(IDX_STATS_SESSION_CREATED_AT !== undefined);
assert(IDX_STATS_SESSION_CLOSING_AT !== undefined);
assert(IDX_STATS_SESSION_HANDSHAKE_COMPLETED_AT !== undefined);
assert(IDX_STATS_SESSION_HANDSHAKE_CONFIRMED_AT !== undefined);
assert(IDX_STATS_SESSION_BYTES_RECEIVED !== undefined);
assert(IDX_STATS_SESSION_BYTES_SENT !== undefined);
assert(IDX_STATS_SESSION_BIDI_IN_STREAM_COUNT !== undefined);
assert(IDX_STATS_SESSION_BIDI_OUT_STREAM_COUNT !== undefined);
assert(IDX_STATS_SESSION_UNI_IN_STREAM_COUNT !== undefined);
assert(IDX_STATS_SESSION_UNI_OUT_STREAM_COUNT !== undefined);
assert(IDX_STATS_SESSION_MAX_BYTES_IN_FLIGHT !== undefined);
assert(IDX_STATS_SESSION_BYTES_IN_FLIGHT !== undefined);
assert(IDX_STATS_SESSION_BLOCK_COUNT !== undefined);
assert(IDX_STATS_SESSION_CWND !== undefined);
assert(IDX_STATS_SESSION_LATEST_RTT !== undefined);
assert(IDX_STATS_SESSION_MIN_RTT !== undefined);
assert(IDX_STATS_SESSION_RTTVAR !== undefined);
assert(IDX_STATS_SESSION_SMOOTHED_RTT !== undefined);
assert(IDX_STATS_SESSION_SSTHRESH !== undefined);
assert(IDX_STATS_SESSION_DATAGRAMS_RECEIVED !== undefined);
assert(IDX_STATS_SESSION_DATAGRAMS_SENT !== undefined);
assert(IDX_STATS_SESSION_DATAGRAMS_ACKNOWLEDGED !== undefined);
assert(IDX_STATS_SESSION_DATAGRAMS_LOST !== undefined);
assert(IDX_STATS_STREAM_CREATED_AT !== undefined);
assert(IDX_STATS_STREAM_OPENED_AT !== undefined);
assert(IDX_STATS_STREAM_RECEIVED_AT !== undefined);
assert(IDX_STATS_STREAM_ACKED_AT !== undefined);
assert(IDX_STATS_STREAM_DESTROYED_AT !== undefined);
assert(IDX_STATS_STREAM_BYTES_RECEIVED !== undefined);
assert(IDX_STATS_STREAM_BYTES_SENT !== undefined);
assert(IDX_STATS_STREAM_MAX_OFFSET !== undefined);
assert(IDX_STATS_STREAM_MAX_OFFSET_ACK !== undefined);
assert(IDX_STATS_STREAM_MAX_OFFSET_RECV !== undefined);
assert(IDX_STATS_STREAM_FINAL_SIZE !== undefined);

class QuicEndpointStats {
  /** @type {BigUint64Array} */
  #handle;
  /** @type {boolean} */
  #disconnected = false;

  /**
   * @param {symbol} privateSymbol
   * @param {ArrayBuffer} buffer
   */
  constructor(privateSymbol, buffer) {
    // We use the kPrivateConstructor symbol to restrict the ability to
    // create new instances of QuicEndpointStats to internal code.
    if (privateSymbol !== kPrivateConstructor) {
      throw new ERR_ILLEGAL_CONSTRUCTOR();
    }
    if (!isArrayBuffer(buffer)) {
      throw new ERR_INVALID_ARG_TYPE('buffer', ['ArrayBuffer'], buffer);
    }
    this.#handle = new BigUint64Array(buffer);
  }

  /** @type {bigint} */
  get createdAt() {
    return this.#handle[IDX_STATS_ENDPOINT_CREATED_AT];
  }

  /** @type {bigint} */
  get destroyedAt() {
    return this.#handle[IDX_STATS_ENDPOINT_DESTROYED_AT];
  }

  /** @type {bigint} */
  get bytesReceived() {
    return this.#handle[IDX_STATS_ENDPOINT_BYTES_RECEIVED];
  }

  /** @type {bigint} */
  get bytesSent() {
    return this.#handle[IDX_STATS_ENDPOINT_BYTES_SENT];
  }

  /** @type {bigint} */
  get packetsReceived() {
    return this.#handle[IDX_STATS_ENDPOINT_PACKETS_RECEIVED];
  }

  /** @type {bigint} */
  get packetsSent() {
    return this.#handle[IDX_STATS_ENDPOINT_PACKETS_SENT];
  }

  /** @type {bigint} */
  get serverSessions() {
    return this.#handle[IDX_STATS_ENDPOINT_SERVER_SESSIONS];
  }

  /** @type {bigint} */
  get clientSessions() {
    return this.#handle[IDX_STATS_ENDPOINT_CLIENT_SESSIONS];
  }

  /** @type {bigint} */
  get serverBusyCount() {
    return this.#handle[IDX_STATS_ENDPOINT_SERVER_BUSY_COUNT];
  }

  /** @type {bigint} */
  get retryCount() {
    return this.#handle[IDX_STATS_ENDPOINT_RETRY_COUNT];
  }

  /** @type {bigint} */
  get versionNegotiationCount() {
    return this.#handle[IDX_STATS_ENDPOINT_VERSION_NEGOTIATION_COUNT];
  }

  /** @type {bigint} */
  get statelessResetCount() {
    return this.#handle[IDX_STATS_ENDPOINT_STATELESS_RESET_COUNT];
  }

  /** @type {bigint} */
  get immediateCloseCount() {
    return this.#handle[IDX_STATS_ENDPOINT_IMMEDIATE_CLOSE_COUNT];
  }

  toString() {
    return JSONStringify(this.toJSON());
  }

  toJSON() {
    return {
      __proto__: null,
      connected: this.isConnected,
      // We need to convert the values to strings because JSON does not
      // support BigInts.
      createdAt: `${this.createdAt}`,
      destroyedAt: `${this.destroyedAt}`,
      bytesReceived: `${this.bytesReceived}`,
      bytesSent: `${this.bytesSent}`,
      packetsReceived: `${this.packetsReceived}`,
      packetsSent: `${this.packetsSent}`,
      serverSessions: `${this.serverSessions}`,
      clientSessions: `${this.clientSessions}`,
      serverBusyCount: `${this.serverBusyCount}`,
      retryCount: `${this.retryCount}`,
      versionNegotiationCount: `${this.versionNegotiationCount}`,
      statelessResetCount: `${this.statelessResetCount}`,
      immediateCloseCount: `${this.immediateCloseCount}`,
    };
  }

  [kInspect](depth, options) {
    if (depth < 0)
      return this;

    const opts = {
      ...options,
      depth: options.depth == null ? null : options.depth - 1,
    };

    return `QuicEndpointStats ${inspect({
      connected: this.isConnected,
      createdAt: this.createdAt,
      destroyedAt: this.destroyedAt,
      bytesReceived: this.bytesReceived,
      bytesSent: this.bytesSent,
      packetsReceived: this.packetsReceived,
      packetsSent: this.packetsSent,
      serverSessions: this.serverSessions,
      clientSessions: this.clientSessions,
      serverBusyCount: this.serverBusyCount,
      retryCount: this.retryCount,
      versionNegotiationCount: this.versionNegotiationCount,
      statelessResetCount: this.statelessResetCount,
      immediateCloseCount: this.immediateCloseCount,
    }, opts)}`;
  }

  /**
   * True if this QuicEndpointStats object is still connected to the underlying
   * Endpoint stats source. If this returns false, then the stats object is
   * no longer being updated and should be considered stale.
   * @returns {boolean}
   */
  get isConnected() {
    return !this.#disconnected;
  }

  [kFinishClose]() {
    // Snapshot the stats into a new BigUint64Array since the underlying
    // buffer will be destroyed.
    this.#handle = new BigUint64Array(this.#handle);
    this.#disconnected = true;
  }
}

class QuicSessionStats {
  /** @type {BigUint64Array} */
  #handle;
  /** @type {boolean} */
  #disconnected = false;

  /**
   * @param {symbol} privateSynbol
   * @param {BigUint64Array} buffer
   */
  constructor(privateSynbol, buffer) {
    // We use the kPrivateConstructor symbol to restrict the ability to
    // create new instances of QuicSessionStats to internal code.
    if (privateSynbol !== kPrivateConstructor) {
      throw new ERR_ILLEGAL_CONSTRUCTOR();
    }
    if (!isArrayBuffer(buffer)) {
      throw new ERR_INVALID_ARG_TYPE('buffer', ['ArrayBuffer'], buffer);
    }
    this.#handle = new BigUint64Array(buffer);
  }

  /** @type {bigint} */
  get createdAt() {
    return this.#handle[IDX_STATS_SESSION_CREATED_AT];
  }

  /** @type {bigint} */
  get closingAt() {
    return this.#handle[IDX_STATS_SESSION_CLOSING_AT];
  }

  /** @type {bigint} */
  get handshakeCompletedAt() {
    return this.#handle[IDX_STATS_SESSION_HANDSHAKE_COMPLETED_AT];
  }

  /** @type {bigint} */
  get handshakeConfirmedAt() {
    return this.#handle[IDX_STATS_SESSION_HANDSHAKE_CONFIRMED_AT];
  }

  /** @type {bigint} */
  get bytesReceived() {
    return this.#handle[IDX_STATS_SESSION_BYTES_RECEIVED];
  }

  /** @type {bigint} */
  get bytesSent() {
    return this.#handle[IDX_STATS_SESSION_BYTES_SENT];
  }

  /** @type {bigint} */
  get bidiInStreamCount() {
    return this.#handle[IDX_STATS_SESSION_BIDI_IN_STREAM_COUNT];
  }

  /** @type {bigint} */
  get bidiOutStreamCount() {
    return this.#handle[IDX_STATS_SESSION_BIDI_OUT_STREAM_COUNT];
  }

  /** @type {bigint} */
  get uniInStreamCount() {
    return this.#handle[IDX_STATS_SESSION_UNI_IN_STREAM_COUNT];
  }

  /** @type {bigint} */
  get uniOutStreamCount() {
    return this.#handle[IDX_STATS_SESSION_UNI_OUT_STREAM_COUNT];
  }

  /** @type {bigint} */
  get maxBytesInFlights() {
    return this.#handle[IDX_STATS_SESSION_MAX_BYTES_IN_FLIGHT];
  }

  /** @type {bigint} */
  get bytesInFlight() {
    return this.#handle[IDX_STATS_SESSION_BYTES_IN_FLIGHT];
  }

  /** @type {bigint} */
  get blockCount() {
    return this.#handle[IDX_STATS_SESSION_BLOCK_COUNT];
  }

  /** @type {bigint} */
  get cwnd() {
    return this.#handle[IDX_STATS_SESSION_CWND];
  }

  /** @type {bigint} */
  get latestRtt() {
    return this.#handle[IDX_STATS_SESSION_LATEST_RTT];
  }

  /** @type {bigint} */
  get minRtt() {
    return this.#handle[IDX_STATS_SESSION_MIN_RTT];
  }

  /** @type {bigint} */
  get rttVar() {
    return this.#handle[IDX_STATS_SESSION_RTTVAR];
  }

  /** @type {bigint} */
  get smoothedRtt() {
    return this.#handle[IDX_STATS_SESSION_SMOOTHED_RTT];
  }

  /** @type {bigint} */
  get ssthresh() {
    return this.#handle[IDX_STATS_SESSION_SSTHRESH];
  }

  /** @type {bigint} */
  get datagramsReceived() {
    return this.#handle[IDX_STATS_SESSION_DATAGRAMS_RECEIVED];
  }

  /** @type {bigint} */
  get datagramsSent() {
    return this.#handle[IDX_STATS_SESSION_DATAGRAMS_SENT];
  }

  /** @type {bigint} */
  get datagramsAcknowledged() {
    return this.#handle[IDX_STATS_SESSION_DATAGRAMS_ACKNOWLEDGED];
  }

  /** @type {bigint} */
  get datagramsLost() {
    return this.#handle[IDX_STATS_SESSION_DATAGRAMS_LOST];
  }

  toString() {
    return JSONStringify(this.toJSON());
  }

  toJSON() {
    return {
      __proto__: null,
      connected: this.isConnected,
      // We need to convert the values to strings because JSON does not
      // support BigInts.
      createdAt: `${this.createdAt}`,
      closingAt: `${this.closingAt}`,
      destroyedAt: `${this.destroyedAt}`,
      handshakeCompletedAt: `${this.handshakeCompletedAt}`,
      handshakeConfirmedAt: `${this.handshakeConfirmedAt}`,
      gracefulClosingAt: `${this.gracefulClosingAt}`,
      bytesReceived: `${this.bytesReceived}`,
      bytesSent: `${this.bytesSent}`,
      bidiInStreamCount: `${this.bidiInStreamCount}`,
      bidiOutStreamCount: `${this.bidiOutStreamCount}`,
      uniInStreamCount: `${this.uniInStreamCount}`,
      uniOutStreamCount: `${this.uniOutStreamCount}`,
      maxBytesInFlights: `${this.maxBytesInFlights}`,
      bytesInFlight: `${this.bytesInFlight}`,
      blockCount: `${this.blockCount}`,
      cwnd: `${this.cwnd}`,
      latestRtt: `${this.latestRtt}`,
      minRtt: `${this.minRtt}`,
      rttVar: `${this.rttVar}`,
      smoothedRtt: `${this.smoothedRtt}`,
      ssthresh: `${this.ssthresh}`,
      datagramsReceived: `${this.datagramsReceived}`,
      datagramsSent: `${this.datagramsSent}`,
      datagramsAcknowledged: `${this.datagramsAcknowledged}`,
      datagramsLost: `${this.datagramsLost}`,
    };
  }

  [kInspect](depth, options) {
    if (depth < 0)
      return this;

    const opts = {
      ...options,
      depth: options.depth == null ? null : options.depth - 1,
    };

    return `QuicSessionStats ${inspect({
      connected: this.isConnected,
      createdAt: this.createdAt,
      closingAt: this.closingAt,
      destroyedAt: this.destroyedAt,
      handshakeCompletedAt: this.handshakeCompletedAt,
      handshakeConfirmedAt: this.handshakeConfirmedAt,
      gracefulClosingAt: this.gracefulClosingAt,
      bytesReceived: this.bytesReceived,
      bytesSent: this.bytesSent,
      bidiInStreamCount: this.bidiInStreamCount,
      bidiOutStreamCount: this.bidiOutStreamCount,
      uniInStreamCount: this.uniInStreamCount,
      uniOutStreamCount: this.uniOutStreamCount,
      maxBytesInFlights: this.maxBytesInFlights,
      bytesInFlight: this.bytesInFlight,
      blockCount: this.blockCount,
      cwnd: this.cwnd,
      latestRtt: this.latestRtt,
      minRtt: this.minRtt,
      rttVar: this.rttVar,
      smoothedRtt: this.smoothedRtt,
      ssthresh: this.ssthresh,
      datagramsReceived: this.datagramsReceived,
      datagramsSent: this.datagramsSent,
      datagramsAcknowledged: this.datagramsAcknowledged,
      datagramsLost: this.datagramsLost,
    }, opts)}`;
  }

  /**
   * True if this QuicSessionStats object is still connected to the underlying
   * Session stats source. If this returns false, then the stats object is
   * no longer being updated and should be considered stale.
   * @returns {boolean}
   */
  get isConnected() {
    return !this.#disconnected;
  }

  [kFinishClose]() {
    // Snapshot the stats into a new BigUint64Array since the underlying
    // buffer will be destroyed.
    this.#handle = new BigUint64Array(this.#handle);
    this.#disconnected = true;
  }
}

class QuicStreamStats {
  /** @type {BigUint64Array} */
  #handle;
  /** type {boolean} */
  #disconnected = false;

  /**
   * @param {symbol} privateSymbol
   * @param {ArrayBuffer} buffer
   */
  constructor(privateSymbol, buffer) {
    // We use the kPrivateConstructor symbol to restrict the ability to
    // create new instances of QuicStreamStats to internal code.
    if (privateSymbol !== kPrivateConstructor) {
      throw new ERR_ILLEGAL_CONSTRUCTOR();
    }
    if (!isArrayBuffer(buffer)) {
      throw new ERR_INVALID_ARG_TYPE('buffer', ['ArrayBuffer'], buffer);
    }
    this.#handle = new BigUint64Array(buffer);
  }

  /** @type {bigint} */
  get createdAt() {
    return this.#handle[IDX_STATS_STREAM_CREATED_AT];
  }

  /** @type {bigint} */
  get openedAt() {
    return this.#handle[IDX_STATS_STREAM_OPENED_AT];
  }

  /** @type {bigint} */
  get receivedAt() {
    return this.#handle[IDX_STATS_STREAM_RECEIVED_AT];
  }

  /** @type {bigint} */
  get ackedAt() {
    return this.#handle[IDX_STATS_STREAM_ACKED_AT];
  }

  /** @type {bigint} */
  get destroyedAt() {
    return this.#handle[IDX_STATS_STREAM_DESTROYED_AT];
  }

  /** @type {bigint} */
  get bytesReceived() {
    return this.#handle[IDX_STATS_STREAM_BYTES_RECEIVED];
  }

  /** @type {bigint} */
  get bytesSent() {
    return this.#handle[IDX_STATS_STREAM_BYTES_SENT];
  }

  /** @type {bigint} */
  get maxOffset() {
    return this.#handle[IDX_STATS_STREAM_MAX_OFFSET];
  }

  /** @type {bigint} */
  get maxOffsetAcknowledged() {
    return this.#handle[IDX_STATS_STREAM_MAX_OFFSET_ACK];
  }

  /** @type {bigint} */
  get maxOffsetReceived() {
    return this.#handle[IDX_STATS_STREAM_MAX_OFFSET_RECV];
  }

  /** @type {bigint} */
  get finalSize() {
    return this.#handle[IDX_STATS_STREAM_FINAL_SIZE];
  }

  toString() {
    return JSONStringify(this.toJSON());
  }

  toJSON() {
    return {
      __proto__: null,
      connected: this.isConnected,
      // We need to convert the values to strings because JSON does not
      // support BigInts.
      createdAt: `${this.createdAt}`,
      openedAt: `${this.openedAt}`,
      receivedAt: `${this.receivedAt}`,
      ackedAt: `${this.ackedAt}`,
      destroyedAt: `${this.destroyedAt}`,
      bytesReceived: `${this.bytesReceived}`,
      bytesSent: `${this.bytesSent}`,
      maxOffset: `${this.maxOffset}`,
      maxOffsetAcknowledged: `${this.maxOffsetAcknowledged}`,
      maxOffsetReceived: `${this.maxOffsetReceived}`,
      finalSize: `${this.finalSize}`,
    };
  }

  [kInspect](depth, options) {
    if (depth < 0)
      return this;

    const opts = {
      ...options,
      depth: options.depth == null ? null : options.depth - 1,
    };

    return `StreamStats ${inspect({
      connected: this.isConnected,
      createdAt: this.createdAt,
      openedAt: this.openedAt,
      receivedAt: this.receivedAt,
      ackedAt: this.ackedAt,
      destroyedAt: this.destroyedAt,
      bytesReceived: this.bytesReceived,
      bytesSent: this.bytesSent,
      maxOffset: this.maxOffset,
      maxOffsetAcknowledged: this.maxOffsetAcknowledged,
      maxOffsetReceived: this.maxOffsetReceived,
      finalSize: this.finalSize,
    }, opts)}`;
  }

  /**
   * True if this QuicStreamStats object is still connected to the underlying
   * Stream stats source. If this returns false, then the stats object is
   * no longer being updated and should be considered stale.
   * @returns {boolean}
   */
  get isConnected() {
    return !this.#disconnected;
  }

  [kFinishClose]() {
    // Snapshot the stats into a new BigUint64Array since the underlying
    // buffer will be destroyed.
    this.#handle = new BigUint64Array(this.#handle);
    this.#disconnected = true;
  }
}

module.exports = {
  QuicEndpointStats,
  QuicSessionStats,
  QuicStreamStats,
};
