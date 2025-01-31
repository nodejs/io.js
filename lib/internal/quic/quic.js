'use strict';

// TODO(@jasnell) Temporarily ignoring c8 covrerage for this file while tests
// are still being developed.
/* c8 ignore start */

const {
  ArrayBufferPrototypeTransfer,
  ArrayIsArray,
  ArrayPrototypePush,
  BigInt,
  ObjectDefineProperties,
  SafeSet,
  SymbolAsyncDispose,
  Uint8Array,
} = primordials;

// QUIC requires that Node.js be compiled with crypto support.
const {
  assertCrypto,
} = require('internal/util');
assertCrypto();

const { inspect } = require('internal/util/inspect');

let debug = require('internal/util/debuglog').debuglog('quic', (fn) => {
  debug = fn;
});

const {
  Endpoint: Endpoint_,
  Http3Application: Http3,
  setCallbacks,

  // The constants to be exposed to end users for various options.
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
    ERR_ILLEGAL_CONSTRUCTOR,
    ERR_INVALID_ARG_TYPE,
    ERR_INVALID_ARG_VALUE,
    ERR_INVALID_STATE,
    ERR_MISSING_ARGS,
    ERR_QUIC_APPLICATION_ERROR,
    ERR_QUIC_CONNECTION_FAILED,
    ERR_QUIC_ENDPOINT_CLOSED,
    ERR_QUIC_OPEN_STREAM_FAILED,
    ERR_QUIC_TRANSPORT_ERROR,
    ERR_QUIC_VERSION_NEGOTIATION_ERROR,
  },
} = require('internal/errors');

const {
  InternalSocketAddress,
  SocketAddress,
  kHandle: kSocketAddressHandle,
} = require('internal/socketaddress');

const {
  createBlobReaderStream,
  isBlob,
  kHandle: kBlobHandle,
} = require('internal/blob');

const {
  isKeyObject,
  isCryptoKey,
} = require('internal/crypto/keys');

const {
  validateBoolean,
  validateFunction,
  validateNumber,
  validateObject,
  validateString,
} = require('internal/validators');

const {
  mapToHeaders,
} = require('internal/http2/util');

const kEmptyObject = { __proto__: null };

const {
  kApplicationProvider,
  kBlocked,
  kConnect,
  kDatagram,
  kDatagramStatus,
  kFinishClose,
  kHandshake,
  kHeaders,
  kOwner,
  kRemoveSession,
  kListen,
  kNewSession,
  kRemoveStream,
  kNewStream,
  kOnHeaders,
  kOnTrailers,
  kPathValidation,
  kPrivateConstructor,
  kReset,
  kSendHeaders,
  kSessionTicket,
  kState,
  kTrailers,
  kVersionNegotiation,
  kInspect,
  kKeyObjectHandle,
  kKeyObjectInner,
  kWantsHeaders,
  kWantsTrailers,
} = require('internal/quic/symbols');

const {
  QuicEndpointStats,
  QuicStreamStats,
  QuicSessionStats,
} = require('internal/quic/stats');

const {
  QuicEndpointState,
  QuicSessionState,
  QuicStreamState,
} = require('internal/quic/state');

const assert = require('internal/assert');

const dc = require('diagnostics_channel');
const onEndpointCreatedChannel = dc.channel('quic.endpoint.created');
const onEndpointListeningChannel = dc.channel('quic.endpoint.listen');
const onEndpointClosingChannel = dc.channel('quic.endpoint.closing');
const onEndpointClosedChannel = dc.channel('quic.endpoint.closed');
const onEndpointErrorChannel = dc.channel('quic.endpoint.error');
const onEndpointBusyChangeChannel = dc.channel('quic.endpoint.busy.change');
const onEndpointClientSessionChannel = dc.channel('quic.session.created.client');
const onEndpointServerSessionChannel = dc.channel('quic.session.created.server');
const onSessionOpenStreamChannel = dc.channel('quic.session.open.stream');
const onSessionReceivedStreamChannel = dc.channel('quic.session.received.stream');
const onSessionSendDatagramChannel = dc.channel('quic.session.send.datagram');
const onSessionUpdateKeyChannel = dc.channel('quic.session.update.key');
const onSessionClosingChannel = dc.channel('quic.session.closing');
const onSessionClosedChannel = dc.channel('quic.session.closed');
const onSessionReceiveDatagramChannel = dc.channel('quic.session.receive.datagram');
const onSessionReceiveDatagramStatusChannel = dc.channel('quic.session.receive.datagram.status');
const onSessionPathValidationChannel = dc.channel('quic.session.path.validation');
const onSessionTicketChannel = dc.channel('quic.session.ticket');
const onSessionVersionNegotiationChannel = dc.channel('quic.session.version.negotiation');
const onSessionHandshakeChannel = dc.channel('quic.session.handshake');

/**
 * @typedef {import('../socketaddress.js').SocketAddress} SocketAddress
 * @typedef {import('../crypto/keys.js').KeyObject} KeyObject
 * @typedef {import('../crypto/keys.js').CryptoKey} CryptoKey
 */

/**
 * @typedef {object} OpenStreamOptions
 * @property {ArrayBuffer|ArrayBufferView|Blob} [body] The outbound payload
 * @property {number} [sendOrder] The ordering of this stream relative to others in the same session.
 */

/**
 * @typedef {object} EndpointOptions
 * @property {string|SocketAddress} [address] The local address to bind to
 * @property {bigint|number} [addressLRUSize] The size of the address LRU cache
 * @property {boolean} [ipv6Only] Use IPv6 only
 * @property {bigint|number} [maxConnectionsPerHost] The maximum number of connections per host
 * @property {bigint|number} [maxConnectionsTotal] The maximum number of total connections
 * @property {bigint|number} [maxRetries] The maximum number of retries
 * @property {bigint|number} [maxStatelessResetsPerHost] The maximum number of stateless resets per host
 * @property {ArrayBufferView} [resetTokenSecret] The reset token secret
 * @property {bigint|number} [retryTokenExpiration] The retry token expiration
 * @property {bigint|number} [tokenExpiration] The token expiration
 * @property {ArrayBufferView} [tokenSecret] The token secret
 * @property {number} [udpReceiveBufferSize] The UDP receive buffer size
 * @property {number} [udpSendBufferSize] The UDP send buffer size
 * @property {number} [udpTTL] The UDP TTL
 * @property {boolean} [validateAddress] Validate the address using retry packets
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
 * @property {EndpointOptions|QuicEndpoint} [endpoint] An endpoint to use.
 * @property {number} [version] The version
 * @property {number} [minVersion] The minimum version
 * @property {'use'|'ignore'|'default'} [preferredAddressPolicy] The preferred address policy
 * @property {ApplicationOptions} [application] The application options
 * @property {TransportParams} [transportParams] The transport parameters
 * @property {string} [servername] The server name identifier
 * @property {string} [protocol] The application layer protocol negotiation
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
 * @property {boolean} [qlog] Enable qlog
 * @property {ArrayBufferView} [sessionTicket] The session ticket
 * @property {bigint|number} [handshakeTimeout] The handshake timeout
 * @property {bigint|number} [maxStreamWindow] The maximum stream window
 * @property {bigint|number} [maxWindow] The maximum window
 * @property {bigint|number} [maxPayloadSize] The maximum payload size
 * @property {bigint|number} [unacknowledgedPacketThreshold] The unacknowledged packet threshold
 * @property {'reno'|'cubic'|'bbr'} [cc] The congestion control algorithm
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
 * @this {QuicEndpoint}
 * @param {QuicSession} session
 * @returns {void}
 */

