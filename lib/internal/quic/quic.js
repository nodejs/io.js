'use strict';

// TODO(@jasnell) Temporarily ignoring c8 covrerage for this file while tests
// are still being developed.
/* c8 ignore start */

const {
  ArrayBuffer,
  ArrayIsArray,
  ArrayPrototypePush,
  BigUint64Array,
  DataView,
  DataViewPrototypeGetBigInt64,
  DataViewPrototypeGetBigUint64,
  DataViewPrototypeGetUint8,
  DataViewPrototypeSetUint8,
  ObjectDefineProperties,
  Symbol,
  SymbolAsyncDispose,
  Uint8Array,
} = primordials;

const {
  assertCrypto,
  customInspectSymbol: kInspect,
} = require('internal/util');
const { inspect } = require('internal/util/inspect');
assertCrypto();

const {
  Endpoint: Endpoint_,
  setCallbacks,

  // The constants to be exposed to end users for various options.
  CC_ALGO_RENO,
  CC_ALGO_CUBIC,
  CC_ALGO_BBR,
  CC_ALGO_RENO_STR,
  CC_ALGO_CUBIC_STR,
  CC_ALGO_BBR_STR,
  PREFERRED_ADDRESS_IGNORE,
  PREFERRED_ADDRESS_USE,
  DEFAULT_PREFERRED_ADDRESS_POLICY,
  DEFAULT_CIPHERS,
  DEFAULT_GROUPS,
  STREAM_DIRECTION_BIDIRECTIONAL,
  STREAM_DIRECTION_UNIDIRECTIONAL,

  // Internal constants for use by the implementation.
  // These are not exposed to end users.
  CLOSECONTEXT_CLOSE: kCloseContextClose,
  CLOSECONTEXT_BIND_FAILURE: kCloseContextBindFailure,
  CLOSECONTEXT_LISTEN_FAILURE: kCloseContextListenFailure,
  CLOSECONTEXT_RECEIVE_FAILURE: kCloseContextReceiveFailure,
  CLOSECONTEXT_SEND_FAILURE: kCloseContextSendFailure,
  CLOSECONTEXT_START_FAILURE: kCloseContextStartFailure,

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
  IDX_STATS_SESSION_DESTROYED_AT,
  IDX_STATS_SESSION_HANDSHAKE_COMPLETED_AT,
  IDX_STATS_SESSION_HANDSHAKE_CONFIRMED_AT,
  IDX_STATS_SESSION_GRACEFUL_CLOSING_AT,
  IDX_STATS_SESSION_BYTES_RECEIVED,
  IDX_STATS_SESSION_BYTES_SENT,
  IDX_STATS_SESSION_BIDI_IN_STREAM_COUNT,
  IDX_STATS_SESSION_BIDI_OUT_STREAM_COUNT,
  IDX_STATS_SESSION_UNI_IN_STREAM_COUNT,
  IDX_STATS_SESSION_UNI_OUT_STREAM_COUNT,
  IDX_STATS_SESSION_LOSS_RETRANSMIT_COUNT,
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
  IDX_STATS_STREAM_RECEIVED_AT,
  IDX_STATS_STREAM_ACKED_AT,
  IDX_STATS_STREAM_CLOSING_AT,
  IDX_STATS_STREAM_DESTROYED_AT,
  IDX_STATS_STREAM_BYTES_RECEIVED,
  IDX_STATS_STREAM_BYTES_SENT,
  IDX_STATS_STREAM_MAX_OFFSET,
  IDX_STATS_STREAM_MAX_OFFSET_ACK,
  IDX_STATS_STREAM_MAX_OFFSET_RECV,
  IDX_STATS_STREAM_FINAL_SIZE,

  IDX_STATE_SESSION_PATH_VALIDATION,
  IDX_STATE_SESSION_VERSION_NEGOTIATION,
  IDX_STATE_SESSION_DATAGRAM,
  IDX_STATE_SESSION_SESSION_TICKET,
  IDX_STATE_SESSION_CLOSING,
  IDX_STATE_SESSION_GRACEFUL_CLOSE,
  IDX_STATE_SESSION_SILENT_CLOSE,
  IDX_STATE_SESSION_STATELESS_RESET,
  IDX_STATE_SESSION_DESTROYED,
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
  IDX_STATE_STREAM_FIN_SENT,
  IDX_STATE_STREAM_FIN_RECEIVED,
  IDX_STATE_STREAM_READ_ENDED,
  IDX_STATE_STREAM_WRITE_ENDED,
  IDX_STATE_STREAM_DESTROYED,
  IDX_STATE_STREAM_PAUSED,
  IDX_STATE_STREAM_RESET,
  IDX_STATE_STREAM_HAS_READER,
  IDX_STATE_STREAM_WANTS_BLOCK,
  IDX_STATE_STREAM_WANTS_HEADERS,
  IDX_STATE_STREAM_WANTS_RESET,
  IDX_STATE_STREAM_WANTS_TRAILERS,
} = internalBinding('quic');

const {
  isArrayBuffer,
  isArrayBufferView,
} = require('util/types');

const {
  Buffer,
} = require('buffer');

const {
  codes: {
    ERR_INVALID_ARG_TYPE,
    ERR_INVALID_ARG_VALUE,
    ERR_INVALID_STATE,
    ERR_QUIC_CONNECTION_FAILED,
    ERR_QUIC_ENDPOINT_CLOSED,
    ERR_QUIC_OPEN_STREAM_FAILED,
  },
} = require('internal/errors');

const {
  InternalSocketAddress,
  SocketAddress,
  kHandle: kSocketAddressHandle,
} = require('internal/socketaddress');

const {
  isKeyObject,
  isCryptoKey,
} = require('internal/crypto/keys');

const {
  kHandle: kKeyObjectHandle,
  kKeyObject: kKeyObjectInner,
} = require('internal/crypto/util');

const {
  validateFunction,
  validateObject,
} = require('internal/validators');

const kEmptyObject = { __proto__: null };
const kEnumerable = { __proto__: null, enumerable: true };

const kBlocked = Symbol('kBlocked');
const kDatagram = Symbol('kDatagram');
const kDatagramStatus = Symbol('kDatagramStatus');
const kError = Symbol('kError');
const kFinishClose = Symbol('kFinishClose');
const kHandshake = Symbol('kHandshake');
const kHeaders = Symbol('kHeaders');
const kOwner = Symbol('kOwner');
const kNewSession = Symbol('kNewSession');
const kNewStream = Symbol('kNewStream');
const kPathValidation = Symbol('kPathValidation');
const kReset = Symbol('kReset');
const kSessionTicket = Symbol('kSessionTicket');
const kTrailers = Symbol('kTrailers');
const kVersionNegotiation = Symbol('kVersionNegotiation');

/**
 * @typedef {import('../socketaddress.js').SocketAddress} SocketAddress
 * @typedef {import('../crypto/keys.js').KeyObject} KeyObject
 * @typedef {import('../crypto/keys.js').CryptoKey} CryptoKey
 */

/**
 * @typedef {object} EndpointOptions
 * @property {SocketAddress} [address] The local address to bind to
 * @property {bigint|number} [retryTokenExpiration] The retry token expiration
 * @property {bigint|number} [tokenExpiration] The token expiration
 * @property {bigint|number} [maxConnectionsPerHost] The maximum number of connections per host
 * @property {bigint|number} [maxConnectionsTotal] The maximum number of total connections
 * @property {bigint|number} [maxStatelessResetsPerHost] The maximum number of stateless resets per host
 * @property {bigint|number} [addressLRUSize] The size of the address LRU cache
 * @property {bigint|number} [maxRetries] The maximum number of retries
 * @property {bigint|number} [maxPayloadSize] The maximum payload size
 * @property {bigint|number} [unacknowledgedPacketThreshold] The unacknowledged packet threshold
 * @property {bigint|number} [handshakeTimeout] The handshake timeout
 * @property {bigint|number} [maxStreamWindow] The maximum stream window
 * @property {bigint|number} [maxWindow] The maximum window
 * @property {number} [rxDiagnosticLoss] The receive diagnostic loss  (range 0.0-1.0)
 * @property {number} [txDiagnosticLoss] The transmit diagnostic loss (range 0.0-1.0)
 * @property {number} [udpReceiveBufferSize] The UDP receive buffer size
 * @property {number} [udpSendBufferSize] The UDP send buffer size
 * @property {number} [udpTTL] The UDP TTL
 * @property {boolean} [noUdpPayloadSizeShaping] Disable UDP payload size shaping
 * @property {boolean} [validateAddress] Validate the address
 * @property {boolean} [disableActiveMigration] Disable active migration
 * @property {boolean} [ipv6Only] Use IPv6 only
 * @property {'reno'|'cubic'|'bbr'|number} [cc] The congestion control algorithm
 * @property {ArrayBufferView} [resetTokenSecret] The reset token secret
 * @property {ArrayBufferView} [tokenSecret] The token secret
 */

