'use strict';

const {
  ObjectDefineProperties,
  ReflectConstruct,
} = primordials;

const {
  ERR_ILLEGAL_CONSTRUCTOR,
  ERR_INVALID_URL,
} = require('internal/errors').codes;

const {
  ReadableStream,
  WritableStream,
  WritableStreamDefaultWriter,
} = require('stream/web');

const {
  validateBoolean,
  validateObject,
  validateOneOf,
  validateUint32,
} = require('internal/validators');

const kReadable = Symbol('kReadable');
const kWritable = Symbol('kWritable');
const kMaxDatagramSize = Symbol('kMaxDatagramSize');
const kIncomingMaxAge = Symbol('kIncomingMaxAge');
const kOutgoingMaxAge = Symbol('kOutgoingMaxAge');
const kIncomingHighWaterMark = Symbol('kIncomingHighWaterMark');
const kOutgoingHighWaterMark = Symbol('kOutgoingHighWaterMark');

/**
 * @typedef {"default"|"throughput"|"low-latency"} WebTransportCongestionControl
 * @typedef {"pending"|"reliable-only"|"supports-unreliable"} WebTransportReliabilityMode
 *
 * @typedef {object} WebTransportHash
 * @property {string} algorithm
 * @property {BufferSource} value
 *
 * @typedef {object} WebTransportOptions
 * @property {boolean} [allowPooling]
 * @property {boolean} [requireUnreliable]
 * @property {sequence<WebTransportHash>} [serverCertificateHashes]
 * @property {WebTransportCongestionControl} [congestionControl]
 * @property {number} [anticipatedConcurrentIncomingUnidirectionalStreams]
 * @property {number} [anticipatedConcurrentIncomingBidirectionalStreams]
 *
 * @typedef {object} WebTransportDatagramStats
 * @property {number} droppedIncoming
 * @property {number} expiredIncoming
 * @property {number} expiredOutgoing
 * @property {number} lostOutgoing
 *
 * @typedef {object} WebTransportConnectionStats
 * @property {number} bytesSent
 * @property {number} packetsSent
 * @property {number} bytesLost
 * @property {number} packetsLost
 * @property {number} bytesReceived
 * @property {number} packetsReceived
 * @property {number} smoothedRtt
 * @property {number} rttVariation
 * @property {number} minRtt
 * @property {WebTransportDatagramStats} datagrams
 * @property {number} estimatedSendRate
 *
 * @typedef {object} WebTransportReceiveStreamStats
 * @property {number} bytesReceived
 * @property {number} bytesRead
 *
 * @typedef {object} WebTransportCloseInfo
 * @property {number} closeCode
 * @property {string} reason
 *
 * @typedef {object} WebTransportSendStreamOptions
 * @property {WebTransportSendGroup} [sendGroup]
 * @property {number} [sendOrder]
 * @property {boolean} [waitUntilAvailable]
 *
 * @callback GetStats
 * @returns {Promise<WebTransportSendStreamStats}
 *
 * @typedef {object} WebTransportSendGroup
 * @property {GetStats} getStats
 *
 * @typedef {"stream"|"session"} WebTransportErrorSource
 *
 * @typedef {object} WebTransportErrorOptions
 * @property {WebTransportErrorSource|undefined} source
 * @property {number|undefined|null} [streamErrorCode]
 */

class WebTransportError extends DOMException {
  /**
   * @type {WebTransportErrorSource}
   */
  #source = 'stream';
  /**
   * @type {number|null}
   */
  #streamErrorCode = null;

  /**
   * @param {string} [message],
   * @param {WebTransportErrorOptions} [options]
   */
  constructor(message = '', options = {}) {
    super(`${message}`, 'WebTransportError');
    validateObject(options, 'options');
    const {
      source = 'stream',
      streamErrorCode = null,
    } = options;
    validateOneOf(source, 'source', ['stream', 'session']);
    if (streamErrorCode != null) {
      validateUint32(streamErrorCode, 'streamErrorCode', true);
    }
    this.#source = source;
    this.#streamErrorCode = streamErrorCode;
  }