/**
 * @callback OnStreamCallback
 * @this {QuicSession}
 * @param {QuicStream} stream
 * @returns {void}
 */

/**
 * @callback OnDatagramCallback
 * @this {QuicSession}
 * @param {Uint8Array} datagram
 * @param {boolean} early A datagram is early if it was received before the TLS handshake completed
 * @returns {void}
 */

/**
 * @callback OnSessionTicketCallback
 * @this {QuicSession}
 * @param {object} ticket
 * @returns {void}
 */

/**
 * @callback OnBlockedCallback
 * @this {QuicStream} stream
 * @returns {void}
 */

/**
 * @callback OnStreamErrorCallback
 * @this {QuicStream}
 * @param {any} error
 * @returns {void}
 */

/**
 * @callback OnHeadersCallback
 * @this {QuicStream}
 * @param {object} headers
 * @param {string} kind
 * @returns {void}
 */

/**
 * @callback OnTrailersCallback
 * @this {QuicStream}
 * @returns {void}
 */

/**
 * Provides the callback configuration for the Endpoint|undefined.
 * @typedef {object} EndpointOptions
 * @property {SocketAddress | string} [address] The local address to bind to
 * @property {bigint|number} [retryTokenExpiration] The retry token expiration
 * @property {bigint|number} [tokenExpiration] The token expiration
 * @property {bigint|number} [maxConnectionsPerHost] The maximum number of connections per host
 * @property {bigint|number} [maxConnectionsTotal] The maximum number of total connections
 * @property {bigint|number} [maxStatelessResetsPerHost] The maximum number of stateless resets per host
 * @property {bigint|number} [addressLRUSize] The size of the address LRU cache
 * @property {bigint|number} [maxRetries] The maximum number of retriesw
 * @property {number} [rxDiagnosticLoss] The receive diagnostic loss probability (range 0.0-1.0)
 * @property {number} [txDiagnosticLoss] The transmit diagnostic loss probability (range 0.0-1.0)
 * @property {number} [udpReceiveBufferSize] The UDP receive buffer size
 * @property {number} [udpSendBufferSize] The UDP send buffer size
 * @property {number} [udpTTL] The UDP TTL
 * @property {boolean} [validateAddress] Validate the address
 * @property {boolean} [ipv6Only] Use IPv6 only
 * @property {ArrayBufferView} [resetTokenSecret] The reset token secret
 * @property {ArrayBufferView} [tokenSecret] The token secret
 */

/**
 * @typedef {object} QuicSessionInfo
 * @property {SocketAddress} local The local address
 * @property {SocketAddress} remote The remote address
 * @property {string} protocol The alpn protocol identifier negotiated for this session
 * @property {string} servername The servername identifier for this session
 * @property {string} cipher The cipher suite negotiated for this session
 * @property {string} cipherVersion The version of the cipher suite negotiated for this session
 * @property {string} [validationErrorReason] The reason the session failed validation (if any)
 * @property {string} [validationErrorCode] The error code for the validation failure (if any)
 */

setCallbacks({
  // QuicEndpoint callbacks

  /**
   * Called when the QuicEndpoint C++ handle has closed and we need to finish
   * cleaning up the JS side.
   * @param {number} context Identifies the reason the endpoint was closed.
   * @param {number} status If context indicates an error, provides the error code.
   */
  onEndpointClose(context, status) {
    debug('endpoint close callback', status);
    this[kOwner][kFinishClose](context, status);
  },
  /**
   * Called when the QuicEndpoint C++ handle receives a new server-side session
   * @param {*} session The QuicSession C++ handle
   */
  onSessionNew(session) {
    debug('new server session callback', this[kOwner], session);
    this[kOwner][kNewSession](session);
  },

  // QuicSession callbacks

  /**
   * Called when the underlying session C++ handle is closed either normally
   * or with an error.
   * @param {number} errorType
   * @param {number} code
   * @param {string} [reason]
   */
  onSessionClose(errorType, code, reason) {
    debug('session close callback', errorType, code, reason);
    this[kOwner][kFinishClose](errorType, code, reason);
  },

  /**
   * Called when a datagram is received on this session.
   * @param {Uint8Array} uint8Array
   * @param {boolean} early
   */
  onSessionDatagram(uint8Array, early) {
    debug('session datagram callback', uint8Array.byteLength, early);
    this[kOwner][kDatagram](uint8Array, early);
  },

  /**
   * Called when the status of a datagram is received.
   * @param {bigint} id
   * @param {'lost' | 'acknowledged'} status
   */
  onSessionDatagramStatus(id, status) {
    debug('session datagram status callback', id, status);
    this[kOwner][kDatagramStatus](id, status);
  },

  /**
   * Called when the session handshake completes.
   * @param {string} servername
   * @param {string} protocol
   * @param {string} cipher
   * @param {string} cipherVersion
   * @param {string} validationErrorReason
   * @param {number} validationErrorCode
   */
  onSessionHandshake(servername, protocol, cipher, cipherVersion,
                     validationErrorReason,
                     validationErrorCode) {
    debug('session handshake callback', servername, protocol, cipher, cipherVersion,
          validationErrorReason, validationErrorCode);
    this[kOwner][kHandshake](servername, protocol, cipher, cipherVersion,
                             validationErrorReason, validationErrorCode);
  },

  /**
   * Called when the session path validation completes.
   * @param {'aborted'|'failure'|'success'} result
   * @param {SocketAddress} newLocalAddress
   * @param {SocketAddress} newRemoteAddress
   * @param {SocketAddress} oldLocalAddress
   * @param {SocketAddress} oldRemoteAddress
   * @param {boolean} preferredAddress
   */
  onSessionPathValidation(result, newLocalAddress, newRemoteAddress,
                          oldLocalAddress, oldRemoteAddress, preferredAddress) {
    debug('session path validation callback', this[kOwner]);
    this[kOwner][kPathValidation](result,
                                  new InternalSocketAddress(newLocalAddress),
                                  new InternalSocketAddress(newRemoteAddress),
                                  new InternalSocketAddress(oldLocalAddress),
                                  new InternalSocketAddress(oldRemoteAddress),
                                  preferredAddress);
  },

  /**
   * Called when the session generates a new TLS session ticket
   * @param {object} ticket An opaque session ticket
   */
  onSessionTicket(ticket) {
    debug('session ticket callback', this[kOwner]);
    this[kOwner][kSessionTicket](ticket);
  },

  /**
   * Called when the session receives a session version negotiation request
   * @param {*} version
   * @param {*} requestedVersions
   * @param {*} supportedVersions
   */
  onSessionVersionNegotiation(version,
                              requestedVersions,
                              supportedVersions) {
    debug('session version negotiation callback', version, requestedVersions, supportedVersions,
          this[kOwner]);
    this[kOwner][kVersionNegotiation](version, requestedVersions, supportedVersions);
    // Note that immediately following a version negotiation event, the
    // session will be destroyed.
  },

  /**
   * Called when a new stream has been received for the session
   * @param {object} stream The QuicStream C++ handle
   * @param {number} direction The stream direction (0 == bidi, 1 == uni)
   */
  onStreamCreated(stream, direction) {
    const session = this[kOwner];
    // The event is ignored and the stream destroyed if the session has been destroyed.
    debug('stream created callback', session, direction);
    if (session.destroyed) {
      stream.destroy();
      return;
    };
    session[kNewStream](stream, direction);
  },

  // QuicStream callbacks
  onStreamBlocked() {
    debug('stream blocked callback', this[kOwner]);
    // Called when the stream C++ handle has been blocked by flow control.
    this[kOwner][kBlocked]();
  },

  onStreamClose(error) {
    // Called when the stream C++ handle has been closed.
    debug(`stream ${this[kOwner].id} closed callback with error: ${error}`);
    this[kOwner][kFinishClose](error);
  },

  onStreamReset(error) {
    // Called when the stream C++ handle has received a stream reset.
    debug('stream reset callback', this[kOwner], error);
    this[kOwner][kReset](error);
  },

  onStreamHeaders(headers, kind) {
    // Called when the stream C++ handle has received a full block of headers.
    debug(`stream ${this[kOwner].id} headers callback`, headers, kind);
    this[kOwner][kHeaders](headers, kind);
  },

  onStreamTrailers() {
    // Called when the stream C++ handle is ready to receive trailing headers.
    debug('stream want trailers callback', this[kOwner]);
    this[kOwner][kTrailers]();
  },
});