/**
 * @typedef {object} TlsOptions
 * @property {string} [sni] The server name indication
 * @property {string} [alpn] The application layer protocol negotiation
 * @property {string} [ciphers] The ciphers
 * @property {string} [groups] The groups
 * @property {boolean} [keylog] Enable key logging
 * @property {boolean} [verifyClient] Verify the client
 * @property {boolean} [tlsTrace] Enable TLS tracing
 * @property {boolean} [verifyPrivateKey] Verify the private key
 * @property {KeyObject|CryptoKey|Array<KeyObject|CryptoKey>} [keys] The keys
 * @property {ArrayBuffer|ArrayBufferView|Array<ArrayBuffer|ArrayBufferView>} [certs] The certificates
 * @property {ArrayBuffer|ArrayBufferView|Array<ArrayBuffer|ArrayBufferView>} [ca] The certificate authority
 * @property {ArrayBuffer|ArrayBufferView|Array<ArrayBuffer|ArrayBufferView>} [crl] The certificate revocation list
 */

/**
 * @typedef {object} TransportParams
 * @property {SocketAddress} [preferredAddressIpv4] The preferred IPv4 address
 * @property {SocketAddress} [preferredAddressIpv6] The preferred IPv6 address
 * @property {bigint|number} [initialMaxStreamDataBidiLocal] The initial maximum stream data bidirectional local
 * @property {bigint|number} [initialMaxStreamDataBidiRemote] The initial maximum stream data bidirectional remote
 * @property {bigint|number} [initialMaxStreamDataUni] The initial maximum stream data unidirectional
 * @property {bigint|number} [initialMaxData] The initial maximum data
 * @property {bigint|number} [initialMaxStreamsBidi] The initial maximum streams bidirectional
 * @property {bigint|number} [initialMaxStreamsUni] The initial maximum streams unidirectional
 * @property {bigint|number} [maxIdleTimeout] The maximum idle timeout
 * @property {bigint|number} [activeConnectionIDLimit] The active connection ID limit
 * @property {bigint|number} [ackDelayExponent] The acknowledgment delay exponent
 * @property {bigint|number} [maxAckDelay] The maximum acknowledgment delay
 * @property {bigint|number} [maxDatagramFrameSize] The maximum datagram frame size
 * @property {boolean} [disableActiveMigration] Disable active migration
 */

/**
 * @typedef {object} ApplicationOptions
 * @property {bigint|number} [maxHeaderPairs] The maximum header pairs
 * @property {bigint|number} [maxHeaderLength] The maximum header length
 * @property {bigint|number} [maxFieldSectionSize] The maximum field section size
 * @property {bigint|number} [qpackMaxDTableCapacity] The qpack maximum dynamic table capacity
 * @property {bigint|number} [qpackEncoderMaxDTableCapacity] The qpack encoder maximum dynamic table capacity
 * @property {bigint|number} [qpackBlockedStreams] The qpack blocked streams
 * @property {boolean} [enableConnectProtocol] Enable the connect protocol
 * @property {boolean} [enableDatagrams] Enable datagrams
 */

/**
 * @typedef {object} SessionOptions
 * @property {number} [version] The version
 * @property {number} [minVersion] The minimum version
 * @property {'use'|'ignore'|'default'} [preferredAddressPolicy] The preferred address policy
 * @property {ApplicationOptions} [application] The application options
 * @property {TransportParams} [transportParams] The transport parameters
 * @property {TlsOptions} [tls] The TLS options
 * @property {boolean} [qlog] Enable qlog
 * @property {ArrayBufferView} [sessionTicket] The session ticket
 */

/**
 * @typedef {object} Datagrams
 * @property {ReadableStream} readable The readable stream
 * @property {WritableStream} writable The writable stream
 */

/**
 * @typedef {object} Path
 * @property {SocketAddress} local The local address
 * @property {SocketAddress} remote The remote address
 */

/**
 * Called when the Endpoint receives a new server-side Session.
 * @callback OnSessionCallback
 * @param {Session} session
 * @param {Endpoint} endpoint
 * @returns {void}
 */

/**
 * @callback OnStreamCallback
 * @param {QuicStream} stream
 * @param {Session} session
 * @returns {void}
 */

/**
 * @callback OnDatagramCallback
 * @param {Uint8Array} datagram
 * @param {Session} session
 * @param {boolean} early
 * @returns {void}
 */

/**
 * @callback OnDatagramStatusCallback
 * @param {bigint} id
 * @param {'lost'|'acknowledged'} status
 * @param {Session} session
 * @returns {void}
 */

/**
 * @callback OnPathValidationCallback
 * @param {'aborted'|'failure'|'success'} result
 * @param {SocketAddress} newLocalAddress
 * @param {SocketAddress} newRemoteAddress
 * @param {SocketAddress} oldLocalAddress
 * @param {SocketAddress} oldRemoteAddress
 * @param {boolean} preferredAddress
 * @param {Session} session
 * @returns {void}
 */

/**
 * @callback OnSessionTicketCallback
 * @param {object} ticket
 * @param {Session} session
 * @returns {void}
 */

/**
 * @callback OnVersionNegotiationCallback
 * @param {number} version
 * @param {number[]} requestedVersions
 * @param {number[]} supportedVersions
 * @param {Session} session
 * @returns {void}
 */

/**
 * @callback OnHandshakeCallback
 * @param {string} sni
 * @param {string} alpn
 * @param {string} cipher
 * @param {string} cipherVersion
 * @param {string} validationErrorReason
 * @param {number} validationErrorCode
 * @param {boolean} earlyDataAccepted
 * @param {Session} session
 * @returns {void}
 */

/**
 * @callback OnBlockedCallback
 * @param {QuicStream} stream
 * @returns {void}
 */

/**
 * @callback OnStreamErrorCallback
 * @param {any} error
 * @param {QuicStream} stream
 * @returns {void}
 */

/**
 * @callback OnHeadersCallback
 * @param {object} headers
 * @param {string} kind
 * @param {QuicStream} stream
 * @returns {void}
 */

/**
 * @callback OnTrailersCallback
 * @param {QuicStream} stream
 * @returns {void}
 */

/**
 * Provdes the callback configuration for Sessions.
 * @typedef {object} SessionCallbackConfiguration
 * @property {OnStreamCallback} onstream The stream callback
 * @property {OnDatagramCallback} [ondatagram] The datagram callback
 * @property {OnDatagramStatusCallback} [ondatagramstatus] The datagram status callback
 * @property {OnPathValidationCallback} [onpathvalidation] The path validation callback
 * @property {OnSessionTicketCallback} [onsessionticket] The session ticket callback
 * @property {OnVersionNegotiationCallback} [onversionnegotiation] The version negotiation callback
 * @property {OnHandshakeCallback} [onhandshake] The handshake callback
 */

/**
 * @typedef {object} StreamCallbackConfiguration
 * @property {OnStreamErrorCallback} onerror The error callback
 * @property {OnBlockedCallback} [onblocked] The blocked callback
 * @property {OnStreamErrorCallback} [onreset] The reset callback
 * @property {OnHeadersCallback} [onheaders] The headers callback
 * @property {OnTrailersCallback} [ontrailers] The trailers callback
 */

/**
 * Provides the callback configuration for the Endpoint.
 * @typedef {object} EndpointCallbackConfiguration
 * @property {OnSessionCallback} onsession The session callback
 * @property {SessionCallbackConfiguration} session The session callback configuration
 * @property {StreamCallbackConfiguration} stream The stream callback configuration
 */