  /**
   * @type {WebTransportErrorSource}
   */
  get source() { return this.#source; }

  /**
   * @type {number|null}
   */
  get streamErrorCode() { return this.#streamErrorCode; }
}

class WebTransporDatagramDuplexStream {
  [kReadable] = undefined;
  [kWritable] = undefined;

  constructor() { throw new ERR_ILLEGAL_CONSTRUCTOR(); }

  /**
   * @type {ReadableStream}
   */
  get readable() { return this[kReadable]; }

  /**
   * @type {WritableStream}
   */
  get writable() { return this[kWritable]; }

  /**
   * @type {number}
   */
  get maxDatagramSize() { return this[kMaxDatagramSize]; }

  /**
   * @type {number}
   */
  set maxDatagramSize(value) {
    validateUint32(value, 'maxDatagramSize');
    this[kMaxDatagramSize] = value;
  }

  /**
   * @type {number}
   */
  get incomingMaxAge() { return this[kIncomingMaxAge]; }

  /**
   * @type {number}
   */
  set incomingMaxAge(value) {
    validateUint32(value, 'incomingMaxAge');
    this[kIncomingMaxAge] = value;
  }

  /**
   * @type {number}
   */
  get outgoingMaxAge() { return this[kOutgoingMaxAge]; }

  /**
   * @type {number}
   */
  set outgoingMaxAge(value) {
    validateUint32(value, 'outgoingMaxAge');
    this[kOutgoingMaxAge] = value;
  }

  /**
   * @type {number}
   */
  get incomingHighWaterMark() { return this[kIncomingHighWaterMark]; }

  /**
   * @type {number}
   */
  set incomingHighWaterMark(value) {
    validateUint32(value, 'incomingHighWaterMark');
    this[kIncomingHighWaterMark] = value;
  }

  /**
   * @type {number}
   */
  get outgoingHighWaterMark() { return this[kOutgoingHighWaterMark]; }

  /**
   * @type {number}
   */
  set outgoingHighWaterMark(value) {
    validateUint32(value, 'outgoingHighWaterMark');
    this[kOutgoingHighWaterMark] = value;
  }
};

class WebTransportWriter extends WritableStreamDefaultWriter {
  constructor() { throw new ERR_ILLEGAL_CONSTRUCTOR(); }

  /**
   * @param {any} chunk
   * @returns {Promise<void>}
   */
  async atomicWrite(chunk) {}
};

class WebTransportSendStream extends WritableStream {
  constructor() { throw new ERR_ILLEGAL_CONSTRUCTOR(); }

  /**
   * @type {WebTransportSendGroup|undefined}
   */
  get sendGroup() {}

  /**
   * @type {number}
   */
  get sendOrder() {}

  /**
   * @returns {Promise<WebTransportSendStreamStats}
   */
  async getStats() {}

  /**
   * @returns {WebTransportWriter}
   */
  getWriter() {}
};

class WebTransportReceiveStream extends ReadableStream {
  constructor() { throw new ERR_ILLEGAL_CONSTRUCTOR(); }

  /**
   * @returns {Promise<WebTransportReceiveStreamStats>}
   */
  async getStats() {}
};

class WebTransportBidirectionalStream {
  [kReadable] = undefined;
  [kWritable] = undefined;

  constructor() { throw new ERR_ILLEGAL_CONSTRUCTOR(); }

  /**
   * @type {WebTransportReceiveStream}
   */
  get readable() { return this[kReadable]; }

  /**
   * @type {WebTransportSendStream}
   */
  get writable() { return this[kWritable]; }
};

class WebTransport {
  /**
   * @type {URL}
   */
  #url;
  #ready;
  #closed;
  #draining;
  #datagrams;
  #incomingBidirectionalStreams;
  #incomingUnidirectionalStreams;
  #reliability;
  #supportsReliability;
  #allowPooling;
  #requireUnreliable;
  #serverCertificateHashes;
  #congestionControl;
  #anticipatedConcurrentIncomingBidirectionalStreams;
  #anticipatedConcurrentIncomingUnidirectionalStreams;

  /**
   * @param {string} url
   * @param {WebTransportOptions} [options]
   */
  constructor(url, options = {}) {
    this.#url = URL.parse(url);
    validateObject(options, 'options');
    if (this.#url == null) {
      throw new ERR_INVALID_URL(url);
    }

    const {
      allowPooling = false,
      requireUnreliable = false,
      serverCertificateHashes = [],
      congestionControl = 'default',
      anticipatedConcurrentIncomingBidirectionalStreams = null,
      anticipatedConcurrentIncomingUnidirectionalStreams = null,
    } = options;

    validateBoolean(allowPooling, 'options.allowPooling');
    validateBoolean(requireUnreliable, 'options.requireUnreliable');
    validateOneOf(congestionControl, 'options.congestionControl',
                  ['default'|'throughput'|'low-latency']);
    validateUint32(anticipatedConcurrentIncomingBidirectionalStreams,
                   'options.anticipatedConcurrentIncomingBidirectionalStreams');
    validateUint32(anticipatedConcurrentIncomingUnidirectionalStreams,
                   'options.anticipatedConcurrentIncomingUnidirectionalStreams');
                   // serverCertificateHashes

    const hashes = Array.from(serverCertificateHashes ?? []);
    for (const hash of hashes) {
      validateObject(hash, 'serverCertificateHashes');
    }

    this.#allowPooling = allowPooling;
    this.#requireUnreliable = requireUnreliable;
    this.#serverCertificateHashes = serverCertificateHashes;
    this.#congestionControl = congestionControl;
    this.#anticipatedConcurrentIncomingBidirectionalStreams =
        anticipatedConcurrentIncomingBidirectionalStreams;
    this.#anticipatedConcurrentIncomingUnidirectionalStreams =
        anticipatedConcurrentIncomingUnidirectionalStreams
  }

  /**
   * @type {Promise<void>}
   */
  get ready() { return this.#ready; }

  /**
   * @type {WebTransportReliabilityMode}
   */
  get reliability() { return this.#reliability; }

  /**
   * @type {WebTransportCongestionControl}
   */
  get congestionControl() { return this.#congestionControl; }

  /**
   * @type {number}
   */
  get anticipatedConcurrentIncomingUnidirectionalStreams() {
    return this.#anticipatedConcurrentIncomingUnidirectionalStreams;
  }

  /**
   * @type {number}
   */
  get anticipatedConcurrentIncomingBidirectionalStreams() {
    return this.#anticipatedConcurrentIncomingBidirectionalStreams;
  }

  /**
   * @type {Promise<WebTransportCloseInfo>}
   */
  get closed() { return this.#closed; }

  /**
   * @type {Promise<void>}
   */
  get draining() { return this.#draining; }

  /**
   * @type {WebTransporDatagramDuplexStream}
   */
  get datagrams() { return this.#datagrams; }

  /**
   * @type {ReadableStream}
   */
  get incomingBidirectionalStreams() {
    return this.#incomingBidirectionalStreams;
  }

  /**
   * @type {ReadableStream}
   */
  get incomingUnidirectionalStreams() {
    return this.#incomingUnidirectionalStreams;
  }

  /**
   * @type {boolean}
   */
  get supportsReliableOnly() { return this.#supportsReliability; }

  /**
   *
   * @param {WebTransportCloseInfo} [closeInfo]
   */
  close(closeInfo) {}

  /**
   * @returns {WebTransportSendGroup}
   */
  createSendGroup() {}

  /**
   * @param {WebTransportSendStreamOptions} [options]
   * @returns {Promise<WebTransportBidirectionalStream>}
   */
  async createBidirectionalStream(options = {}) {}

  /**
   * @param {WebTransportSendStreamOptions} [options]
   * @returns {Promise<WebTransportSendStream>}
   */
  async createUnidirectionalStream(options = {}) {}

  /**
   * @returns {Promise<WebTransportConnectionStats>}
   */
  async getStats() {}
}

ObjectDefineProperties(WebTransporDatagramDuplexStream.prototype, {
  [kReadable]: { __proto__: null, writable: true, enumerable: false, },
  [kWritable]: { __proto__: null, writable: true, enumerable: false, },
  [kMaxDatagramSize]: { __proto__: null, writable: true, enumerable: false, },
  [kIncomingMaxAge]: { __proto__: null, writable: true, enumerable: false, },
  [kOutgoingMaxAge]: { __proto__: null, writable: true, enumerable: false, },
  [kIncomingHighWaterMark]: { __proto__: null, writable: true, enumerable: false,},
  [kOutgoingHighWaterMark]: { __proto__: null, writable: true, enumerable: false,},
});

ObjectDefineProperties(WebTransportBidirectionalStream.prototype, {
  [kReadable]: { __proto__: null, writable: true, enumerable: false, },
  [kWritable]: { __proto__: null, writable: true, enumerable: false, },
});

function newWebTransportBidirectionalStream() {
  return ReflectConstruct(function() {}, [],
                          WebTransportBidirectionalStream);
}

function newWebTransporDatagramDuplexStream() {
  return ReflectConstruct(function() {}, [],
                          WebTransporDatagramDuplexStream);
}

module.exports = {
  WebTransport,
  WebTransportError,
  WebTransporDatagramDuplexStream,
  WebTransportSendStream,
  WebTransportReceiveStream,
  WebTransportBidirectionalStream,
  WebTransportWriter,

  newWebTransportBidirectionalStream,
  newWebTransporDatagramDuplexStream,
};