function validateBody(body) {
  // TODO(@jasnell): Support streaming sources
  if (body === undefined) return body;
  if (isArrayBuffer(body)) return ArrayBufferPrototypeTransfer(body);
  if (isArrayBufferView(body)) {
    const size = body.byteLength;
    const offset = body.byteOffset;
    return new Uint8Array(ArrayBufferPrototypeTransfer(body.buffer), offset, size);
  }
  if (isBlob(body)) return body[kBlobHandle];

  throw new ERR_INVALID_ARG_TYPE('options.body', [
    'ArrayBuffer',
    'ArrayBufferView',
    'Blob',
  ], body);
}

class QuicStream {
  /** @type {object} */
  #handle;
  /** @type {QuicSession} */
  #session;
  /** @type {QuicStreamStats} */
  #stats;
  /** @type {QuicStreamState} */
  #state;
  /** @type {number} */
  #direction = undefined;
  /** @type {OnBlockedCallback|undefined} */
  #onblocked = undefined;
  /** @type {OnStreamErrorCallback|undefined} */
  #onreset = undefined;
  /** @type {OnHeadersCallback|undefined} */
  #onheaders = undefined;
  /** @type {OnTrailersCallback|undefined} */
  #ontrailers = undefined;
  /** @type {Promise<void>} */
  #pendingClose = Promise.withResolvers();  // eslint-disable-line node-core/prefer-primordials
  #reader;
  #readable;

  /**
   * @param {symbol} privateSymbol
   * @param {object} handle
   * @param {QuicSession} session
   * @param {number} direction
   */
  constructor(privateSymbol, handle, session, direction) {
    if (privateSymbol !== kPrivateConstructor) {
      throw new ERR_ILLEGAL_CONSTRUCTOR();
    }

    this.#handle = handle;
    this.#handle[kOwner] = this;
    this.#session = session;
    this.#direction = direction;
    this.#stats = new QuicStreamStats(kPrivateConstructor, this.#handle.stats);
    this.#state = new QuicStreamState(kPrivateConstructor, this.#handle.state);
    this.#reader = this.#handle.getReader();

    if (this.pending) {
      debug(`pending ${this.direction} stream created`);
    } else {
      debug(`${this.direction} stream ${this.id} created`);
    }
  }