setCallbacks({
  onEndpointClose(context, status) {
    this[kOwner][kFinishClose](context, status);
  },
  onSessionNew(session) {
    this[kOwner][kNewSession](session);
  },
  onSessionClose(errorType, code, reason) {
    this[kOwner][kFinishClose](errorType, code, reason);
  },
  onSessionDatagram(uint8Array, early) {
    this[kOwner][kDatagram](uint8Array, early);
  },
  onSessionDatagramStatus(id, status) {
    this[kOwner][kDatagramStatus](id, status);
  },
  onSessionHandshake(sni, alpn, cipher, cipherVersion,
                     validationErrorReason,
                     validationErrorCode,
                     earlyDataAccepted) {
    this[kOwner][kHandshake](sni, alpn, cipher, cipherVersion,
                             validationErrorReason, validationErrorCode,
                             earlyDataAccepted);
  },
  onSessionPathValidation(...args) {
    this[kOwner][kPathValidation](...args);
  },
  onSessionTicket(ticket) {
    this[kOwner][kSessionTicket](ticket);
  },
  onSessionVersionNegotiation(version,
                              requestedVersions,
                              supportedVersions) {
    this[kOwner][kVersionNegotiation](version, requestedVersions, supportedVersions);
    // Note that immediately following a version negotiation event, the
    // session will be destroyed.
  },
  onStreamCreated(stream, direction) {
    const session = this[kOwner];
    // The event is ignored if the session has been destroyed.
    if (session.destroyed) {
      stream.destroy();
      return;
    };
    session[kNewStream](stream, direction);
  },
  onStreamBlocked() {
    this[kOwner][kBlocked]();
  },
  onStreamClose(error) {
    this[kOwner][kError](error);
  },
  onStreamReset(error) {
    this[kOwner][kReset](error);
  },
  onStreamHeaders(headers, kind) {
    this[kOwner][kHeaders](headers, kind);
  },
  onStreamTrailers() {
    this[kOwner][kTrailers]();
  },
});

/**
 * @param {'use'|'ignore'|'default'} policy
 * @returns {number}
 */
function getPreferredAddressPolicy(policy = 'default') {
  switch (policy) {
    case 'use': return PREFERRED_ADDRESS_USE;
    case 'ignore': return PREFERRED_ADDRESS_IGNORE;
    case 'default': return DEFAULT_PREFERRED_ADDRESS_POLICY;
  }
  throw new ERR_INVALID_ARG_VALUE('options.preferredAddressPolicy', policy);
}

/**
 * @param {TlsOptions} tls
 */
function processTlsOptions(tls) {
  validateObject(tls, 'options.tls');
  const {
    sni,
    alpn,
    ciphers,
    groups,
    keylog,
    verifyClient,
    tlsTrace,
    verifyPrivateKey,
    keys,
    certs,
    ca,
    crl,
  } = tls;

  const keyHandles = [];
  if (keys !== undefined) {
    const keyInputs = ArrayIsArray(keys) ? keys : [keys];
    for (const key of keyInputs) {
      if (isKeyObject(key)) {
        if (key.type !== 'private') {
          throw new ERR_INVALID_ARG_VALUE('options.tls.keys', key, 'must be a private key');
        }
        ArrayPrototypePush(keyHandles, key[kKeyObjectHandle]);
      } else if (isCryptoKey(key)) {
        if (key.type !== 'private') {
          throw new ERR_INVALID_ARG_VALUE('options.tls.keys', key, 'must be a private key');
        }
        ArrayPrototypePush(keyHandles, key[kKeyObjectInner][kKeyObjectHandle]);
      } else {
        throw new ERR_INVALID_ARG_TYPE('options.tls.keys', ['KeyObject', 'CryptoKey'], key);
      }
    }
  }

  return {
    sni,
    alpn,
    ciphers,
    groups,
    keylog,
    verifyClient,
    tlsTrace,
    verifyPrivateKey,
    keys: keyHandles,
    certs,
    ca,
    crl,
  };
}

class QuicStreamStats {
  /** @type {BigUint64Array} */
  #handle;

