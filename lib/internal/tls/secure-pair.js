'use strict';

const {
  ReflectConstruct,
  Symbol,
} = primordials;

const EventEmitter = require('events');
const { kEmptyObject } = require('internal/util');
const { Duplex } = require('stream');
const _tls_wrap = require('_tls_wrap');
const _tls_common = require('_tls_common');

const kCallback = Symbol('Callback');
const kOtherSide = Symbol('Other');

class DuplexSocket extends Duplex {
  constructor() {
    super();
    this[kCallback] = null;
    this[kOtherSide] = null;
  }

  _read() {
    const callback = this[kCallback];
    if (callback) {
      this[kCallback] = null;
      callback();
    }
  }

  _write(chunk, encoding, callback) {
    if (chunk.length === 0) {
      process.nextTick(callback);
    } else {
      this[kOtherSide].push(chunk);
      this[kOtherSide][kCallback] = callback;
    }
  }

  _final(callback) {
    this[kOtherSide].on('end', callback);
    this[kOtherSide].push(null);
  }
}

class DuplexPair {
  constructor() {
    this.socket1 = new DuplexSocket();
    this.socket2 = new DuplexSocket();
    this.socket1[kOtherSide] = this.socket2;
    this.socket2[kOtherSide] = this.socket1;
  }
}

class SecurePair extends EventEmitter {
  constructor(secureContext = _tls_common.createSecureContext(),
              isServer = false,
              requestCert = !isServer,
              rejectUnauthorized = false,
              options = kEmptyObject) {
    super();
    const { socket1, socket2 } = new DuplexPair();

    this.server = options.server;
    this.credentials = secureContext;

    this.encrypted = socket1;
    this.cleartext = new _tls_wrap.TLSSocket(socket2, {
      secureContext,
      isServer,
      requestCert,
      rejectUnauthorized,
      ...options,
    });
    this.cleartext.once('secure', () => this.emit('secure'));
  }

  destroy() {
    this.cleartext.destroy();
    this.encrypted.destroy();
  }
}

exports.createSecurePair = function createSecurePair(...args) {
  return ReflectConstruct(SecurePair, args);
};