  get readable() {
    if (this.#readable === undefined) {
      assert(this.#reader);
      this.#readable = createBlobReaderStream(this.#reader);
    }
    return this.#readable;
  }

  /** @type {boolean} */
  get pending() { return this.#state.pending; }

  /** @type {OnBlockedCallback} */
  get onblocked() { return this.#onblocked; }

  set onblocked(fn) {
    if (fn === undefined) {
      this.#onblocked = undefined;
      this.#state.wantsBlock = false;
    } else {
      validateFunction(fn, 'onblocked');
      this.#onblocked = fn.bind(this);
      this.#state.wantsBlock = true;
    }
  }

  /** @type {OnStreamErrorCallback} */
  get onreset() { return this.#onreset; }

  set onreset(fn) {
    if (fn === undefined) {
      this.#onreset = undefined;
      this.#state.wantsReset = false;
    } else {
      validateFunction(fn, 'onreset');
      this.#onreset = fn.bind(this);
      this.#state.wantsReset = true;
    }
  }

  /** @type {OnHeadersCallback} */
  get [kOnHeaders]() { return this.#onheaders; }

  set [kOnHeaders](fn) {
    if (fn === undefined) {
      this.#onheaders = undefined;
      this.#state[kWantsHeaders] = false;
    } else {
      validateFunction(fn, 'onheaders');
      this.#onheaders = fn.bind(this);
      this.#state[kWantsHeaders] = true;
    }
  }

  /** @type {OnTrailersCallback} */
  get [kOnTrailers]() { return this.#ontrailers; }

  set [kOnTrailers](fn) {
    if (fn === undefined) {
      this.#ontrailers = undefined;
      this.#state[kWantsTrailers] = false;
    } else {
      validateFunction(fn, 'ontrailers');
      this.#ontrailers = fn.bind(this);
      this.#state[kWantsTrailers] = true;
    }
  }

  /** @type {QuicStreamStats} */
  get stats() { return this.#stats; }

  /** @type {QuicStreamState} */
  get state() { return this.#state; }

  /** @type {QuicSession} */
  get session() { return this.#session; }

  /**
   * Returns the id for this stream. If the stream is destroyed or still pending,
   * `undefined` will be returned.
   * @type {bigint}
   */
  get id() {
    if (this.destroyed || this.pending) return undefined;
    return this.#state.id;
  }

  /** @type {'bidi'|'uni'} */
  get direction() {
    return this.#direction === STREAM_DIRECTION_BIDIRECTIONAL ? 'bidi' : 'uni';
  }

  /** @returns {boolean} */
  get destroyed() {
    return this.#handle === undefined;
  }

  /** @type {Promise<void>} */
  get closed() {
    return this.#pendingClose.promise;
  }

  /**
   * @param {ArrayBuffer|ArrayBufferView|Blob} outbound
   */
  setOutbound(outbound) {
    if (this.destroyed) {
      throw new ERR_INVALID_STATE('Stream is destroyed');
    }
    if (this.#state.hasOutbound) {
      throw new ERR_INVALID_STATE('Stream already has an outbound data source');
    }
    this.#handle.attachSource(validateBody(outbound));
  }

  /**
   * @param {bigint} code
   */
  stopSending(code = 0n) {
    if (this.destroyed) {
      throw new ERR_INVALID_STATE('Stream is destroyed');
    }
    this.#handle.stopSending(BigInt(code));
  }

  /**
   * @param {bigint} code
   */
  resetStream(code = 0n) {
    if (this.destroyed) {
      throw new ERR_INVALID_STATE('Stream is destroyed');
    }
    this.#handle.resetStream(BigInt(code));
  }

  /** @type {'default' | 'low' | 'high'} */
  get priority() {
    if (this.destroyed || !this.session.state.isPrioritySupported) return undefined;
    switch (this.#handle.getPriority()) {
      case 3: return 'default';
      case 7: return 'low';
      case 0: return 'high';
      default: return 'default';
    }
  }

  set priority(val) {
    if (this.destroyed || !this.session.state.isPrioritySupported) return;
    switch (val) {
      case 'default': this.#handle.setPriority(3, 1); break;
      case 'low': this.#handle.setPriority(7, 1); break;
      case 'high': this.#handle.setPriority(0, 1); break;
    }
    // Otherwise ignore the value as invalid.
  }

  /**
   * Send a block of headers. The headers are formatted as an array
   * of key, value pairs. The reason we don't use a Headers object
   * here is because this needs to be able to represent headers like
   * :method which the high-level Headers API does not allow.
   *
   * Note that QUIC in general does not support headers. This method
   * is in place to support HTTP3 and is therefore not generally
   * exposed except via a private symbol.
   * @param {object} headers
   * @returns {boolean} true if the headers were scheduled to be sent.
   */
  [kSendHeaders](headers) {
    validateObject(headers, 'headers');
    if (this.pending) {
      debug('pending stream enqueing headers', headers);
    } else {
      debug(`stream ${this.id} sending headers`, headers);
    }
    // TODO(@jasnell): Support differentiating between early headers, primary headers, etc
    return this.#handle.sendHeaders(1, mapToHeaders(headers), 1);
  }

  [kFinishClose](error) {
    if (this.destroyed) return this.#pendingClose.promise;
    if (error !== undefined) {
      if (this.pending) {
        debug(`destroying pending stream with error: ${error}`);
      } else {
        debug(`destroying stream ${this.id} with error: ${error}`);
      }
      this.#pendingClose.reject(error);
    } else {
      if (this.pending) {
        debug('destroying pending stream with no error');
      } else {
        debug(`destroying stream ${this.id} with no error`);
      }
      this.#pendingClose.resolve();
    }
    this.#stats[kFinishClose]();
    this.#state[kFinishClose]();
    this.#session[kRemoveStream](this);
    this.#session = undefined;
    this.#pendingClose.reject = undefined;
    this.#pendingClose.resolve = undefined;
    this.#onblocked = undefined;
    this.#onreset = undefined;
    this.#onheaders = undefined;
    this.#ontrailers = undefined;
    this.#handle = undefined;
  }

  [kBlocked]() {
    // The blocked event should only be called if the stream was created with
    // an onblocked callback. The callback should always exist here.
    assert(this.#onblocked, 'Unexpected stream blocked event');
    this.#onblocked();
  }

  [kReset](error) {
    // The reset event should only be called if the stream was created with
    // an onreset callback. The callback should always exist here.
    assert(this.#onreset, 'Unexpected stream reset event');
    this.#onreset(error);
  }

  [kHeaders](headers, kind) {
    // The headers event should only be called if the stream was created with
    // an onheaders callback. The callback should always exist here.
    assert(this.#onheaders, 'Unexpected stream headers event');
    assert(ArrayIsArray(headers));
    assert(headers.length % 2 === 0);
    const block = {
      __proto__: null,
    };
    for (let n = 0; n + 1 < headers.length; n += 2) {
      if (block[headers[n]] !== undefined) {
        block[headers[n]] = [block[headers[n]], headers[n + 1]];
      } else {
        block[headers[n]] = headers[n + 1];
      }
    }

    this.#onheaders(block, kind);
  }

  [kTrailers]() {
    // The trailers event should only be called if the stream was created with
    // an ontrailers callback. The callback should always exist here.
    assert(this.#ontrailers, 'Unexpected stream trailers event');
    this.#ontrailers();
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
      pending: this.pending,
      stats: this.stats,
      state: this.#state,
      session: this.session,
    }, opts)}`;
  }
}

class QuicSession {
  /** @type {QuicEndpoint} */
  #endpoint = undefined;
  /** @type {boolean} */
  #isPendingClose = false;
  /** @type {object|undefined} */
  #handle;
  /** @type {PromiseWithResolvers<void>} */
  #pendingClose = Promise.withResolvers();  // eslint-disable-line node-core/prefer-primordials
  /** @type {PromiseWithResolvers<QuicSessionInfo>} */
  #pendingOpen = Promise.withResolvers();  // eslint-disable-line node-core/prefer-primordials
  /** @type {QuicSessionState} */
  #state;
  /** @type {QuicSessionStats} */
  #stats;
  /** @type {Set<QuicStream>} */
  #streams = new SafeSet();
  /** @type {OnStreamCallback} */
  #onstream = undefined;
  /** @type {OnDatagramCallback|undefined} */
  #ondatagram = undefined;
  /** @type {{}} */
  #sessionticket = undefined;

  /**
   * @param {symbol} privateSymbol
   * @param {object} handle
   * @param {QuicEndpoint} endpoint
   */
  constructor(privateSymbol, handle, endpoint) {
    // Instances of QuicSession can only be created internally.
    if (privateSymbol !== kPrivateConstructor) {
      throw new ERR_ILLEGAL_CONSTRUCTOR();
    }

    this.#endpoint = endpoint;
    this.#handle = handle;
    this.#handle[kOwner] = this;
    this.#stats = new QuicSessionStats(kPrivateConstructor, handle.stats);
    this.#state = new QuicSessionState(kPrivateConstructor, handle.state);
    this.#state.hasVersionNegotiationListener = true;
    this.#state.hasPathValidationListener = true;
    this.#state.hasSessionTicketListener = true;

    debug('session created');
  }

  /** @type {boolean} */
  get #isClosedOrClosing() {
    return this.#handle === undefined || this.#isPendingClose;
  }

  /** @type {any} */
  get sessionticket() { return this.#sessionticket; }

  /** @type {OnStreamCallback} */
  get onstream() { return this.#onstream; }

  set onstream(fn) {
    if (fn === undefined) {
      this.#onstream = undefined;
    } else {
      validateFunction(fn, 'onstream');
      this.#onstream = fn.bind(this);
    }
  }

  /** @type {OnDatagramCallback} */
  get ondatagram() { return this.#ondatagram; }

  set ondatagram(fn) {
    if (fn === undefined) {
      this.#ondatagram = undefined;
      this.#state.hasDatagramListener = false;
    } else {
      validateFunction(fn, 'ondatagram');
      this.#ondatagram = fn.bind(this);
      this.#state.hasDatagramListener = true;
    }
  }

  /** @type {QuicSessionStats} */
  get stats() { return this.#stats; }

  /** @type {QuicSessionState} */
  get [kState]() { return this.#state; }

  /** @type {QuicEndpoint} */
  get endpoint() { return this.#endpoint; }

  /**
   * @param {number} direction
   * @param {OpenStreamOptions} options
   * @returns {QuicStream}
   */
  async #createStream(direction, options = kEmptyObject) {
    if (this.#isClosedOrClosing) {
      throw new ERR_INVALID_STATE('Session is closed. New streams cannot be opened.');
    }
    const dir = direction === STREAM_DIRECTION_BIDIRECTIONAL ? 'bidi' : 'uni';
    if (this.#state.isStreamOpenAllowed) {
      debug(`opening new pending ${dir} stream`);
    } else {
      debug(`opening new ${dir} stream`);
    }

    validateObject(options, 'options');
    const {
      body,
      sendOrder = 50,
      [kHeaders]: headers,
    } = options;
    if (headers !== undefined) {
      validateObject(headers, 'options.headers');
    }

    validateNumber(sendOrder, 'options.sendOrder');
    // TODO(@jasnell): Make use of sendOrder to set the priority

    const validatedBody = validateBody(body);

    const handle = this.#handle.openStream(direction, validatedBody);
    if (handle === undefined) {
      throw new ERR_QUIC_OPEN_STREAM_FAILED();
    }

    if (headers !== undefined) {
      // If headers are specified and there's no body, then we assume
      // that the headers are terminal.
      handle.sendHeaders(1, mapToHeaders(headers),
                         validatedBody === undefined ? 1 : 0);
    }

    const stream = new QuicStream(kPrivateConstructor, handle, this, direction);
    this.#streams.add(stream);

    if (onSessionOpenStreamChannel.hasSubscribers) {
      onSessionOpenStreamChannel.publish({
        stream,
        session: this,
        direction: dir,
      });
    }
    return stream;
  }

  /**
   * @param {OpenStreamOptions} [options]
   * @returns {Promise<QuicStream>}
   */
  async createBidirectionalStream(options = kEmptyObject) {
    return await this.#createStream(STREAM_DIRECTION_BIDIRECTIONAL, options);
  }

  /**
   * @param {OpenStreamOptions} [options]
   * @returns {Promise<QuicStream>}
   */
  async createUnidirectionalStream(options = kEmptyObject) {
    return await this.#createStream(STREAM_DIRECTION_UNIDIRECTIONAL, options);
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
   * @returns {Promise<void>}
   */
  async sendDatagram(datagram) {
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
      const length = datagram.byteLength;
      const offset = datagram.byteOffset;
      datagram = new Uint8Array(ArrayBufferPrototypeTransfer(datagram.buffer),
                                length, offset);
    }

    debug(`sending datagram with ${datagram.byteLength} bytes`);

    const id = this.#handle.sendDatagram(datagram);

    if (onSessionSendDatagramChannel.hasSubscribers) {
      onSessionSendDatagramChannel.publish({
        id,
        length: datagram.byteLength,
        session: this,
      });
    }
  }

  /**
   * Initiate a key update.
   */
  updateKey() {
    if (this.#isClosedOrClosing) {
      throw new ERR_INVALID_STATE('Session is closed');
    }

    debug('updating session key');

    this.#handle.updateKey();
    if (onSessionUpdateKeyChannel.hasSubscribers) {
      onSessionUpdateKeyChannel.publish({
        session: this,
      });
    }
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

      debug('gracefully closing the session');

      this.#handle?.gracefulClose();
      if (onSessionClosingChannel.hasSubscribers) {
        onSessionClosingChannel.publish({
          session: this,
        });
      }
    }
    return this.closed;
  }

  /** @type {Promise<QuicSessionInfo>} */
  get opened() { return this.#pendingOpen.promise; }

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
    if (this.destroyed) return;

    debug('destroying the session');

    // First, forcefully and immediately destroy all open streams, if any.
    for (const stream of this.#streams) {
      stream.destroy(error);
    }
    // The streams should remove themselves when they are destroyed but let's
    // be doubly sure.
    if (this.#streams.size) {
      process.emitWarning(
        `The session is destroyed with ${this.#streams.size} active streams. ` +
        'This should not happen and indicates a bug in Node.js. Please open an ' +
        'issue in the Node.js GitHub repository at https://github.com/nodejs/node ' +
        'to report the problem.',
      );
    }
    this.#streams.clear();

    // Remove this session immediately from the endpoint
    this.#endpoint[kRemoveSession](this);
    this.#endpoint = undefined;
    this.#isPendingClose = false;

    if (error) {
      // If the session is still waiting to be closed, and error
      // is specified, reject the closed promise.
      this.#pendingClose.reject?.(error);
      this.#pendingOpen.reject?.(error);
    } else {
      this.#pendingClose.resolve?.();
    }

    this.#pendingClose.reject = undefined;
    this.#pendingClose.resolve = undefined;
    this.#pendingOpen.reject = undefined;
    this.#pendingOpen.resolve = undefined;

    this.#state[kFinishClose]();
    this.#stats[kFinishClose]();

    this.#onstream = undefined;
    this.#ondatagram = undefined;
    this.#sessionticket = undefined;

    // Destroy the underlying C++ handle
    this.#handle.destroy();
    this.#handle = undefined;

    if (onSessionClosedChannel.hasSubscribers) {
      onSessionClosedChannel.publish({
        session: this,
        error,
      });
    }
  }

  /**
   * @param {number} errorType
   * @param {number} code
   * @param {string} [reason]
   */
  [kFinishClose](errorType, code, reason) {
    // If code is zero, then we closed without an error. Yay! We can destroy
    // safely without specifying an error.
    if (code === 0n) {
      debug('finishing closing the session with no error');
      this.destroy();
      return;
    }

    debug('finishing closing the session with an error', errorType, code, reason);
    // Otherwise, errorType indicates the type of error that occurred, code indicates
    // the specific error, and reason is an optional string describing the error.
    switch (errorType) {
      case 0: /* Transport Error */
        if (code === 0n) {
          this.destroy();
        } else {
          this.destroy(new ERR_QUIC_TRANSPORT_ERROR(code, reason));
        }
        break;
      case 1: /* Application Error */
        if (code === 0n) {
          this.destroy();
        } else {
          this.destroy(new ERR_QUIC_APPLICATION_ERROR(code, reason));
        }
        break;
      case 2: /* Version Negotiation Error */
        this.destroy(new ERR_QUIC_VERSION_NEGOTIATION_ERROR());
        break;
      case 3: /* Idle close */ {
        // An idle close is not really an error. We can just destroy.
        this.destroy();
        break;
      }
    }
  }

  /**
   * @param {Uint8Array} u8
   * @param {boolean} early
   */
  [kDatagram](u8, early) {
    // The datagram event should only be called if the session was created with
    // an ondatagram callback. The callback should always exist here.
    assert(this.#ondatagram, 'Unexpected datagram event');
    if (this.destroyed) return;
    const length = u8.byteLength;
    this.#ondatagram(u8, early);

    if (onSessionReceiveDatagramChannel.hasSubscribers) {
      onSessionReceiveDatagramChannel.publish({
        length,
        early,
        session: this,
      });
    }
  }

  /**
   * @param {bigint} id
   * @param {'lost'|'acknowledged'} status
   */
  [kDatagramStatus](id, status) {
    if (this.destroyed) return;
    if (onSessionReceiveDatagramStatusChannel.hasSubscribers) {
      onSessionReceiveDatagramStatusChannel.publish({
        id,
        status,
        session: this,
      });
    }
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
    if (onSessionPathValidationChannel.hasSubscribers) {
      onSessionPathValidationChannel.publish({
        result,
        newLocalAddress,
        newRemoteAddress,
        oldLocalAddress,
        oldRemoteAddress,
        preferredAddress,
        session: this,
      });
    }
  }

  /**
   * @param {object} ticket
   */
  [kSessionTicket](ticket) {
    if (this.destroyed) return;
    this.#sessionticket = ticket;
    if (onSessionTicketChannel.hasSubscribers) {
      onSessionTicketChannel.publish({
        ticket,
        session: this,
      });
    }
  }

  /**
   * @param {number} version
   * @param {number[]} requestedVersions
   * @param {number[]} supportedVersions
   */
  [kVersionNegotiation](version, requestedVersions, supportedVersions) {
    if (this.destroyed) return;
    this.destroy(new ERR_QUIC_VERSION_NEGOTIATION_ERROR());
    if (onSessionVersionNegotiationChannel.hasSubscribers) {
      onSessionVersionNegotiationChannel.publish({
        version,
        requestedVersions,
        supportedVersions,
        session: this,
      });
    }
  }

  /**
   * @param {string} servername
   * @param {string} protocol
   * @param {string} cipher
   * @param {string} cipherVersion
   * @param {string} validationErrorReason
   * @param {number} validationErrorCode
   */
  [kHandshake](servername, protocol, cipher, cipherVersion, validationErrorReason,
               validationErrorCode) {
    if (this.destroyed || !this.#pendingOpen.resolve) return;

    const addr = this.#handle.getRemoteAddress();

    const info = {
      local: this.#endpoint.address,
      remote: addr !== undefined ?
        new InternalSocketAddress(addr) :
        undefined,
      servername,
      protocol,
      cipher,
      cipherVersion,
      validationErrorReason,
      validationErrorCode,
    };

    this.#pendingOpen.resolve?.(info);
    this.#pendingOpen.resolve = undefined;
    this.#pendingOpen.reject = undefined;

    if (onSessionHandshakeChannel.hasSubscribers) {
      onSessionHandshakeChannel.publish({
        session: this,
        ...info,
      });
    }
  }

  /**
   * @param {object} handle
   * @param {number} direction
   */
  [kNewStream](handle, direction) {
    const stream = new QuicStream(kPrivateConstructor, handle, this, direction);

    // A new stream was received. If we don't have an onstream callback, then
    // there's nothing we can do about it. Destroy the stream in this case.
    if (typeof this.#onstream !== 'function') {
      process.emitWarning('A new stream was received but no onstream callback was provided');
      stream.destroy();
      return;
    }
    this.#streams.add(stream);

    this.#onstream(stream);

    if (onSessionReceivedStreamChannel.hasSubscribers) {
      onSessionReceivedStreamChannel.publish({
        stream,
        session: this,
      });
    }
  }

  [kRemoveStream](stream) {
    this.#streams.delete(stream);
  }

  [kInspect](depth, options) {
    if (depth < 0)
      return this;

    const opts = {
      ...options,
      depth: options.depth == null ? null : options.depth - 1,
    };

    return `QuicSession ${inspect({
      closed: this.closed,
      closing: this.#isPendingClose,
      destroyed: this.destroyed,
      endpoint: this.endpoint,
      path: this.path,
      state: this.#state,
      stats: this.stats,
      streams: this.#streams,
    }, opts)}`;
  }

  async [SymbolAsyncDispose]() { await this.close(); }
}

// The QuicEndpoint represents a local UDP port binding. It can act as both a
// server for receiving peer sessions, or a client for initiating them. The
// local UDP port will be lazily bound only when connect() or listen() are
// called.
class QuicEndpoint {
  /**
   * The local socket address on which the endpoint is listening (lazily created)
   * @type {SocketAddress|undefined}
   */
  #address = undefined;
  /**
   * When true, the endpoint has been marked busy and is temporarily not accepting
   * new sessions (only used when the Endpoint is acting as a server)
   * @type {boolean}
   */
  #busy = false;
  /**
   * The underlying C++ handle for the endpoint. When undefined the endpoint is
   * considered to be closed.
   * @type {object}
   */
  #handle;
  /**
   * True if endpoint.close() has been called and the [kFinishClose] method has
   * not yet been called.
   * @type {boolean}
   */
  #isPendingClose = false;
  /**
   * True if the endpoint is acting as a server and actively listening for connections.
   * @type {boolean}
   */
  #listening = false;
  /**
   * A promise that is resolved when the endpoint has been closed (or rejected if
   * the endpoint closes abruptly due to an error).
   * @type {PromiseWithResolvers<void>}
   */
  #pendingClose = Promise.withResolvers();  // eslint-disable-line node-core/prefer-primordials
  /**
   * If destroy() is called with an error, the error is stored here and used to reject
   * the pendingClose promise when [kFinishClose] is called.
   * @type {any}
   */
  #pendingError = undefined;
  /**
   * The collection of active sessions.
   * @type {Set<QuicSession>}
   */
  #sessions = new SafeSet();
  /**
   * The internal state of the endpoint. Used to efficiently track and update the
   * state of the underlying c++ endpoint handle.
   * @type {QuicEndpointState}
   */
  #state;
  /**
   * The collected statistics for the endpoint.
   * @type {QuicEndpointStats}
   */
  #stats;
  /**
   * The user provided callback that is invoked when a new session is received.
   * (used only when the endpoint is acting as a server)
   * @type {OnSessionCallback}
   */
  #onsession = undefined;

  /**
   * @param {EndpointOptions} options
   * @returns {EndpointOptions}
   */
  #processEndpointOptions(options) {
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
      rxDiagnosticLoss,
      txDiagnosticLoss,
      udpReceiveBufferSize,
      udpSendBufferSize,
      udpTTL,
      validateAddress,
      ipv6Only,
      cc,
      resetTokenSecret,
      tokenSecret,
    } = options;

    // All of the other options will be validated internally by the C++ code
    if (address !== undefined && !SocketAddress.isSocketAddress(address)) {
      if (typeof address === 'string') {
        address = SocketAddress.parse(address);
      } else if (typeof address === 'object' && address !== null) {
        address = new SocketAddress(address);
      } else {
        throw new ERR_INVALID_ARG_TYPE('options.address', ['SocketAddress', 'string'], address);
      }
    }

    return {
      __proto__: null,
      address: address?.[kSocketAddressHandle],
      retryTokenExpiration,
      tokenExpiration,
      maxConnectionsPerHost,
      maxConnectionsTotal,
      maxStatelessResetsPerHost,
      addressLRUSize,
      maxRetries,
      rxDiagnosticLoss,
      txDiagnosticLoss,
      udpReceiveBufferSize,
      udpSendBufferSize,
      udpTTL,
      validateAddress,
      ipv6Only,
      cc,
      resetTokenSecret,
      tokenSecret,
    };
  }

  #newSession(handle) {
    const session = new QuicSession(kPrivateConstructor, handle, this);
    this.#sessions.add(session);
    return session;
  }

  /**
   * @param {EndpointOptions} config
   */
  constructor(config = kEmptyObject) {
    this.#handle = new Endpoint_(this.#processEndpointOptions(config));
    this.#handle[kOwner] = this;
    this.#stats = new QuicEndpointStats(kPrivateConstructor, this.#handle.stats);
    this.#state = new QuicEndpointState(kPrivateConstructor, this.#handle.state);

    if (onEndpointCreatedChannel.hasSubscribers) {
      onEndpointCreatedChannel.publish({
        endpoint: this,
        config,
      });
    }

    debug('endpoint created');
  }

  /**
   * Statistics collected while the endpoint is operational.
   * @type {QuicEndpointStats}
   */
  get stats() { return this.#stats; }

  /** @type {QuicEndpointState} */
  get [kState]() { return this.#state; }

  get #isClosedOrClosing() {
    return this.destroyed || this.#isPendingClose;
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
    // Non-op if there is no change
    if (!!val !== this.#busy) {
      debug('toggling endpoint busy status to ', !this.#busy);
      this.#busy = !this.#busy;
      this.#handle.markBusy(this.#busy);
      if (onEndpointBusyChangeChannel.hasSubscribers) {
        onEndpointBusyChangeChannel.publish({
          endpoint: this,
          busy: this.#busy,
        });
      }
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
   * @param {OnSessionCallback|SessionOptions} [onsession]
   * @param {SessionOptions} [options]
   */
  [kListen](onsession, options) {
    if (this.#isClosedOrClosing) {
      throw new ERR_INVALID_STATE('Endpoint is closed');
    }
    if (this.#listening) {
      throw new ERR_INVALID_STATE('Endpoint is already listening');
    }
    if (this.#state.isBusy) {
      throw new ERR_INVALID_STATE('Endpoint is busy');
    }
    validateObject(options, 'options');
    this.#onsession = onsession.bind(this);

    debug('endpoint listening as a server');
    this.#handle.listen(options);
    this.#listening = true;
  }

  /**
   * Initiates a session with a remote endpoint.
   * @param {{}} address
   * @param {SessionOptions} [options]
   * @returns {QuicSession}
   */
  [kConnect](address, options) {
    if (this.#isClosedOrClosing) {
      throw new ERR_INVALID_STATE('Endpoint is closed');
    }
    if (this.#state.isBusy) {
      throw new ERR_INVALID_STATE('Endpoint is busy');
    }
    validateObject(options, 'options');
    const { sessionTicket, ...rest } = options;

    debug('endpoint connecting as a client');
    const handle = this.#handle.connect(address, rest, sessionTicket);
    if (handle === undefined) {
      throw new ERR_QUIC_CONNECTION_FAILED();
    }
    const session = this.#newSession(handle);

    return session;
  }

  /**
   * Gracefully closes the endpoint. Any existing sessions will be permitted to
   * end gracefully, after which the endpoint will be closed immediately. New
   * sessions will not be accepted or created. The returned promise will be resolved
   * when closing is complete, or will be rejected if the endpoint is closed abruptly
   * due to an error.
   * @returns {Promise<void>} Returns this.closed
   */
  close() {
    if (!this.#isClosedOrClosing) {
      if (onEndpointClosingChannel.hasSubscribers) {
        onEndpointClosingChannel.publish({
          endpoint: this,
          hasPendingError: this.#pendingError !== undefined,
        });
      }
      this.#isPendingClose = true;

      debug('gracefully closing the endpoint');

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

  /**
   * @type {boolean}
   */
  get closing() { return this.#isPendingClose; }

  /** @type {boolean} */
  get destroyed() { return this.#handle === undefined; }

  /**
   * Forcefully terminates the endpoint by immediately destroying all sessions
   * after calling close. If an error is given, the closed promise will be
   * rejected with that error. If no error is given, the closed promise will
   * be resolved.
   * @param {any} [error]
   * @returns {Promise<void>} Returns this.closed
   */
  destroy(error) {
    debug('destroying the endpoint');
    if (!this.#isClosedOrClosing) {
      this.#pendingError = error;
      // Trigger a graceful close of the endpoint that'll ensure that the
      // endpoint is closed down after all sessions are closed... Because
      // we force all sessions to be abruptly destroyed as the next step,
      // the endpoint will be closed immediately after all the sessions
      // are destroyed.
      this.close();
    }
    // Now, force all sessions to be abruptly closed...
    for (const session of this.#sessions) {
      session.destroy(error);
    }
    return this.closed;
  }

  #maybeGetCloseError(context, status) {
    switch (context) {
      case kCloseContextClose: {
        return this.#pendingError;
      }
      case kCloseContextBindFailure: {
        return new ERR_QUIC_ENDPOINT_CLOSED('Bind failure', status);
      }
      case kCloseContextListenFailure: {
        return new ERR_QUIC_ENDPOINT_CLOSED('Listen failure', status);
      }
      case kCloseContextReceiveFailure: {
        return new ERR_QUIC_ENDPOINT_CLOSED('Receive failure', status);
      }
      case kCloseContextSendFailure: {
        return new ERR_QUIC_ENDPOINT_CLOSED('Send failure', status);
      }
      case kCloseContextStartFailure: {
        return new ERR_QUIC_ENDPOINT_CLOSED('Start failure', status);
      }
    }
    // Otherwise return undefined.
  }

  [kFinishClose](context, status) {
    if (this.#handle === undefined) return;
    debug('endpoint is finishing close', context, status);
    this.#handle = undefined;
    this.#stats[kFinishClose]();
    this.#state[kFinishClose]();
    this.#address = undefined;
    this.#busy = false;
    this.#listening = false;
    this.#isPendingClose = false;

    // As QuicSessions are closed they are expected to remove themselves
    // from the sessions collection. Just in case they don't, let's force
    // it by resetting the set so we don't leak memory. Let's emit a warning,
    // tho, if the set is not empty at this point as that would indicate a
    // bug in Node.js that should be fixed.
    if (this.#sessions.size > 0) {
      process.emitWarning(
        `The endpoint is closed with ${this.#sessions.size} active sessions. ` +
        'This should not happen and indicates a bug in Node.js. Please open an ' +
        'issue in the Node.js GitHub repository at https://github.com/nodejs/node ' +
        'to report the problem.',
      );
    }
    this.#sessions.clear();

    // If destroy was called with an error, then the this.#pendingError will be
    // set. Or, if context indicates an error condition that caused the endpoint
    // to be closed, the status will indicate the error code. In either case,
    // we will reject the pending close promise at this point.
    const maybeCloseError = this.#maybeGetCloseError(context, status);
    if (maybeCloseError !== undefined) {
      if (onEndpointErrorChannel.hasSubscribers) {
        onEndpointErrorChannel.publish({
          endpoint: this,
          error: maybeCloseError,
        });
      }
      this.#pendingClose.reject(maybeCloseError);
    } else {
      // Otherwise we are good to resolve the pending close promise!
      this.#pendingClose.resolve();
    }
    if (onEndpointClosedChannel.hasSubscribers) {
      onEndpointClosedChannel.publish({
        endpoint: this,
      });
    }

    // Note that we are intentionally not clearing the
    // this.#pendingClose.promise here.
    this.#pendingClose.resolve = undefined;
    this.#pendingClose.reject = undefined;
    this.#pendingError = undefined;
  }

  [kNewSession](handle) {
    const session = this.#newSession(handle);
    if (onEndpointServerSessionChannel.hasSubscribers) {
      onEndpointServerSessionChannel.publish({
        endpoint: this,
        session,
      });
    }
    assert(typeof this.#onsession === 'function',
           'onsession callback not specified');
    this.#onsession(session);
  }

  // Called by the QuicSession when it closes to remove itself from
  // the active sessions tracked by the QuicEndpoint.
  [kRemoveSession](session) {
    this.#sessions.delete(session);
  }

  async [SymbolAsyncDispose]() { await this.close(); }

  [kInspect](depth, options) {
    if (depth < 0)
      return this;

    const opts = {
      ...options,
      depth: options.depth == null ? null : options.depth - 1,
    };

    return `QuicEndpoint ${inspect({
      address: this.address,
      busy: this.busy,
      closed: this.closed,
      closing: this.#isPendingClose,
      destroyed: this.destroyed,
      listening: this.#listening,
      sessions: this.#sessions,
      stats: this.stats,
      state: this.#state,
    }, opts)}`;
  }
};

function readOnlyConstant(value) {
  return {
    __proto__: null,
    value,
    writable: false,
    configurable: false,
    enumerable: true,
  };
}

/**
 * @param {EndpointOptions} endpoint
 */
function processEndpointOption(endpoint) {
  if (endpoint === undefined) {
    return {
      endpoint: new QuicEndpoint(),
      created: true,
    };
  } else if (endpoint instanceof QuicEndpoint) {
    return {
      endpoint,
      created: false,
    };
  }
  validateObject(endpoint, 'options.endpoint');
  return {
    endpoint: new QuicEndpoint(endpoint),
    created: true,
  };
}

/**
 * @param {SessionOptions} tls
 */
function processTlsOptions(tls, forServer) {
  const {
    servername,
    protocol,
    ciphers = DEFAULT_CIPHERS,
    groups = DEFAULT_GROUPS,
    keylog = false,
    verifyClient = false,
    tlsTrace = false,
    verifyPrivateKey = false,
    keys,
    certs,
    ca,
    crl,
  } = tls;

  if (servername !== undefined) {
    validateString(servername, 'options.servername');
  }
  if (protocol !== undefined) {
    validateString(protocol, 'options.protocol');
  }
  if (ciphers !== undefined) {
    validateString(ciphers, 'options.ciphers');
  }
  if (groups !== undefined) {
    validateString(groups, 'options.groups');
  }
  validateBoolean(keylog, 'options.keylog');
  validateBoolean(verifyClient, 'options.verifyClient');
  validateBoolean(tlsTrace, 'options.tlsTrace');
  validateBoolean(verifyPrivateKey, 'options.verifyPrivateKey');

  if (certs !== undefined) {
    const certInputs = ArrayIsArray(certs) ? certs : [certs];
    for (const cert of certInputs) {
      if (!isArrayBufferView(cert) && !isArrayBuffer(cert)) {
        throw new ERR_INVALID_ARG_TYPE('options.certs',
                                       ['ArrayBufferView', 'ArrayBuffer'], cert);
      }
    }
  }

  if (ca !== undefined) {
    const caInputs = ArrayIsArray(ca) ? ca : [ca];
    for (const caCert of caInputs) {
      if (!isArrayBufferView(caCert) && !isArrayBuffer(caCert)) {
        throw new ERR_INVALID_ARG_TYPE('options.ca',
                                       ['ArrayBufferView', 'ArrayBuffer'], caCert);
      }
    }
  }

  if (crl !== undefined) {
    const crlInputs = ArrayIsArray(crl) ? crl : [crl];
    for (const crlCert of crlInputs) {
      if (!isArrayBufferView(crlCert) && !isArrayBuffer(crlCert)) {
        throw new ERR_INVALID_ARG_TYPE('options.crl',
                                       ['ArrayBufferView', 'ArrayBuffer'], crlCert);
      }
    }
  }

  const keyHandles = [];
  if (keys !== undefined) {
    const keyInputs = ArrayIsArray(keys) ? keys : [keys];
    for (const key of keyInputs) {
      if (isKeyObject(key)) {
        if (key.type !== 'private') {
          throw new ERR_INVALID_ARG_VALUE('options.keys', key, 'must be a private key');
        }
        ArrayPrototypePush(keyHandles, key[kKeyObjectHandle]);
      } else if (isCryptoKey(key)) {
        if (key.type !== 'private') {
          throw new ERR_INVALID_ARG_VALUE('options.keys', key, 'must be a private key');
        }
        ArrayPrototypePush(keyHandles, key[kKeyObjectInner][kKeyObjectHandle]);
      } else {
        throw new ERR_INVALID_ARG_TYPE('options.keys', ['KeyObject', 'CryptoKey'], key);
      }
    }
  }

  // For a server we require key and cert at least
  if (forServer) {
    if (keyHandles.length === 0) {
      throw new ERR_MISSING_ARGS('options.keys');
    }
    if (certs === undefined) {
      throw new ERR_MISSING_ARGS('options.certs');
    }
  }

  return {
    __proto__: null,
    servername,
    protocol,
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
 * @param {SessionOptions} options
 * @param {boolean} [forServer]
 * @returns {SessionOptions}
 */
function processSessionOptions(options, forServer = false) {
  validateObject(options, 'options');
  const {
    endpoint,
    version,
    minVersion,
    preferredAddressPolicy = 'default',
    transportParams = kEmptyObject,
    qlog = false,
    sessionTicket,
    maxPayloadSize,
    unacknowledgedPacketThreshold = 0,
    handshakeTimeout,
    maxStreamWindow,
    maxWindow,
    cc,
    [kApplicationProvider]: provider,
  } = options;

  if (provider !== undefined) {
    validateObject(provider, 'options[kApplicationProvider]');
  }

  if (cc !== undefined) {
    validateString(cc, 'options.cc');
    if (cc !== 'reno' || cc !== 'bbr' || cc !== 'cubic') {
      throw new ERR_INVALID_ARG_VALUE(cc, 'options.cc');
    }
  }

  const {
    endpoint: actualEndpoint,
    created: endpointCreated,
  } = processEndpointOption(endpoint);

  return {
    __proto__: null,
    endpoint: actualEndpoint,
    endpointCreated,
    version,
    minVersion,
    preferredAddressPolicy: getPreferredAddressPolicy(preferredAddressPolicy),
    transportParams,
    tls: processTlsOptions(options, forServer),
    qlog,
    maxPayloadSize,
    unacknowledgedPacketThreshold,
    handshakeTimeout,
    maxStreamWindow,
    maxWindow,
    sessionTicket,
    provider,
    cc,
  };
}

// ============================================================================

/**
 * @param {OnSessionCallback} callback
 * @param {SessionOptions} [options]
 * @returns {Promise<QuicEndpoint>}
 */
async function listen(callback, options = kEmptyObject) {
  validateFunction(callback, 'callback');
  const {
    endpoint,
    ...sessionOptions
  } = processSessionOptions(options, true /* for server */);
  endpoint[kListen](callback, sessionOptions);

  if (onEndpointListeningChannel.hasSubscribers) {
    onEndpointListeningChannel.publish({
      endpoint,
      options,
    });
  }

  return endpoint;
}

/**
 * @param {string|SocketAddress} address
 * @param {SessionOptions} [options]
 * @returns {Promise<QuicSession>}
 */
async function connect(address, options = kEmptyObject) {
  if (typeof address === 'string') {
    address = SocketAddress.parse(address);
  }

  if (!SocketAddress.isSocketAddress(address)) {
    if (address == null || typeof address !== 'object') {
      throw new ERR_INVALID_ARG_TYPE('address', ['SocketAddress', 'string'], address);
    }
    address = new SocketAddress(address);
  }

  const {
    endpoint,
    ...rest
  } = processSessionOptions(options);

  const session = endpoint[kConnect](address[kSocketAddressHandle], rest);

  if (onEndpointClientSessionChannel.hasSubscribers) {
    onEndpointClientSessionChannel.publish({
      endpoint,
      session,
      address,
      options,
    });
  }

  return session;
}

ObjectDefineProperties(QuicEndpoint, {
  Stats: {
    __proto__: null,
    writable: true,
    configurable: true,
    enumerable: true,
    value: QuicEndpointStats,
  },
});
ObjectDefineProperties(QuicSession, {
  Stats: {
    __proto__: null,
    writable: true,
    configurable: true,
    enumerable: true,
    value: QuicSessionStats,
  },
});
ObjectDefineProperties(QuicStream, {
  Stats: {
    __proto__: null,
    writable: true,
    configurable: true,
    enumerable: true,
    value: QuicStreamStats,
  },
});

// ============================================================================

module.exports = {
  listen,
  connect,
  QuicEndpoint,
  QuicSession,
  QuicStream,
  Http3,
};

ObjectDefineProperties(module.exports, {
  CC_ALGO_RENO: readOnlyConstant(CC_ALGO_RENO_STR),
  CC_ALGO_CUBIC: readOnlyConstant(CC_ALGO_CUBIC_STR),
  CC_ALGO_BBR: readOnlyConstant(CC_ALGO_BBR_STR),
  DEFAULT_CIPHERS: readOnlyConstant(DEFAULT_CIPHERS),
  DEFAULT_GROUPS: readOnlyConstant(DEFAULT_GROUPS),
});


/* c8 ignore stop */