  /**
   * @param {ArrayBuffer} buffer
   */
  constructor(buffer) {
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
  get receivedAt() {
    return this.#handle[IDX_STATS_STREAM_RECEIVED_AT];
  }

  /** @type {bigint} */
  get ackedAt() {
    return this.#handle[IDX_STATS_STREAM_ACKED_AT];
  }

  /** @type {bigint} */
  get closingAt() {
    return this.#handle[IDX_STATS_STREAM_CLOSING_AT];
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

  toJSON() {
    return {
      __proto__: null,
      createdAt: `${this.createdAt}`,
      receivedAt: `${this.receivedAt}`,
      ackedAt: `${this.ackedAt}`,
      closingAt: `${this.closingAt}`,
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
      createdAt: this.createdAt,
      receivedAt: this.receivedAt,
      ackedAt: this.ackedAt,
      closingAt: this.closingAt,
      destroyedAt: this.destroyedAt,
      bytesReceived: this.bytesReceived,
      bytesSent: this.bytesSent,
      maxOffset: this.maxOffset,
      maxOffsetAcknowledged: this.maxOffsetAcknowledged,
      maxOffsetReceived: this.maxOffsetReceived,
      finalSize: this.finalSize,
    }, opts)}`;
  }

  [kFinishClose]() {
    // Snapshot the stats into a new BigUint64Array since the underlying
    // buffer will be destroyed.
    this.#handle = new BigUint64Array(this.#handle);
  }
}

class QuicStreamState {
  /** @type {DataView} */
  #handle;

  /**
   * @param {ArrayBuffer} buffer
   */
  constructor(buffer) {
    if (!isArrayBuffer(buffer)) {
      throw new ERR_INVALID_ARG_TYPE('buffer', ['ArrayBuffer'], buffer);
    }
    this.#handle = new DataView(buffer);
  }

  /** @type {bigint} */
  get id() {
    return DataViewPrototypeGetBigInt64(this.#handle, IDX_STATE_STREAM_ID);
  }

  /** @type {boolean} */
  get finSent() {
    return !!DataViewPrototypeGetUint8(this.#handle, IDX_STATE_STREAM_FIN_SENT);
  }

  /** @type {boolean} */
  get finReceived() {
    return !!DataViewPrototypeGetUint8(this.#handle, IDX_STATE_STREAM_FIN_RECEIVED);
  }

  /** @type {boolean} */
  get readEnded() {
    return !!DataViewPrototypeGetUint8(this.#handle, IDX_STATE_STREAM_READ_ENDED);
  }

  /** @type {boolean} */
  get writeEnded() {
    return !!DataViewPrototypeGetUint8(this.#handle, IDX_STATE_STREAM_WRITE_ENDED);
  }

  /** @type {boolean} */
  get destroyed() {
    return !!DataViewPrototypeGetUint8(this.#handle, IDX_STATE_STREAM_DESTROYED);
  }

  /** @type {boolean} */
  get paused() {
    return !!DataViewPrototypeGetUint8(this.#handle, IDX_STATE_STREAM_PAUSED);
  }

  /** @type {boolean} */
  get reset() {
    return !!DataViewPrototypeGetUint8(this.#handle, IDX_STATE_STREAM_RESET);
  }

  /** @type {boolean} */
  get hasReader() {
    return !!DataViewPrototypeGetUint8(this.#handle, IDX_STATE_STREAM_HAS_READER);
  }

  /** @type {boolean} */
  get wantsBlock() {
    return !!DataViewPrototypeGetUint8(this.#handle, IDX_STATE_STREAM_WANTS_BLOCK);
  }

  /** @type {boolean} */
  set wantsBlock(val) {
    DataViewPrototypeSetUint8(this.#handle, IDX_STATE_STREAM_WANTS_BLOCK, val ? 1 : 0);
  }

  /** @type {boolean} */
  get wantsHeaders() {
    return !!DataViewPrototypeGetUint8(this.#handle, IDX_STATE_STREAM_WANTS_HEADERS);
  }

  /** @type {boolean} */
  set wantsHeaders(val) {
    DataViewPrototypeSetUint8(this.#handle, IDX_STATE_STREAM_WANTS_HEADERS, val ? 1 : 0);
  }

  /** @type {boolean} */
  get wantsReset() {
    return !!DataViewPrototypeGetUint8(this.#handle, IDX_STATE_STREAM_WANTS_RESET);
  }

  /** @type {boolean} */
  set wantsReset(val) {
    DataViewPrototypeSetUint8(this.#handle, IDX_STATE_STREAM_WANTS_RESET, val ? 1 : 0);
  }

  /** @type {boolean} */
  get wantsTrailers() {
    return !!DataViewPrototypeGetUint8(this.#handle, IDX_STATE_STREAM_WANTS_TRAILERS);
  }

  /** @type {boolean} */
  set wantsTrailers(val) {
    DataViewPrototypeSetUint8(this.#handle, IDX_STATE_STREAM_WANTS_TRAILERS, val ? 1 : 0);
  }

  toJSON() {
    return {
      __proto__: null,
      id: `${this.id}`,
      finSent: this.finSent,
      finReceived: this.finReceived,
      readEnded: this.readEnded,
      writeEnded: this.writeEnded,
      destroyed: this.destroyed,
      paused: this.paused,
      reset: this.reset,
      hasReader: this.hasReader,
      wantsBlock: this.wantsBlock,
      wantsHeaders: this.wantsHeaders,
      wantsReset: this.wantsReset,
      wantsTrailers: this.wantsTrailers,
    };
  }

  [kInspect](depth, options) {
    if (depth < 0)
      return this;

    const opts = {
      ...options,
      depth: options.depth == null ? null : options.depth - 1,
    };

    return `StreamState ${inspect({
      id: this.id,
      finSent: this.finSent,
      finReceived: this.finReceived,
      readEnded: this.readEnded,
      writeEnded: this.writeEnded,
      destroyed: this.destroyed,
      paused: this.paused,
      reset: this.reset,
      hasReader: this.hasReader,
      wantsBlock: this.wantsBlock,
      wantsHeaders: this.wantsHeaders,
      wantsReset: this.wantsReset,
      wantsTrailers: this.wantsTrailers,
    }, opts)}`;
  }

  [kFinishClose]() {
    // When the stream is destroyed, the state is no longer available.
    this.#handle = new DataView(new ArrayBuffer(0));
  }
}

class QuicStream {
  /** @type {object} */
  #handle;
  /** @type {Session} */
  #session;
  /** @type {QuicStreamStats} */
  #stats;
  /** @type {QuicStreamState} */
  #state;
  /** @type {number} */
  #direction;
  /** @type {OnStreamErrorCallback} */
  #onerror;
  /** @type {OnBlockedCallback|undefined} */
  #onblocked;
  /** @type {OnStreamErrorCallback|undefined} */
  #onreset;
  /** @type {OnHeadersCallback|undefined} */
  #onheaders;
  /** @type {OnTrailersCallback|undefined} */
  #ontrailers;

  /**
   * @param {StreamCallbackConfiguration} config
   * @param {object} handle
   * @param {Session} session
   */
  constructor(config, handle, session, direction) {
    validateObject(config, 'config');
    this.#stats = new QuicStreamStats(handle.stats);
    this.#state = new QuicStreamState(handle.stats);
    const {
      onblocked,
      onerror,
      onreset,
      onheaders,
      ontrailers,
    } = config;
    if (onblocked !== undefined) {
      validateFunction(onblocked, 'config.onblocked');
      this.#state.wantsBlock = true;
      this.#onblocked = onblocked;
    }
    if (onreset !== undefined) {
      validateFunction(onreset, 'config.onreset');
      this.#state.wantsReset = true;
      this.#onreset = onreset;
    }
    if (onheaders !== undefined) {
      validateFunction(onheaders, 'config.onheaders');
      this.#state.wantsHeaders = true;
      this.#onheaders = onheaders;
    }
    if (ontrailers !== undefined) {
      validateFunction(ontrailers, 'config.ontrailers');
      this.#state.wantsTrailers = true;
      this.#ontrailers = ontrailers;
    }
    validateFunction(onerror, 'config.onerror');
    this.#onerror = onerror;
    this.#handle = handle;
    this.#session = session;
    this.#direction = direction;
    this.#handle[kOwner] = true;
  }

  /** @type {QuicStreamStats} */
  get stats() { return this.#stats; }

  /** @type {QuicStreamState} */
  get state() { return this.#state; }

  /** @type {Session} */
  get session() { return this.#session; }

  /** @type {bigint} */
  get id() { return this.#state.id; }

  /** @type {'bidi'|'uni'} */
  get direction() {
    return this.#direction === 0 ? 'bidi' : 'uni';
  }

  [kBlocked]() {
    this.#onblocked(this);
  }

  [kError](error) {
    this.#onerror(error, this);
  }

  [kReset](error) {
    this.#onreset(error, this);
  }

  [kHeaders](headers, kind) {
    this.#onheaders(headers, kind, this);
  }

  [kTrailers]() {
    this.#ontrailers(this);
  }

  [kInspect](depth, options) {
    if (depth < 0)
      return this;

    const opts = {
      ...options,
      depth: options.depth == null ? null : options.depth - 1,
    };

    return `Stream ${inspect({
      id: this.id,
      direction: this.direction,
      stats: this.stats,
      state: this.state,
      session: this.session,
    }, opts)}`;
  }
}

class SessionStats {
  /** @type {BigUint64Array} */
  #handle;

  /**
   * @param {BigUint64Array} buffer
   */
  constructor(buffer) {
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
  get destroyedAt() {
    return this.#handle[IDX_STATS_SESSION_DESTROYED_AT];
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
  get gracefulClosingAt() {
    return this.#handle[IDX_STATS_SESSION_GRACEFUL_CLOSING_AT];
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
  get lossRetransmitCount() {
    return this.#handle[IDX_STATS_SESSION_LOSS_RETRANSMIT_COUNT];
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

  toJSON() {
    return {
      __proto__: null,
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
      lossRetransmitCount: `${this.lossRetransmitCount}`,
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

    return `SessionStats ${inspect({
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
      lossRetransmitCount: this.lossRetransmitCount,
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

  [kFinishClose]() {
    // Snapshot the stats into a new BigUint64Array since the underlying
    // buffer will be destroyed.
    this.#handle = new BigUint64Array(this.#handle);
  }
}

class SessionState {
  /** @type {DataView} */
  #handle;

  /**
   * @param {ArrayBuffer} buffer
   */
  constructor(buffer) {
    if (!isArrayBuffer(buffer)) {
      throw new ERR_INVALID_ARG_TYPE('buffer', ['ArrayBuffer'], buffer);
    }
    this.#handle = new DataView(buffer);
  }

  /** @type {boolean} */
  get hasPathValidationListener() {
    return !!DataViewPrototypeGetUint8(this.#handle, IDX_STATE_SESSION_PATH_VALIDATION);
  }

  /** @type {boolean} */
  set hasPathValidationListener(val) {
    DataViewPrototypeSetUint8(this.#handle, IDX_STATE_SESSION_PATH_VALIDATION, val ? 1 : 0);
  }

  /** @type {boolean} */
  get hasVersionNegotiationListener() {
    return !!DataViewPrototypeGetUint8(this.#handle, IDX_STATE_SESSION_VERSION_NEGOTIATION);
  }

  /** @type {boolean} */
  set hasVersionNegotiationListener(val) {
    DataViewPrototypeSetUint8(this.#handle, IDX_STATE_SESSION_VERSION_NEGOTIATION, val ? 1 : 0);
  }

  /** @type {boolean} */
  get hasDatagramListener() {
    return !!DataViewPrototypeGetUint8(this.#handle, IDX_STATE_SESSION_DATAGRAM);
  }

  /** @type {boolean} */
  set hasDatagramListener(val) {
    DataViewPrototypeSetUint8(this.#handle, IDX_STATE_SESSION_DATAGRAM, val ? 1 : 0);
  }

  /** @type {boolean} */
  get hasSessionTicketListener() {
    return !!DataViewPrototypeGetUint8(this.#handle, IDX_STATE_SESSION_SESSION_TICKET);
  }

  /** @type {boolean} */
  set hasSessionTicketListener(val) {
    DataViewPrototypeSetUint8(this.#handle, IDX_STATE_SESSION_SESSION_TICKET, val ? 1 : 0);
  }

  /** @type {boolean} */
  get isClosing() {
    return !!DataViewPrototypeGetUint8(this.#handle, IDX_STATE_SESSION_CLOSING);
  }

  /** @type {boolean} */
  get isGracefulClose() {
    return !!DataViewPrototypeGetUint8(this.#handle, IDX_STATE_SESSION_GRACEFUL_CLOSE);
  }

  /** @type {boolean} */
  get isSilentClose() {
    return !!DataViewPrototypeGetUint8(this.#handle, IDX_STATE_SESSION_SILENT_CLOSE);
  }

  /** @type {boolean} */
  get isStatelessReset() {
    return !!DataViewPrototypeGetUint8(this.#handle, IDX_STATE_SESSION_STATELESS_RESET);
  }

  /** @type {boolean} */
  get isDestroyed() {
    return !!DataViewPrototypeGetUint8(this.#handle, IDX_STATE_SESSION_DESTROYED);
  }

  /** @type {boolean} */
  get isHandshakeCompleted() {
    return !!DataViewPrototypeGetUint8(this.#handle, IDX_STATE_SESSION_HANDSHAKE_COMPLETED);
  }

  /** @type {boolean} */
  get isHandshakeConfirmed() {
    return !!DataViewPrototypeGetUint8(this.#handle, IDX_STATE_SESSION_HANDSHAKE_CONFIRMED);
  }

  /** @type {boolean} */
  get isStreamOpenAllowed() {
    return !!DataViewPrototypeGetUint8(this.#handle, IDX_STATE_SESSION_STREAM_OPEN_ALLOWED);
  }

  /** @type {boolean} */
  get isPrioritySupported() {
    return !!DataViewPrototypeGetUint8(this.#handle, IDX_STATE_SESSION_PRIORITY_SUPPORTED);
  }

  /** @type {boolean} */
  get isWrapped() {
    return !!DataViewPrototypeGetUint8(this.#handle, IDX_STATE_SESSION_WRAPPED);
  }

  /** @type {bigint} */
  get lastDatagramId() {
    return DataViewPrototypeGetBigUint64(this.#handle, IDX_STATE_SESSION_LAST_DATAGRAM_ID);
  }

  toJSON() {
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

    const opts = {
      ...options,
      depth: options.depth == null ? null : options.depth - 1,
    };

    return `SessionState ${inspect({
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
    this.#handle = new DataView(new ArrayBuffer(0));
  }
}

class Session {
  /** @type {Endpoint} */
  #endpoint = undefined;
  /** @type {boolean} */
  #isPendingClose = false;
  /** @type {object|undefined} */
  #handle;
  /** @type {PromiseWithResolvers<void>} */
  #pendingClose;
  /** @type {SocketAddress|undefined} */
  #remoteAddress = undefined;
  /** @type {SessionState} */
  #state;
  /** @type {SessionStats} */
  #stats;
  /** @type {QuicStream[]} */
  #streams = [];
  /** @type {OnStreamCallback} */
  #onstream;
  /** @type {OnDatagramCallback|undefined} */
  #ondatagram;
  /** @type {OnDatagramStatusCallback|undefined} */
  #ondatagramstatus;
  /** @type {OnPathValidationCallback|undefined} */
  #onpathvalidation;
  /** @type {OnSessionTicketCallback|undefined} */
  #onsessionticket;
  /** @type {OnVersionNegotiationCallback|undefined} */
  #onversionnegotiation;
  /** @type {OnHandshakeCallback} */
  #onhandshake;
  /** @type {StreamCallbackConfiguration} */
  #streamConfig;

  /**
   * @param {SessionCallbackConfiguration} config
   * @param {StreamCallbackConfiguration} streamConfig
   * @param {object} [handle]
   * @param {Endpoint} [endpoint]
   */
  constructor(config, streamConfig, handle, endpoint) {
    validateObject(config, 'config');
    this.#stats = new SessionStats(handle.stats);
    this.#state = new SessionState(handle.state);
    const {
      ondatagram,
      ondatagramstatus,
      onhandshake,
      onpathvalidation,
      onsessionticket,
      onstream,
      onversionnegotiation,
    } = config;
    if (ondatagram !== undefined) {
      validateFunction(ondatagram, 'config.ondatagram');
      validateFunction(ondatagramstatus, 'config.ondatagramstatus');
      this.#state.hasDatagramListener = true;
      this.#ondatagram = ondatagram;
      this.#ondatagramstatus = ondatagramstatus;
    }
    validateFunction(onhandshake, 'config.onhandshake');
    if (onpathvalidation !== undefined) {
      validateFunction(onpathvalidation, 'config.onpathvalidation');
      this.#state.hasPathValidationListener = true;
      this.#onpathvalidation = onpathvalidation;
    }
    if (onsessionticket !== undefined) {
      validateFunction(onsessionticket, 'config.onsessionticket');
      this.#state.hasSessionTicketListener = true;
      this.#onsessionticket = onsessionticket;
    }
    validateFunction(onstream, 'config.onstream');
    if (onversionnegotiation !== undefined) {
      validateFunction(onversionnegotiation, 'config.onversionnegotiation');
      this.#state.hasVersionNegotiationListener = true;
      this.#onversionnegotiation = onversionnegotiation;
    }
    this.#onhandshake = onhandshake;
    this.#onstream = onstream;
    this.#handle = handle;
    this.#endpoint = endpoint;
    this.#pendingClose = Promise.withResolvers();  // eslint-disable-line node-core/prefer-primordials
    this.#handle[kOwner] = this;
  }

  /** @type {boolean} */
  get #isClosedOrClosing() {
    return this.#handle === undefined || this.#isPendingClose;
  }

  /** @type {SessionStats} */
  get stats() { return this.#stats; }

  /** @type {SessionState} */
  get state() { return this.#state; }

  /** @type {Endpoint} */
  get endpoint() { return this.#endpoint; }

  /** @type {Path} */
  get path() {
    if (this.#isClosedOrClosing) return undefined;
    if (this.#remoteAddress === undefined) {
      const addr = this.#handle.getRemoteAddress();
      if (addr !== undefined) {
        this.#remoteAddress = new InternalSocketAddress(addr);
      }
    }
    return {
      local: this.#endpoint.address,
      remote: this.#remoteAddress,
    };
  }

  /**
   * @returns {QuicStream}
   */
  openBidirectionalStream() {
    if (this.#isClosedOrClosing) {
      throw new ERR_INVALID_STATE('Session is closed');
    }
    if (!this.state.isStreamOpenAllowed) {
      throw new ERR_QUIC_OPEN_STREAM_FAILED();
    }
    const handle = this.#handle.openStream(STREAM_DIRECTION_BIDIRECTIONAL);
    if (handle === undefined) {
      throw new ERR_QUIC_OPEN_STREAM_FAILED();
    }
    const stream = new QuicStream(this.#streamConfig, handle, this, 0);
    ArrayPrototypePush(this.#streams, stream);
    return stream;
  }

  /**
   * @returns {QuicStream}
   */
  openUnidirectionalStream() {
    if (this.#isClosedOrClosing) {
      throw new ERR_INVALID_STATE('Session is closed');
    }
    if (!this.state.isStreamOpenAllowed) {
      throw new ERR_QUIC_OPEN_STREAM_FAILED();
    }
    const handle = this.#handle.openStream(STREAM_DIRECTION_UNIDIRECTIONAL);
    if (handle === undefined) {
      throw new ERR_QUIC_OPEN_STREAM_FAILED();
    }
    const stream = new QuicStream(this.#streamConfig, handle, this, 1);
    ArrayPrototypePush(this.#streams, stream);
    return stream;
  }

  /**
   * Initiate a key update.
   */
  updateKey() {
    if (this.#isClosedOrClosing) {
      throw new ERR_INVALID_STATE('Session is closed');
    }
    this.#handle.updateKey();
  }

  /**
   * Gracefully closes the session. Any streams created on the session will be
   * allowed to complete gracefully and any datagrams that have already been
   * queued for sending will be allowed to complete. Once all streams have been
   * completed and all datagrams have been sent, the session will be closed.
   * New streams will not be allowed to be created. The returned promise will
   * be resolved when the session closes, or will be rejected if the session
   * closes abruptly due to an error.
   * @returns {Promise<void>}
   */
  close() {
    if (!this.#isClosedOrClosing) {
      this.#isPendingClose = true;
      this.#handle?.gracefulClose();
    }
    return this.closed;
  }

  /**
   * A promise that is resolved when the session is closed, or is rejected if
   * the session is closed abruptly due to an error.
   * @type {Promise<void>}
   */
  get closed() { return this.#pendingClose.promise; }

  /** @type {boolean} */
  get destroyed() { return this.#handle === undefined; }

  /**
   * Forcefully closes the session abruptly without waiting for streams to be
   * completed naturally. Any streams that are still open will be immediately
   * destroyed and any queued datagrams will be dropped. If an error is given,
   * the closed promise will be rejected with that error. If no error is given,
   * the closed promise will be resolved.
   * @param {any} error
   */
  destroy(error) {
    // TODO(@jasnell): Implement.
  }

  /**
   * Send a datagram. The id of the sent datagram will be returned. The status
   * of the sent datagram will be reported via the datagram-status event if
   * possible.
   *
   * If a string is given it will be encoded as UTF-8.
   *
   * If an ArrayBufferView is given, the view will be copied.
   * @param {ArrayBufferView|string} datagram The datagram payload
   * @returns {bigint} The datagram ID
   */
  sendDatagram(datagram) {
    if (this.#isClosedOrClosing) {
      throw new ERR_INVALID_STATE('Session is closed');
    }
    if (typeof datagram === 'string') {
      datagram = Buffer.from(datagram, 'utf8');
    } else {
      if (!isArrayBufferView(datagram)) {
        throw new ERR_INVALID_ARG_TYPE('datagram',
                                       ['ArrayBufferView', 'string'],
                                       datagram);
      }
      datagram = new Uint8Array(datagram.buffer.transfer(),
                                datagram.byteOffset,
                                datagram.byteLength);
    }
    return this.#handle.sendDatagram(datagram);
  }

  /**
   * @param {Uint8Array} u8
   * @param {boolean} early
   */
  [kDatagram](u8, early) {
    if (this.destroyed) return;
    this.#ondatagram(u8, this, early);
  }

  /**
   * @param {bigint} id
   * @param {'lost'|'acknowledged'} status
   */
  [kDatagramStatus](id, status) {
    if (this.destroyed) return;
    this.#ondatagramstatus(id, status, this);
  }

  /**
   * @param {'aborted'|'failure'|'success'} result
   * @param {SocketAddress} newLocalAddress
   * @param {SocketAddress} newRemoteAddress
   * @param {SocketAddress} oldLocalAddress
   * @param {SocketAddress} oldRemoteAddress
   * @param {boolean} preferredAddress
   */
  [kPathValidation](result, newLocalAddress, newRemoteAddress, oldLocalAddress,
                    oldRemoteAddress, preferredAddress) {
    if (this.destroyed) return;
    this.#onpathvalidation(result, newLocalAddress, newRemoteAddress,
                           oldLocalAddress, oldRemoteAddress, preferredAddress,
                           this);
  }

  /**
   * @param {object} ticket
   */
  [kSessionTicket](ticket) {
    if (this.destroyed) return;
    this.#onsessionticket(ticket, this);
  }

  /**
   * @param {number} version
   * @param {number[]} requestedVersions
   * @param {number[]} supportedVersions
   */
  [kVersionNegotiation](version, requestedVersions, supportedVersions) {
    if (this.destroyed) return;
    this.#onversionnegotiation(version, requestedVersions, supportedVersions, this);
  }

  /**
   * @param {string} sni
   * @param {string} alpn
   * @param {string} cipher
   * @param {string} cipherVersion
   * @param {string} validationErrorReason
   * @param {number} validationErrorCode
   * @param {boolean} earlyDataAccepted
   */
  [kHandshake](sni, alpn, cipher, cipherVersion, validationErrorReason,
               validationErrorCode, earlyDataAccepted) {
    if (this.destroyed) return;
    this.#onhandshake(sni, alpn, cipher, cipherVersion, validationErrorReason,
                      validationErrorCode, earlyDataAccepted, this);
  }

  /**
   * @param {object} handle
   * @param {number} direction
   */
  [kNewStream](handle, direction) {
    const stream = new QuicStream(this.#streamConfig, handle, this, direction);
    ArrayPrototypePush(this.#streams, stream);
    this.#onstream(stream, this);
  }

  [kInspect](depth, options) {
    if (depth < 0)
      return this;

    const opts = {
      ...options,
      depth: options.depth == null ? null : options.depth - 1,
    };

    return `Session ${inspect({
      closed: this.closed,
      closing: this.#isPendingClose,
      destroyed: this.destroyed,
      endpoint: this.endpoint,
      path: this.path,
      state: this.state,
      stats: this.stats,
      streams: this.#streams,
    }, opts)}`;
  }

  [kFinishClose](errorType, code, reason) {
    // TODO(@jasnell): Finish the implementation
  }

  async [SymbolAsyncDispose]() { await this.close(); }
}

class EndpointStats {
  /** @type {BigUint64Array} */
  #handle;

  /**
   * @param {ArrayBuffer} buffer
   */
  constructor(buffer) {
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

  toJSON() {
    return {
      __proto__: null,
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

    return `EndpointStats ${inspect({
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

  [kFinishClose]() {
    // Snapshot the stats into a new BigUint64Array since the underlying
    // buffer will be destroyed.
    this.#handle = new BigUint64Array(this.#handle);
  }
}

class EndpointState {
  /** @type {DataView} */
  #handle;

  /**
   * @param {ArrayBuffer} buffer
   */
  constructor(buffer) {
    if (!isArrayBuffer(buffer)) {
      throw new ERR_INVALID_ARG_TYPE('buffer', ['ArrayBuffer'], buffer);
    }
    this.#handle = new DataView(buffer);
  }

  #assertNotClosed() {
    if (this.#handle.byteLength === 0) {
      throw new ERR_INVALID_STATE('Endpoint is closed');
    }
  }

  /** @type {boolean} */
  get isBound() {
    this.#assertNotClosed();
    return !!DataViewPrototypeGetUint8(this.#handle, IDX_STATE_ENDPOINT_BOUND);
  }

  /** @type {boolean} */
  get isReceiving() {
    this.#assertNotClosed();
    return !!DataViewPrototypeGetUint8(this.#handle, IDX_STATE_ENDPOINT_RECEIVING);
  }

  /** @type {boolean} */
  get isListening() {
    this.#assertNotClosed();
    return !!DataViewPrototypeGetUint8(this.#handle, IDX_STATE_ENDPOINT_LISTENING);
  }

  /** @type {boolean} */
  get isClosing() {
    this.#assertNotClosed();
    return !!DataViewPrototypeGetUint8(this.#handle, IDX_STATE_ENDPOINT_CLOSING);
  }

  /** @type {boolean} */
  get isBusy() {
    this.#assertNotClosed();
    return !!DataViewPrototypeGetUint8(this.#handle, IDX_STATE_ENDPOINT_BUSY);
  }

  /**
   * The number of underlying callbacks that are pending. If the session
   * is closing, these are the number of callbacks that the session is
   * waiting on before it can be closed.
   * @type {bigint}
   */
  get pendingCallbacks() {
    this.#assertNotClosed();
    return DataViewPrototypeGetBigUint64(this.#handle, IDX_STATE_ENDPOINT_PENDING_CALLBACKS);
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
      return 'EndpointState { <Closed> }';
    }

    const opts = {
      ...options,
      depth: options.depth == null ? null : options.depth - 1,
    };

    return `EndpointState ${inspect({
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

function validateStreamConfig(config, name = 'config') {
  validateObject(config, name);
  if (config.onerror !== undefined)
    validateFunction(config.onerror, `${name}.onerror`);
  if (config.onblocked !== undefined)
    validateFunction(config.onblocked, `${name}.onblocked`);
  if (config.onreset !== undefined)
    validateFunction(config.onreset, `${name}.onreset`);
  if (config.onheaders !== undefined)
    validateFunction(config.onheaders, `${name}.onheaders`);
  if (config.ontrailers !== undefined)
    validateFunction(config.ontrailers, `${name}.ontrailers`);
  return config;
}

function validateSessionConfig(config, name = 'config') {
  validateObject(config, name);
  if (config.onstream !== undefined)
    validateFunction(config.onstream, `${name}.onstream`);
  if (config.ondatagram !== undefined)
    validateFunction(config.ondatagram, `${name}.ondatagram`);
  if (config.ondatagramstatus !== undefined)
    validateFunction(config.ondatagramstatus, `${name}.ondatagramstatus`);
  if (config.onpathvalidation !== undefined)
    validateFunction(config.onpathvalidation, `${name}.onpathvalidation`);
  if (config.onsessionticket !== undefined)
    validateFunction(config.onsessionticket, `${name}.onsessionticket`);
  if (config.onversionnegotiation !== undefined)
    validateFunction(config.onversionnegotiation, `${name}.onversionnegotiation`);
  if (config.onhandshake !== undefined)
    validateFunction(config.onhandshake, `${name}.onhandshake`);
  return config;
}

function validateEndpointConfig(config) {
  validateObject(config, 'config');
  validateFunction(config.onsession, 'config.onsession');
  if (config.session !== undefined)
    validateSessionConfig(config.session, 'config.session');
  if (config.stream !== undefined)
    validateStreamConfig(config.stream, 'config.stream');
  return config;
}

class Endpoint {
  /** @type {SocketAddress|undefined} */
  #address = undefined;
  /** @type {boolean} */
  #busy = false;
  /** @type {object} */
  #handle;
  /** @type {boolean} */
  #isPendingClose = false;
  /** @type {boolean} */
  #listening = false;
  /** @type {PromiseWithResolvers<void>} */
  #pendingClose;
  /** @type {any} */
  #pendingError = undefined;
  /** @type {Session[]} */
  #sessions = [];
  /** @type {EndpointState} */
  #state;
  /** @type {EndpointStats} */
  #stats;
  /** @type {OnSessionCallback} */
  #onsession;
  /** @type {SessionCallbackConfiguration} */
  #sessionConfig;
  /** @type {StreamCallbackConfiguration */
  #streamConfig;

  /**
   * @param {EndpointCallbackConfiguration} config
   * @param {EndpointOptions} [options]
   */
  constructor(config, options = kEmptyObject) {
    validateEndpointConfig(config);
    this.#onsession = config.onsession;
    this.#sessionConfig = config.session;
    this.#streamConfig = config.stream;

    validateObject(options, 'options');
    let { address } = options;
    const {
      retryTokenExpiration,
      tokenExpiration,
      maxConnectionsPerHost,
      maxConnectionsTotal,
      maxStatelessResetsPerHost,
      addressLRUSize,
      maxRetries,
      maxPayloadSize,
      unacknowledgedPacketThreshold,
      handshakeTimeout,
      maxStreamWindow,
      maxWindow,
      rxDiagnosticLoss,
      txDiagnosticLoss,
      udpReceiveBufferSize,
      udpSendBufferSize,
      udpTTL,
      noUdpPayloadSizeShaping,
      validateAddress,
      disableActiveMigration,
      ipv6Only,
      cc,
      resetTokenSecret,
      tokenSecret,
    } = options;

    // All of the other options will be validated internally by the C++ code
    if (address !== undefined && !SocketAddress.isSocketAddress(address)) {
      if (typeof address === 'object' && address !== null) {
        address = new SocketAddress(address);
      } else {
        throw new ERR_INVALID_ARG_TYPE('options.address', 'SocketAddress', address);
      }
    }

    this.#pendingClose = Promise.withResolvers();  // eslint-disable-line node-core/prefer-primordials
    this.#handle = new Endpoint_({
      __proto__: null,
      address: address?.[kSocketAddressHandle],
      retryTokenExpiration,
      tokenExpiration,
      maxConnectionsPerHost,
      maxConnectionsTotal,
      maxStatelessResetsPerHost,
      addressLRUSize,
      maxRetries,
      maxPayloadSize,
      unacknowledgedPacketThreshold,
      handshakeTimeout,
      maxStreamWindow,
      maxWindow,
      rxDiagnosticLoss,
      txDiagnosticLoss,
      udpReceiveBufferSize,
      udpSendBufferSize,
      udpTTL,
      noUdpPayloadSizeShaping,
      validateAddress,
      disableActiveMigration,
      ipv6Only,
      cc,
      resetTokenSecret,
      tokenSecret,
    });
    this.#handle[kOwner] = this;
    this.#stats = new EndpointStats(this.#handle.stats);
    this.#state = new EndpointState(this.#handle.state);
  }

  /** @type {EndpointStats} */
  get stats() { return this.#stats; }

  /** @type {EndpointState} */
  get state() { return this.#state; }

  get #isClosedOrClosing() {
    return this.#handle === undefined || this.#isPendingClose;
  }

  /**
   * When an endpoint is marked as busy, it will not accept new connections.
   * Existing connections will continue to work.
   * @type {boolean}
   */
  get busy() { return this.#busy; }

  /**
   * @type {boolean}
   */
  set busy(val) {
    if (this.#isClosedOrClosing) {
      throw new ERR_INVALID_STATE('Endpoint is closed');
    }
    // The val is allowed to be any truthy value
    val = !!val;
    // Non-op if there is no change
    if (val !== this.#busy) {
      this.#busy = val;
      this.#handle.markBusy(this.#busy);
    }
  }

  /**
   * The local address the endpoint is bound to (if any)
   * @type {SocketAddress|undefined}
   */
  get address() {
    if (this.#isClosedOrClosing) return undefined;
    if (this.#address === undefined) {
      const addr = this.#handle.address();
      if (addr !== undefined) this.#address = new InternalSocketAddress(addr);
    }
    return this.#address;
  }

  /**
   * Configures the endpoint to listen for incoming connections.
   * @param {SessionOptions} [options]
   */
  listen(options = kEmptyObject) {
    if (this.#isClosedOrClosing) {
      throw new ERR_INVALID_STATE('Endpoint is closed');
    }
    if (this.#listening) {
      throw new ERR_INVALID_STATE('Endpoint is already listening');
    }

    validateObject(options, 'options');

    const {
      version,
      minVersion,
      preferredAddressPolicy = 'default',
      application = kEmptyObject,
      transportParams = kEmptyObject,
      tls = kEmptyObject,
      qlog = false,
    } = options;

    this.#handle.listen({
      version,
      minVersion,
      preferredAddressPolicy: getPreferredAddressPolicy(preferredAddressPolicy),
      application,
      transportParams,
      tls: processTlsOptions(tls),
      qlog,
    });
    this.#listening = true;
  }

  /**
   * Initiates a session with a remote endpoint.
   * @param {SocketAddress} address
   * @param {SessionOptions} [options]
   * @returns {Session}
   */
  connect(address, options = kEmptyObject) {
    if (this.#isClosedOrClosing) {
      throw new ERR_INVALID_STATE('Endpoint is closed');
    }
    if (this.#busy) {
      throw new ERR_INVALID_STATE('Endpoint is busy');
    }

    if (address === undefined || !SocketAddress.isSocketAddress(address)) {
      if (typeof address === 'object' && address !== null) {
        address = new SocketAddress(address);
      } else {
        throw new ERR_INVALID_ARG_TYPE('address', 'SocketAddress', address);
      }
    }

    validateObject(options, 'options');
    const {
      version,
      minVersion,
      preferredAddressPolicy = 'default',
      application = kEmptyObject,
      transportParams = kEmptyObject,
      tls = kEmptyObject,
      qlog = false,
      sessionTicket,
    } = options;

    const handle = this.#handle.connect(address[kSocketAddressHandle], {
      version,
      minVersion,
      preferredAddressPolicy: getPreferredAddressPolicy(preferredAddressPolicy),
      application,
      transportParams,
      tls: processTlsOptions(tls),
      qlog,
    }, sessionTicket);

    if (handle === undefined) {
      throw new ERR_QUIC_CONNECTION_FAILED();
    }
    const session = new Session(this.#sessionConfig, this.#streamConfig, handle, this);
    ArrayPrototypePush(this.#sessions, session);
    return session;
  }

  /**
   * Gracefully closes the endpoint. Any existing sessions will be permitted to
   * end gracefully, after which the endpoint will be closed. New sessions will
   * not be accepted or created. The returned promise will be resolved when
   * closing is complete, or will be rejected if the endpoint is closed abruptly
   * due to an error.
   * @returns {Promise<void>}
   */
  close() {
    if (!this.#isClosedOrClosing) {
      this.#isPendingClose = true;
      this.#handle?.closeGracefully();
    }
    return this.closed;
  }

  /**
   * Returns a promise that is resolved when the endpoint is closed or rejects
   * if the endpoint is closed abruptly due to an error. The closed property
   * is set to the same promise that is returned by the close() method.
   * @type {Promise<void>}
   */
  get closed() { return this.#pendingClose.promise; }

  /** @type {boolean} */
  get destroyed() { return this.#handle === undefined; }

  /**
   * Forcefully terminates the endpoint by immediately destroying all sessions
   * after calling close. If an error is given, the closed promise will be
   * rejected with that error. If no error is given, the closed promise will
   * be resolved.
   * @param {any} error
   */
  destroy(error) {
    if (!this.#isClosedOrClosing) {
      // Start closing the endpoint.
      this.#pendingError = error;
      // Trigger a graceful close of the endpoint that'll ensure that the
      // endpoint is closed down after all sessions are closed...
      this.close();
    }
    // Now, force all sessions to be abruptly closed...
    for (const session of this.#sessions) {
      session.destroy(error);
    }
  }

  ref() {
    if (this.#handle !== undefined) this.#handle.ref(true);
  }

  unref() {
    if (this.#handle !== undefined) this.#handle.ref(false);
  }

  [kFinishClose](context, status) {
    if (this.#handle === undefined) return;
    this.#isPendingClose = false;
    this.#address = undefined;
    this.#busy = false;
    this.#listening = false;
    this.#isPendingClose = false;
    this.#stats[kFinishClose]();
    this.#state[kFinishClose]();
    this.#sessions = [];

    switch (context) {
      case kCloseContextClose: {
        if (this.#pendingError !== undefined) {
          this.#pendingClose.reject(this.#pendingError);
        } else {
          this.#pendingClose.resolve();
        }
        break;
      }
      case kCloseContextBindFailure: {
        this.#pendingClose.reject(
          new ERR_QUIC_ENDPOINT_CLOSED('Bind failure', status));
        break;
      }
      case kCloseContextListenFailure: {
        this.#pendingClose.reject(
          new ERR_QUIC_ENDPOINT_CLOSED('Listen failure', status));
        break;
      }
      case kCloseContextReceiveFailure: {
        this.#pendingClose.reject(
          new ERR_QUIC_ENDPOINT_CLOSED('Receive failure', status));
        break;
      }
      case kCloseContextSendFailure: {
        this.#pendingClose.reject(
          new ERR_QUIC_ENDPOINT_CLOSED('Send failure', status));
        break;
      }
      case kCloseContextStartFailure: {
        this.#pendingClose.reject(
          new ERR_QUIC_ENDPOINT_CLOSED('Start failure', status));
        break;
      }
    }

    this.#pendingError = undefined;
    this.#pendingClose.resolve = undefined;
    this.#pendingClose.reject = undefined;
    this.#handle = undefined;
  }

  [kNewSession](handle) {
    const session = new Session(this.#sessionConfig, this.#streamConfig, handle, this);
    ArrayPrototypePush(this.#sessions, session);
    this.#onsession(session, this);
  }

  async [SymbolAsyncDispose]() { await this.close(); }

  [kInspect](depth, options) {
    if (depth < 0)
      return this;

    const opts = {
      ...options,
      depth: options.depth == null ? null : options.depth - 1,
    };

    return `Endpoint ${inspect({
      address: this.address,
      busy: this.busy,
      closed: this.closed,
      closing: this.#isPendingClose,
      destroyed: this.destroyed,
      listening: this.#listening,
      sessions: this.#sessions,
      stats: this.stats,
      state: this.state,
    }, opts)}`;
  }
};

ObjectDefineProperties(QuicStreamStats.prototype, {
  createdAt: kEnumerable,
  receivedAt: kEnumerable,
  ackedAt: kEnumerable,
  closingAt: kEnumerable,
  destroyedAt: kEnumerable,
  bytesReceived: kEnumerable,
  bytesSent: kEnumerable,
  maxOffset: kEnumerable,
  maxOffsetAcknowledged: kEnumerable,
  maxOffsetReceived: kEnumerable,
  finalSize: kEnumerable,
});
ObjectDefineProperties(QuicStreamState.prototype, {
  id: kEnumerable,
  finSent: kEnumerable,
  finReceived: kEnumerable,
  readEnded: kEnumerable,
  writeEnded: kEnumerable,
  destroyed: kEnumerable,
  paused: kEnumerable,
  reset: kEnumerable,
  hasReader: kEnumerable,
  wantsBlock: kEnumerable,
  wantsHeaders: kEnumerable,
  wantsReset: kEnumerable,
  wantsTrailers: kEnumerable,
});
ObjectDefineProperties(QuicStream.prototype, {
  stats: kEnumerable,
  state: kEnumerable,
  session: kEnumerable,
  id: kEnumerable,
});
ObjectDefineProperties(SessionStats.prototype, {
  createdAt: kEnumerable,
  closingAt: kEnumerable,
  destroyedAt: kEnumerable,
  handshakeCompletedAt: kEnumerable,
  handshakeConfirmedAt: kEnumerable,
  gracefulClosingAt: kEnumerable,
  bytesReceived: kEnumerable,
  bytesSent: kEnumerable,
  bidiInStreamCount: kEnumerable,
  bidiOutStreamCount: kEnumerable,
  uniInStreamCount: kEnumerable,
  uniOutStreamCount: kEnumerable,
  lossRetransmitCount: kEnumerable,
  maxBytesInFlights: kEnumerable,
  bytesInFlight: kEnumerable,
  blockCount: kEnumerable,
  cwnd: kEnumerable,
  latestRtt: kEnumerable,
  minRtt: kEnumerable,
  rttVar: kEnumerable,
  smoothedRtt: kEnumerable,
  ssthresh: kEnumerable,
  datagramsReceived: kEnumerable,
  datagramsSent: kEnumerable,
  datagramsAcknowledged: kEnumerable,
  datagramsLost: kEnumerable,
});
ObjectDefineProperties(SessionState.prototype, {
  hasPathValidationListener: kEnumerable,
  hasVersionNegotiationListener: kEnumerable,
  hasDatagramListener: kEnumerable,
  hasSessionTicketListener: kEnumerable,
  isClosing: kEnumerable,
  isGracefulClose: kEnumerable,
  isSilentClose: kEnumerable,
  isStatelessReset: kEnumerable,
  isDestroyed: kEnumerable,
  isHandshakeCompleted: kEnumerable,
  isHandshakeConfirmed: kEnumerable,
  isStreamOpenAllowed: kEnumerable,
  isPrioritySupported: kEnumerable,
  isWrapped: kEnumerable,
  lastDatagramId: kEnumerable,
});
ObjectDefineProperties(Session.prototype, {
  closed: kEnumerable,
  destroyed: kEnumerable,
  endpoint: kEnumerable,
  path: kEnumerable,
  state: kEnumerable,
  stats: kEnumerable,
});
ObjectDefineProperties(EndpointStats.prototype, {
  createdAt: kEnumerable,
  destroyedAt: kEnumerable,
  bytesReceived: kEnumerable,
  bytesSent: kEnumerable,
  packetsReceived: kEnumerable,
  packetsSent: kEnumerable,
  serverSessions: kEnumerable,
  clientSessions: kEnumerable,
  serverBusyCount: kEnumerable,
  retryCount: kEnumerable,
  versionNegotiationCount: kEnumerable,
  statelessResetCount: kEnumerable,
  immediateCloseCount: kEnumerable,
});
ObjectDefineProperties(EndpointState.prototype, {
  isBound: kEnumerable,
  isReceiving: kEnumerable,
  isListening: kEnumerable,
  isClosing: kEnumerable,
  isBusy: kEnumerable,
  pendingCallbacks: kEnumerable,
});
ObjectDefineProperties(Endpoint.prototype, {
  address: kEnumerable,
  busy: kEnumerable,
  closed: kEnumerable,
  destroyed: kEnumerable,
  state: kEnumerable,
  stats: kEnumerable,
});
ObjectDefineProperties(Endpoint, {
  CC_ALGO_RENO: {
    __proto__: null,
    value: CC_ALGO_RENO,
    writable: false,
    configurable: false,
    enumerable: true,
  },
  CC_ALGO_CUBIC: {
    __proto__: null,
    value: CC_ALGO_CUBIC,
    writable: false,
    configurable: false,
    enumerable: true,
  },
  CC_ALGO_BBR: {
    __proto__: null,
    value: CC_ALGO_BBR,
    writable: false,
    configurable: false,
    enumerable: true,
  },
  CC_ALGO_RENO_STR: {
    __proto__: null,
    value: CC_ALGO_RENO_STR,
    writable: false,
    configurable: false,
    enumerable: true,
  },
  CC_ALGO_CUBIC_STR: {
    __proto__: null,
    value: CC_ALGO_CUBIC_STR,
    writable: false,
    configurable: false,
    enumerable: true,
  },
  CC_ALGO_BBR_STR: {
    __proto__: null,
    value: CC_ALGO_BBR_STR,
    writable: false,
    configurable: false,
    enumerable: true,
  },
});
ObjectDefineProperties(Session, {
  DEFAULT_CIPHERS: {
    __proto__: null,
    value: DEFAULT_CIPHERS,
    writable: false,
    configurable: false,
    enumerable: true,
  },
  DEFAULT_GROUPS: {
    __proto__: null,
    value: DEFAULT_GROUPS,
    writable: false,
    configurable: false,
    enumerable: true,
  },
});

module.exports = {
  Endpoint,
  Session,
  QuicStream,
  // Exported for testing
  kFinishClose,
  SessionState,
  SessionStats,
  QuicStreamState,
  QuicStreamStats,
  EndpointState,
  EndpointStats,
};

/* c8 ignore stop */
