// Copyright Joyent, Inc. and other Node contributors.
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit
// persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
// NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
// USE OR OTHER DEALINGS IN THE SOFTWARE.

'use strict';

const {
  ObjectAssign,
  ObjectDefineProperty,
  ObjectSetPrototypeOf,
  ReflectApply,
  RegExp,
  Symbol,
  SymbolFor,
} = primordials;

const {
  assertCrypto,
  deprecate,
  kEmptyObject,
} = require('internal/util');

assertCrypto();

const { setImmediate } = require('timers');
const assert = require('internal/assert');
const crypto = require('crypto');
const EE = require('events');
const net = require('net');
const tls = require('tls');
const common = require('_tls_common');
const { kReinitializeHandle } = require('internal/net');
const JSStreamSocket = require('internal/js_stream_socket');
const { Buffer } = require('buffer');
let debug = require('internal/util/debuglog').debuglog('tls', (fn) => {
  debug = fn;
});
const { TCP, constants: TCPConstants } = internalBinding('tcp_wrap');
const tls_wrap = internalBinding('tls_wrap');
const { Pipe, constants: PipeConstants } = internalBinding('pipe_wrap');
const { owner_symbol } = require('internal/async_hooks').symbols;
const { isArrayBufferView } = require('internal/util/types');
const { SecureContext: NativeSecureContext } = internalBinding('crypto');
const {
  ConnResetException,
  codes: {
    ERR_INVALID_ARG_TYPE,
    ERR_INVALID_ARG_VALUE,
    ERR_MULTIPLE_CALLBACK,
    ERR_SOCKET_CLOSED,
    ERR_TLS_ALPN_CALLBACK_INVALID_RESULT,
    ERR_TLS_ALPN_CALLBACK_WITH_PROTOCOLS,
    ERR_TLS_DH_PARAM_SIZE,
    ERR_TLS_HANDSHAKE_TIMEOUT,
    ERR_TLS_INVALID_CONTEXT,
    ERR_TLS_INVALID_STATE,
    ERR_TLS_RENEGOTIATION_DISABLED,
    ERR_TLS_REQUIRED_SERVER_NAME,
    ERR_TLS_SESSION_ATTACK,
    ERR_TLS_SNI_FROM_SERVER,
  },
} = require('internal/errors');
const { onpskexchange: kOnPskExchange } = internalBinding('symbols');
const {
  getOptionValue,
  getAllowUnauthorized,
} = require('internal/options');
const {
  validateBoolean,
  validateBuffer,
  validateFunction,
  validateInt32,
  validateNumber,
  validateObject,
  validateString,
  validateUint32,
} = require('internal/validators');
const {
  InternalX509Certificate,
} = require('internal/crypto/x509');
const traceTls = getOptionValue('--trace-tls');
const tlsKeylog = getOptionValue('--tls-keylog');
const { appendFile } = require('fs');
const kConnectOptions = Symbol('connect-options');
const kDisableRenegotiation = Symbol('disable-renegotiation');
const kErrorEmitted = Symbol('error-emitted');
const kHandshakeTimeout = Symbol('handshake-timeout');
const kRes = Symbol('res');
const kSNICallback = Symbol('snicallback');
const kALPNCallback = Symbol('alpncallback');
const kEnableTrace = Symbol('enableTrace');
const kPskCallback = Symbol('pskcallback');
const kPskIdentityHint = Symbol('pskidentityhint');
const kPendingSession = Symbol('pendingSession');
const kIsVerified = Symbol('verified');

const noop = () => {};

let ipServernameWarned = false;
let tlsTracingWarned = false;

// Server side times how long a handshake is taking to protect against slow
// handshakes being used for DoS.
function onhandshakestart(now) {
  debug('server onhandshakestart');

  const { lastHandshakeTime } = this;
  assert(now >= lastHandshakeTime,
         `now (${now}) < lastHandshakeTime (${lastHandshakeTime})`);

  this.lastHandshakeTime = now;

  // If this is the first handshake we can skip the rest of the checks.
  if (lastHandshakeTime === 0)
    return;

  if ((now - lastHandshakeTime) >= tls.CLIENT_RENEG_WINDOW * 1000)
    this.handshakes = 1;
  else
    this.handshakes++;

  const owner = this[owner_symbol];

  assert(owner._tlsOptions.isServer);

  if (this.handshakes > tls.CLIENT_RENEG_LIMIT) {
    owner._emitTLSError(new ERR_TLS_SESSION_ATTACK());
    return;
  }

  if (owner[kDisableRenegotiation])
    owner._emitTLSError(new ERR_TLS_RENEGOTIATION_DISABLED());
}

function onhandshakedone() {
  debug('server onhandshakedone');

  const owner = this[owner_symbol];
  assert(owner._tlsOptions.isServer);

  // `newSession` callback wasn't called yet
  if (owner._newSessionPending) {
    owner._securePending = true;
    return;
  }

  owner._finishInit();
}


function loadSession(hello) {
  debug('server onclienthello',
        'sessionid.len', hello.sessionId.length,
        'ticket?', hello.tlsTicket,
  );
  const owner = this[owner_symbol];

  let once = false;
  function onSession(err, session) {
    debug('server resumeSession callback(err %j, sess? %s)', err, !!session);
    if (once)
      return owner.destroy(new ERR_MULTIPLE_CALLBACK());
    once = true;

    if (err)
      return owner.destroy(err);

    if (owner._handle === null)
      return owner.destroy(new ERR_SOCKET_CLOSED());

    owner._handle.loadSession(session);
    // Session is loaded. End the parser to allow handshaking to continue.
    owner._handle.endParser();
  }

  if (hello.sessionId.length <= 0 ||
      hello.tlsTicket ||
      (owner.server &&
      !owner.server.emit('resumeSession', hello.sessionId, onSession))) {
    // Sessions without identifiers can't be resumed.
    // Sessions with tickets can be resumed directly from the ticket, no server
    // session storage is necessary.
    // Without a call to a resumeSession listener, a session will never be
    // loaded, so end the parser to allow handshaking to continue.
    owner._handle.endParser();
  }
}


function loadSNI(info) {
  const owner = this[owner_symbol];
  const servername = info.servername;
  if (!servername || !owner._SNICallback)
    return requestOCSP(owner, info);

  let once = false;
  owner._SNICallback(servername, (err, context) => {
    if (once)
      return owner.destroy(new ERR_MULTIPLE_CALLBACK());
    once = true;

    if (err)
      return owner.destroy(err);

    if (owner._handle === null)
      return owner.destroy(new ERR_SOCKET_CLOSED());

    // TODO(indutny): eventually disallow raw `SecureContext`
    if (context)
      owner._handle.sni_context = context.context || context;

    requestOCSP(owner, info);
  });
}


function callALPNCallback(protocolsBuffer) {
  const handle = this;
  const socket = handle[owner_symbol];

  const servername = handle.getServername();

  // Collect all the protocols from the given buffer:
  const protocols = [];
  let offset = 0;
  while (offset < protocolsBuffer.length) {
    const protocolLen = protocolsBuffer[offset];
    offset += 1;

    const protocol = protocolsBuffer.slice(offset, offset + protocolLen);
    offset += protocolLen;

    protocols.push(protocol.toString('ascii'));
  }

  const selectedProtocol = socket[kALPNCallback]({
    servername,
    protocols,
  });

  // Undefined -> all proposed protocols rejected
  if (selectedProtocol === undefined) return undefined;

  const protocolIndex = protocols.indexOf(selectedProtocol);
  if (protocolIndex === -1) {
    throw new ERR_TLS_ALPN_CALLBACK_INVALID_RESULT(selectedProtocol, protocols);
  }
  let protocolOffset = 0;
  for (let i = 0; i < protocolIndex; i++) {
    protocolOffset += 1 + protocols[i].length;
  }

  return protocolOffset;
}

function requestOCSP(socket, info) {
  if (!info.OCSPRequest || !socket.server)
    return requestOCSPDone(socket);

  let ctx = socket._handle.sni_context;

  if (!ctx) {
    ctx = socket.server._sharedCreds;

    // TLS socket is using a `net.Server` instead of a tls.TLSServer.
    // Some TLS properties like `server._sharedCreds` will not be present
    if (!ctx)
      return requestOCSPDone(socket);
  }

  // TODO(indutny): eventually disallow raw `SecureContext`
  if (ctx.context)
    ctx = ctx.context;

  if (socket.server.listenerCount('OCSPRequest') === 0) {
    return requestOCSPDone(socket);
  }

  let once = false;
  const onOCSP = (err, response) => {
    debug('server OCSPRequest done', 'handle?', !!socket._handle, 'once?', once,
          'response?', !!response, 'err?', err);
    if (once)
      return socket.destroy(new ERR_MULTIPLE_CALLBACK());
    once = true;

    if (err)
      return socket.destroy(err);

    if (socket._handle === null)
      return socket.destroy(new ERR_SOCKET_CLOSED());

    if (response)
      socket._handle.setOCSPResponse(response);
    requestOCSPDone(socket);
  };

  debug('server oncertcb emit OCSPRequest');
  socket.server.emit('OCSPRequest',
                     ctx.getCertificate(),
                     ctx.getIssuer(),
                     onOCSP);
}

function requestOCSPDone(socket) {
  debug('server certcb done');
  try {
    socket._handle.certCbDone();
  } catch (e) {
    debug('server certcb done errored', e);
    socket.destroy(e);
  }
}

function onnewsessionclient(sessionId, session) {
  debug('client emit session');
  const owner = this[owner_symbol];
  if (owner[kIsVerified]) {
    owner.emit('session', session);
  } else {
    owner[kPendingSession] = session;
  }
}

function onnewsession(sessionId, session) {
  debug('onnewsession');
  const owner = this[owner_symbol];

  // TODO(@sam-github) no server to emit the event on, but handshake won't
  // continue unless newSessionDone() is called, should it be, or is that
  // situation unreachable, or only occurring during shutdown?
  if (!owner.server)
    return;

  let once = false;
  const done = () => {
    debug('onnewsession done');
    if (once)
      return;
    once = true;

    if (owner._handle === null)
      return owner.destroy(new ERR_SOCKET_CLOSED());

    this.newSessionDone();

    owner._newSessionPending = false;
    if (owner._securePending)
      owner._finishInit();
    owner._securePending = false;
  };

  owner._newSessionPending = true;
  if (!owner.server.emit('newSession', sessionId, session, done))
    done();
}

function onPskServerCallback(identity, maxPskLen) {
  const owner = this[owner_symbol];
  const ret = owner[kPskCallback](owner, identity);
  if (ret == null)
    return undefined;

  let psk;
  if (isArrayBufferView(ret)) {
    psk = ret;
  } else {
    if (typeof ret !== 'object') {
      throw new ERR_INVALID_ARG_TYPE(
        'ret',
        ['Object', 'Buffer', 'TypedArray', 'DataView'],
        ret,
      );
    }
    psk = ret.psk;
    validateBuffer(psk, 'psk');
  }

  if (psk.length > maxPskLen) {
    throw new ERR_INVALID_ARG_VALUE(
      'psk',
      psk,
      `Pre-shared key exceeds ${maxPskLen} bytes`,
    );
  }

  return psk;
}

function onPskClientCallback(hint, maxPskLen, maxIdentityLen) {
  const owner = this[owner_symbol];
  const ret = owner[kPskCallback](hint);
  if (ret == null)
    return undefined;

  validateObject(ret, 'ret');

  validateBuffer(ret.psk, 'psk');
  if (ret.psk.length > maxPskLen) {
    throw new ERR_INVALID_ARG_VALUE(
      'psk',
      ret.psk,
      `Pre-shared key exceeds ${maxPskLen} bytes`,
    );
  }

  validateString(ret.identity, 'identity');
  if (Buffer.byteLength(ret.identity) > maxIdentityLen) {
    throw new ERR_INVALID_ARG_VALUE(
      'identity',
      ret.identity,
      `PSK identity exceeds ${maxIdentityLen} bytes`,
    );
  }

  return { psk: ret.psk, identity: ret.identity };
}

function onkeylog(line) {
  debug('onkeylog');
  this[owner_symbol].emit('keylog', line);
}

function onocspresponse(resp) {
  debug('client onocspresponse');
  this[owner_symbol].emit('OCSPResponse', resp);
}

function onerror(err) {
  const owner = this[owner_symbol];
  debug('%s onerror %s had? %j',
        (typeof owner._tlsOptions === 'object' && owner._tlsOptions !== null) ?
          owner._tlsOptions.isServer ? 'server' : 'client' :
          'unknown',
        err, owner._hadError);

  if (owner._hadError)
    return;

  owner._hadError = true;

  // Destroy socket if error happened before handshake's finish
  if (!owner._secureEstablished) {
    // When handshake fails control is not yet released,
    // so self._tlsError will return null instead of actual error

    // Set closing the socket after emitting an event since the socket needs to
    // be accessible when the `tlsClientError` event is emitted.
    owner._closeAfterHandlingError = true;
    owner.destroy(err);
  } else if (owner._tlsOptions?.isServer &&
             owner._rejectUnauthorized &&
             /peer did not return a certificate/.test(err.message)) {
    // Ignore server's authorization errors
    owner.destroy();
  } else {
    // Emit error
    owner._emitTLSError(err);
  }
}

// Used by both client and server TLSSockets to start data flowing from _handle,
// read(0) causes a StreamBase::ReadStart, via Socket._read.
function initRead(tlsSocket, socket) {
  debug('%s initRead',
        tlsSocket._tlsOptions.isServer ? 'server' : 'client',
        'handle?', !!tlsSocket._handle,
        'buffered?', !!socket && socket.readableLength,
  );
  // If we were destroyed already don't bother reading
  if (!tlsSocket._handle)
    return;

  // Socket already has some buffered data - emulate receiving it
  if (socket?.readableLength) {
    let buf;
    while ((buf = socket.read()) !== null)
      tlsSocket._handle.receive(buf);
  }

  tlsSocket.read(0);
}

/**
 * Provides a wrap of socket stream to do encrypted communication.
 */

function TLSSocket(socket, opts) {
  const tlsOptions = { ...opts };
  let enableTrace = tlsOptions.enableTrace;

  if (enableTrace == null) {
    enableTrace = traceTls;

    if (enableTrace && !tlsTracingWarned) {
      tlsTracingWarned = true;
      process.emitWarning('Enabling --trace-tls can expose sensitive data in ' +
                          'the resulting log.');
    }
  } else {
    validateBoolean(enableTrace, 'options.enableTrace');
  }

  if (tlsOptions.ALPNProtocols)
    tls.convertALPNProtocols(tlsOptions.ALPNProtocols, tlsOptions);

  this._tlsOptions = tlsOptions;
  this._secureEstablished = false;
  this._securePending = false;
  this._newSessionPending = false;
  this._controlReleased = false;
  this.secureConnecting = true;
  this._SNICallback = null;
  this[kALPNCallback] = null;
  this.servername = null;
  this.alpnProtocol = null;
  this.authorized = false;
  this.authorizationError = null;
  this[kRes] = null;
  this[kIsVerified] = false;
  this[kPendingSession] = null;

  let wrap;
  let handle;
  let wrapHasActiveWriteFromPrevOwner;

  if (socket) {
    if (socket instanceof net.Socket && socket._handle) {
      // 1. connected socket
      wrap = socket;
    } else {
      // 2. socket has no handle so it is js not c++
      // 3. unconnected sockets are wrapped
      // TLS expects to interact from C++ with a net.Socket that has a C++ stream
      // handle, but a JS stream doesn't have one. Wrap it up to make it look like
      // a socket.
      wrap = new JSStreamSocket(socket);
    }

    handle = wrap._handle;
    wrapHasActiveWriteFromPrevOwner = wrap.writableLength > 0;
  } else {
    // 4. no socket, one will be created with net.Socket().connect
    wrap = null;
    wrapHasActiveWriteFromPrevOwner = false;
  }

  // Just a documented property to make secure sockets
  // distinguishable from regular ones.
  this.encrypted = true;

  ReflectApply(net.Socket, this, [{
    handle: this._wrapHandle(wrap, handle, wrapHasActiveWriteFromPrevOwner),
    allowHalfOpen: socket ? socket.allowHalfOpen : tlsOptions.allowHalfOpen,
    pauseOnCreate: tlsOptions.pauseOnConnect,
    manualStart: true,
    highWaterMark: tlsOptions.highWaterMark,
    onread: !socket ? tlsOptions.onread : null,
    signal: tlsOptions.signal,
  }]);

  // Proxy for API compatibility
  this.ssl = this._handle;  // C++ TLSWrap object

  this.on('error', this._tlsError);

  this._init(socket, wrap);

  if (enableTrace && this._handle)
    this._handle.enableTrace();

  if (wrapHasActiveWriteFromPrevOwner) {
    // `wrap` is a streams.Writable in JS. This empty write will be queued
    // and hence finish after all existing writes, which is the timing
    // we want to start to send any tls data to `wrap`.
    wrap.write('', (err) => {
      if (err) {
        debug('error got before writing any tls data to the underlying stream');
        this.destroy(err);
        return;
      }

      this._handle.writesIssuedByPrevListenerDone();
    });
  }

  // Read on next tick so the caller has a chance to setup listeners
  process.nextTick(initRead, this, socket);
}
ObjectSetPrototypeOf(TLSSocket.prototype, net.Socket.prototype);
ObjectSetPrototypeOf(TLSSocket, net.Socket);
exports.TLSSocket = TLSSocket;

const proxiedMethods = [
  'ref', 'unref', 'open', 'bind', 'listen', 'connect', 'bind6',
  'connect6', 'getsockname', 'getpeername', 'setNoDelay', 'setKeepAlive',
  'setSimultaneousAccepts', 'setBlocking',

  // PipeWrap
  'setPendingInstances',
];

// Proxy HandleWrap, PipeWrap and TCPWrap methods
function makeMethodProxy(name) {
  return function methodProxy(...args) {
    if (this._parent[name])
      return ReflectApply(this._parent[name], this._parent, args);
  };
}
for (const proxiedMethod of proxiedMethods) {
  tls_wrap.TLSWrap.prototype[proxiedMethod] =
    makeMethodProxy(proxiedMethod);
}

tls_wrap.TLSWrap.prototype.close = function close(cb) {
  let ssl;
  if (this[owner_symbol]) {
    ssl = this[owner_symbol].ssl;
    this[owner_symbol].ssl = null;
  }

  // Invoke `destroySSL` on close to clean up possibly pending write requests
  // that may self-reference TLSWrap, leading to leak
  const done = () => {
    if (ssl) {
      ssl.destroySSL();
      if (ssl._secureContext.singleUse) {
        ssl._secureContext.context.close();
        ssl._secureContext.context = null;
      }
    }
    if (cb)
      cb();
  };

  if (this._parentWrap) {
    if (this._parentWrap._handle === null) {
      // The socket handle was already closed.
      done();
      return;
    }

    if (this._parentWrap._handle === this._parent) {
      this._parentWrap.once('close', done);
      this._parentWrap.destroy();
      return;
    }
  }

  return this._parent.close(done);
};

TLSSocket.prototype.disableRenegotiation = function disableRenegotiation() {
  this[kDisableRenegotiation] = true;
};

/**
 *
 * @param {null|net.Socket} wrap
 * @param {null|object} handle
 * @param {boolean} wrapHasActiveWriteFromPrevOwner
 * @returns {object}
 */
TLSSocket.prototype._wrapHandle = function(wrap, handle, wrapHasActiveWriteFromPrevOwner) {
  const options = this._tlsOptions;
  if (!handle) {
    handle = options.pipe ?
      new Pipe(PipeConstants.SOCKET) :
      new TCP(TCPConstants.SOCKET);
    handle[owner_symbol] = this;
  }

  // Wrap socket's handle
  const context = options.secureContext ||
                  options.credentials ||
                  tls.createSecureContext(options);
  assert(handle.isStreamBase, 'handle must be a StreamBase');
  if (!(context.context instanceof NativeSecureContext)) {
    throw new ERR_TLS_INVALID_CONTEXT('context');
  }

  const res = tls_wrap.wrap(handle, context.context,
                            !!options.isServer,
                            wrapHasActiveWriteFromPrevOwner);
  res._parent = handle;  // C++ "wrap" object: TCPWrap, JSStream, ...
  res._parentWrap = wrap;  // JS object: net.Socket, JSStreamSocket, ...
  res._secureContext = context;
  res.reading = handle.reading;
  this[kRes] = res;
  defineHandleReading(this, handle);

  // Guard against adding multiple listeners, as this method may be called
  // repeatedly on the same socket by reinitializeHandle
  if (this.listenerCount('close', onSocketCloseDestroySSL) === 0) {
    this.on('close', onSocketCloseDestroySSL);
  }

  if (wrap) {
    wrap.on('close', () => this.destroy());
  }

  return res;
};

TLSSocket.prototype[kReinitializeHandle] = function reinitializeHandle(handle) {
  const originalServername = this.ssl ? this._handle.getServername() : null;
  const originalSession = this.ssl ? this._handle.getSession() : null;

  this.handle = this._wrapHandle(null, handle, false);
  this.ssl = this._handle;

  net.Socket.prototype[kReinitializeHandle].call(this, this.handle);
  this._init();

  if (this._tlsOptions.enableTrace) {
    this._handle.enableTrace();
  }

  if (originalSession) {
    this.setSession(originalSession);
  }

  if (originalServername) {
    this.setServername(originalServername);
  }
};

// This eliminates a cyclic reference to TLSWrap
// Ref: https://github.com/nodejs/node/commit/f7620fb96d339f704932f9bb9a0dceb9952df2d4
function defineHandleReading(socket, handle) {
  ObjectDefineProperty(handle, 'reading', {
    __proto__: null,
    get: () => {
      return socket[kRes].reading;
    },
    set: (value) => {
      socket[kRes].reading = value;
    },
  });
}

function onSocketCloseDestroySSL() {
  // Make sure we are not doing it on OpenSSL's stack
  setImmediate(destroySSL, this);
  this[kRes] = null;
}

function destroySSL(self) {
  self._destroySSL();
}

TLSSocket.prototype._destroySSL = function _destroySSL() {
  if (!this.ssl) return;
  this.ssl.destroySSL();
  if (this.ssl._secureContext.singleUse) {
    this.ssl._secureContext.context.close();
    this.ssl._secureContext.context = null;
  }
  this.ssl = null;
  this[kPendingSession] = null;
  this[kIsVerified] = false;
};

function keylogNewListener(event) {
  if (event !== 'keylog')
    return;

  // Guard against enableKeylogCallback after destroy
  if (!this._handle) return;
  this._handle.enableKeylogCallback();

  // Remove this listener since it's no longer needed.
  this.removeListener('newListener', keylogNewListener);
}

function newListener(event) {
  if (event !== 'session')
    return;

  // Guard against enableSessionCallbacks after destroy
  if (!this._handle) return;
  this._handle.enableSessionCallbacks();

  // Remove this listener since it's no longer needed.
  this.removeListener('newListener', newListener);
}

// Constructor guts, arbitrarily factored out.
let warnOnTlsKeylog = true;
let warnOnTlsKeylogError = true;
TLSSocket.prototype._init = function(socket, wrap) {
  const options = this._tlsOptions;
  const ssl = this._handle;
  this.server = options.server;

  debug('%s _init',
        options.isServer ? 'server' : 'client',
        'handle?', !!ssl,
  );

  // Clients (!isServer) always request a cert, servers request a client cert
  // only on explicit configuration.
  const requestCert = !!options.requestCert || !options.isServer;
  const rejectUnauthorized = !!options.rejectUnauthorized;

  this._requestCert = requestCert;
  this._rejectUnauthorized = rejectUnauthorized;
  if (requestCert || rejectUnauthorized)
    ssl.setVerifyMode(requestCert, rejectUnauthorized);

  // Only call .onkeylog if there is a keylog listener.
  ssl.onkeylog = onkeylog;

  if (this.listenerCount('newListener', keylogNewListener) === 0) {
    this.on('newListener', keylogNewListener);
  }

  if (options.isServer) {
    ssl.onhandshakestart = onhandshakestart;
    ssl.onhandshakedone = onhandshakedone;
    ssl.onclienthello = loadSession;
    ssl.oncertcb = loadSNI;
    ssl.onnewsession = onnewsession;
    ssl.lastHandshakeTime = 0;
    ssl.handshakes = 0;

    if (options.ALPNCallback) {
      validateFunction(options.ALPNCallback, 'options.ALPNCallback');
      this[kALPNCallback] = options.ALPNCallback;
      ssl.ALPNCallback = callALPNCallback;
      ssl.enableALPNCb();
    }

    if (this.server) {
      if (this.server.listenerCount('resumeSession') > 0 ||
          this.server.listenerCount('newSession') > 0) {
        // Also starts the client hello parser as a side effect.
        ssl.enableSessionCallbacks();
      }
      if (this.server.listenerCount('OCSPRequest') > 0)
        ssl.enableCertCb();
    }
  } else {
    ssl.onhandshakestart = noop;
    ssl.onhandshakedone = () => {
      debug('client onhandshakedone');
      this._finishInit();
    };
    ssl.onocspresponse = onocspresponse;

    if (options.session)
      ssl.setSession(options.session);

    ssl.onnewsession = onnewsessionclient;

    // Only call .onnewsession if there is a session listener.
    if (this.listenerCount('newListener', newListener) === 0) {
      this.on('newListener', newListener);
    }
  }

  if (tlsKeylog) {
    if (warnOnTlsKeylog) {
      warnOnTlsKeylog = false;
      process.emitWarning('Using --tls-keylog makes TLS connections insecure ' +
        'by writing secret key material to file ' + tlsKeylog);
    }
    this.on('keylog', (line) => {
      appendFile(tlsKeylog, line, { mode: 0o600 }, (err) => {
        if (err && warnOnTlsKeylogError) {
          warnOnTlsKeylogError = false;
          process.emitWarning('Failed to write TLS keylog (this warning ' +
            'will not be repeated): ' + err);
        }
      });
    });
  }

  ssl.onerror = onerror;

  // If custom SNICallback was given, or if
  // there're SNI contexts to perform match against -
  // set `.onsniselect` callback.
  if (options.isServer &&
      options.SNICallback &&
      (options.SNICallback !== SNICallback ||
       (options.server && options.server._contexts.length))) {
    validateFunction(options.SNICallback, 'options.SNICallback');
    this._SNICallback = options.SNICallback;
    ssl.enableCertCb();
  }

  if (options.ALPNProtocols)
    ssl.setALPNProtocols(options.ALPNProtocols);

  if (options.pskCallback && ssl.enablePskCallback) {
    validateFunction(options.pskCallback, 'pskCallback');

    ssl[kOnPskExchange] = options.isServer ?
      onPskServerCallback : onPskClientCallback;

    this[kPskCallback] = options.pskCallback;
    ssl.enablePskCallback();

    if (options.pskIdentityHint) {
      validateString(options.pskIdentityHint, 'options.pskIdentityHint');
      ssl.setPskIdentityHint(options.pskIdentityHint);
    }
  }

  // We can only come here via [kWrapConnectedHandle]() call that happens
  // if the connection is established with `autoSelectFamily` set to `true`.
  const connectOptions = this[kConnectOptions];
  if (!options.isServer && connectOptions) {
    if (connectOptions.servername) {
      this.setServername(connectOptions.servername);
    }
  }

  if (options.handshakeTimeout > 0)
    this.setTimeout(options.handshakeTimeout, this._handleTimeout);

  if (socket instanceof net.Socket) {
    this._parent = socket;

    // To prevent assertion in afterConnect() and properly kick off readStart
    this.connecting = socket.connecting || !socket._handle;
    socket.once('connect', () => {
      this.connecting = false;
      this.emit('connect');
    });
  }

  // Assume `tls.connect()`
  if (wrap) {
    wrap.on('error', (err) => this._emitTLSError(err));
  } else {
    assert(!socket);
    this.connecting = true;
  }
};

TLSSocket.prototype.renegotiate = function(options, callback) {
  validateObject(options, 'options');
  if (callback !== undefined) {
    validateFunction(callback, 'callback');
  }

  debug('%s renegotiate()',
        this._tlsOptions.isServer ? 'server' : 'client',
        'destroyed?', this.destroyed,
  );

  if (this.destroyed)
    return;

  let requestCert = !!this._requestCert;
  let rejectUnauthorized = !!this._rejectUnauthorized;

  if (options.requestCert !== undefined)
    requestCert = !!options.requestCert;
  if (options.rejectUnauthorized !== undefined)
    rejectUnauthorized = !!options.rejectUnauthorized;

  if (requestCert !== this._requestCert ||
      rejectUnauthorized !== this._rejectUnauthorized) {
    this._handle.setVerifyMode(requestCert, rejectUnauthorized);
    this._requestCert = requestCert;
    this._rejectUnauthorized = rejectUnauthorized;
  }
  // Ensure that we'll cycle through internal openssl's state
  this.write('');

  try {
    this._handle.renegotiate();
  } catch (err) {
    if (callback) {
      process.nextTick(callback, err);
    }
    return false;
  }

  // Ensure that we'll cycle through internal openssl's state
  this.write('');

  if (callback) {
    this.once('secure', () => callback(null));
  }

  return true;
};

TLSSocket.prototype.exportKeyingMaterial = function(length, label, context) {
  validateUint32(length, 'length', true);
  validateString(label, 'label');
  if (context !== undefined)
    validateBuffer(context, 'context');

  if (!this._secureEstablished)
    throw new ERR_TLS_INVALID_STATE();

  return this._handle.exportKeyingMaterial(length, label, context);
};

TLSSocket.prototype.setMaxSendFragment = function setMaxSendFragment(size) {
  validateInt32(size, 'size');
  return this._handle.setMaxSendFragment(size) === 1;
};

TLSSocket.prototype._handleTimeout = function() {
  this._emitTLSError(new ERR_TLS_HANDSHAKE_TIMEOUT());
};

TLSSocket.prototype._emitTLSError = function(err) {
  const e = this._tlsError(err);
  if (e)
    this.emit('error', e);
};

TLSSocket.prototype._tlsError = function(err) {
  this.emit('_tlsError', err);
  if (this._controlReleased)
    return err;
  return null;
};

TLSSocket.prototype._releaseControl = function() {
  if (this._controlReleased)
    return false;
  this._controlReleased = true;
  this.removeListener('error', this._tlsError);
  return true;
};

TLSSocket.prototype._finishInit = function() {
  // Guard against getting onhandshakedone() after .destroy().
  // * 1.2: If destroy() during onocspresponse(), then write of next handshake
  // record fails, the handshake done info callbacks does not occur, and the
  // socket closes.
  // * 1.3: The OCSP response comes in the same record that finishes handshake,
  // so even after .destroy(), the handshake done info callback occurs
  // immediately after onocspresponse(). Ignore it.
  if (!this._handle)
    return;

  this.alpnProtocol = this._handle.getALPNNegotiatedProtocol();
  // The servername could be set by TLSWrap::SelectSNIContextCallback().
  if (this.servername === null) {
    this.servername = this._handle.getServername();
  }

  debug('%s _finishInit',
        this._tlsOptions.isServer ? 'server' : 'client',
        'handle?', !!this._handle,
        'alpn', this.alpnProtocol,
        'servername', this.servername);

  this._secureEstablished = true;
  if (this._tlsOptions.handshakeTimeout > 0)
    this.setTimeout(0, this._handleTimeout);
  this.emit('secure');
};

TLSSocket.prototype._start = function() {
  debug('%s _start',
        this._tlsOptions.isServer ? 'server' : 'client',
        'handle?', !!this._handle,
        'connecting?', this.connecting,
        'requestOCSP?', !!this._tlsOptions.requestOCSP,
  );
  if (this.connecting) {
    this.once('connect', this._start);
    return;
  }

  // Socket was destroyed before the connection was established
  if (!this._handle)
    return;

  if (this._tlsOptions.requestOCSP)
    this._handle.requestOCSP();
  this._handle.start();
};

TLSSocket.prototype.setServername = function(name) {
  validateString(name, 'name');

  if (this._tlsOptions.isServer) {
    throw new ERR_TLS_SNI_FROM_SERVER();
  }

  this._handle.setServername(name);
};

TLSSocket.prototype.setSession = function(session) {
  if (typeof session === 'string')
    session = Buffer.from(session, 'latin1');
  this._handle.setSession(session);
};

TLSSocket.prototype.getPeerCertificate = function(detailed) {
  if (this._handle) {
    return common.translatePeerCertificate(
      this._handle.getPeerCertificate(detailed)) || {};
  }

  return null;
};

TLSSocket.prototype.getCertificate = function() {
  if (this._handle) {
    // It's not a peer cert, but the formatting is identical.
    return common.translatePeerCertificate(
      this._handle.getCertificate()) || {};
  }

  return null;
};

TLSSocket.prototype.getPeerX509Certificate = function(detailed) {
  const cert = this._handle?.getPeerX509Certificate();
  return cert ? new InternalX509Certificate(cert) : undefined;
};

TLSSocket.prototype.getX509Certificate = function() {
  const cert = this._handle?.getX509Certificate();
  return cert ? new InternalX509Certificate(cert) : undefined;
};

TLSSocket.prototype.setKeyCert = function(context) {
  if (this._handle) {
    let secureContext;
    if (context instanceof common.SecureContext)
      secureContext = context;
    else
      secureContext = tls.createSecureContext(context);
    this._handle.setKeyCert(secureContext.context);
  }
};

// Proxy TLSSocket handle methods
function makeSocketMethodProxy(name) {
  return function socketMethodProxy(...args) {
    if (this._handle)
      return ReflectApply(this._handle[name], this._handle, args);
    return null;
  };
}

[
  'getCipher',
  'getSharedSigalgs',
  'getEphemeralKeyInfo',
  'getFinished',
  'getPeerFinished',
  'getProtocol',
  'getSession',
  'getTLSTicket',
  'isSessionReused',
  'enableTrace',
].forEach((method) => {
  TLSSocket.prototype[method] = makeSocketMethodProxy(method);
});

// TODO: support anonymous (nocert)


function onServerSocketSecure() {
  if (this._requestCert) {
    const verifyError = this._handle.verifyError();
    if (verifyError) {
      this.authorizationError = verifyError.code;

      if (this._rejectUnauthorized)
        this.destroy();
    } else {
      this.authorized = true;
    }
  }

  if (!this.destroyed && this._releaseControl()) {
    debug('server emit secureConnection');
    this.secureConnecting = false;
    this._tlsOptions.server.emit('secureConnection', this);
  }
}

function onSocketTLSError(err) {
  if (!this._controlReleased && !this[kErrorEmitted]) {
    this[kErrorEmitted] = true;
    debug('server emit tlsClientError:', err);
    this._tlsOptions.server.emit('tlsClientError', err, this);
  }
}

function onSocketKeylog(line) {
  this._tlsOptions.server.emit('keylog', line, this);
}

function onSocketClose(err) {
  // Closed because of error - no need to emit it twice
  if (err)
    return;

  // Emit ECONNRESET
  if (!this._controlReleased && !this[kErrorEmitted]) {
    this[kErrorEmitted] = true;
    const connReset = new ConnResetException('socket hang up');
    this._tlsOptions.server.emit('tlsClientError', connReset, this);
  }
}

function tlsConnectionListener(rawSocket) {
  debug('net.Server.on(connection): new TLSSocket');
  const socket = new TLSSocket(rawSocket, {
    secureContext: this._sharedCreds,
    isServer: true,
    server: this,
    requestCert: this.requestCert,
    rejectUnauthorized: this.rejectUnauthorized,
    handshakeTimeout: this[kHandshakeTimeout],
    ALPNProtocols: this.ALPNProtocols,
    ALPNCallback: this.ALPNCallback,
    SNICallback: this[kSNICallback] || SNICallback,
    enableTrace: this[kEnableTrace],
    pauseOnConnect: this.pauseOnConnect,
    pskCallback: this[kPskCallback],
    pskIdentityHint: this[kPskIdentityHint],
  });

  socket.on('secure', onServerSocketSecure);

  if (this.listenerCount('keylog') > 0)
    socket.on('keylog', onSocketKeylog);

  socket[kErrorEmitted] = false;
  socket.on('close', onSocketClose);
  socket.on('_tlsError', onSocketTLSError);
}

// AUTHENTICATION MODES
//
// There are several levels of authentication that TLS/SSL supports.
// Read more about this in "man SSL_set_verify".
//
// 1. The server sends a certificate to the client but does not request a
// cert from the client. This is common for most HTTPS servers. The browser
// can verify the identity of the server, but the server does not know who
// the client is. Authenticating the client is usually done over HTTP using
// login boxes and cookies and stuff.
//
// 2. The server sends a cert to the client and requests that the client
// also send it a cert. The client knows who the server is and the server is
// requesting the client also identify themselves. There are several
// outcomes:
//
//   A) verifyError returns null meaning the client's certificate is signed
//   by one of the server's CAs. The server now knows the client's identity
//   and the client is authorized.
//
//   B) For some reason the client's certificate is not acceptable -
//   verifyError returns a string indicating the problem. The server can
//   either (i) reject the client or (ii) allow the client to connect as an
//   unauthorized connection.
//
// The mode is controlled by two boolean variables.
//
// requestCert
//   If true the server requests a certificate from client connections. For
//   the common HTTPS case, users will want this to be false, which is what
//   it defaults to.
//
// rejectUnauthorized
//   If true clients whose certificates are invalid for any reason will not
//   be allowed to make connections. If false, they will simply be marked as
//   unauthorized but secure communication will continue. By default this is
//   true.
//
//
//
// Options:
// - requestCert. Send verify request. Default to false.
// - rejectUnauthorized. Boolean, default to true.
// - key. string.
// - cert: string.
// - clientCertEngine: string.
// - ca: string or array of strings.
// - sessionTimeout: integer.
//
// emit 'secureConnection'
//   function (tlsSocket) { }
//
//   "UNABLE_TO_GET_ISSUER_CERT", "UNABLE_TO_GET_CRL",
//   "UNABLE_TO_DECRYPT_CERT_SIGNATURE", "UNABLE_TO_DECRYPT_CRL_SIGNATURE",
//   "UNABLE_TO_DECODE_ISSUER_PUBLIC_KEY", "CERT_SIGNATURE_FAILURE",
//   "CRL_SIGNATURE_FAILURE", "CERT_NOT_YET_VALID" "CERT_HAS_EXPIRED",
//   "CRL_NOT_YET_VALID", "CRL_HAS_EXPIRED" "ERROR_IN_CERT_NOT_BEFORE_FIELD",
//   "ERROR_IN_CERT_NOT_AFTER_FIELD", "ERROR_IN_CRL_LAST_UPDATE_FIELD",
//   "ERROR_IN_CRL_NEXT_UPDATE_FIELD", "OUT_OF_MEM",
//   "DEPTH_ZERO_SELF_SIGNED_CERT", "SELF_SIGNED_CERT_IN_CHAIN",
//   "UNABLE_TO_GET_ISSUER_CERT_LOCALLY", "UNABLE_TO_VERIFY_LEAF_SIGNATURE",
//   "CERT_CHAIN_TOO_LONG", "CERT_REVOKED" "INVALID_CA",
//   "PATH_LENGTH_EXCEEDED", "INVALID_PURPOSE" "CERT_UNTRUSTED",
//   "CERT_REJECTED"
//
function Server(options, listener) {
  if (!(this instanceof Server))
    return new Server(options, listener);

  if (typeof options === 'function') {
    listener = options;
    options = kEmptyObject;
  } else if (options == null || typeof options === 'object') {
    options ??= kEmptyObject;
  } else {
    throw new ERR_INVALID_ARG_TYPE('options', 'Object', options);
  }

  this._contexts = [];
  this.requestCert = options.requestCert === true;
  this.rejectUnauthorized = options.rejectUnauthorized !== false;

  this.ALPNCallback = options.ALPNCallback;
  if (this.ALPNCallback && options.ALPNProtocols) {
    throw new ERR_TLS_ALPN_CALLBACK_WITH_PROTOCOLS();
  }

  if (options.sessionTimeout)
    this.sessionTimeout = options.sessionTimeout;

  if (options.ticketKeys)
    this.ticketKeys = options.ticketKeys;

  if (options.ALPNProtocols)
    tls.convertALPNProtocols(options.ALPNProtocols, this);

  this.setSecureContext(options);

  this[kHandshakeTimeout] = options.handshakeTimeout || (120 * 1000);
  this[kSNICallback] = options.SNICallback;
  this[kPskCallback] = options.pskCallback;
  this[kPskIdentityHint] = options.pskIdentityHint;

  validateNumber(this[kHandshakeTimeout], 'options.handshakeTimeout');

  if (this[kSNICallback]) {
    validateFunction(this[kSNICallback], 'options.SNICallback');
  }

  if (this[kPskCallback]) {
    validateFunction(this[kPskCallback], 'options.pskCallback');
  }

  if (this[kPskIdentityHint]) {
    validateString(this[kPskIdentityHint], 'options.pskIdentityHint');
  }

  // constructor call
  ReflectApply(net.Server, this, [options, tlsConnectionListener]);

  if (listener) {
    this.on('secureConnection', listener);
  }

  this[kEnableTrace] = options.enableTrace;
}

ObjectSetPrototypeOf(Server.prototype, net.Server.prototype);
ObjectSetPrototypeOf(Server, net.Server);
exports.Server = Server;
exports.createServer = function createServer(options, listener) {
  return new Server(options, listener);
};


Server.prototype.setSecureContext = function(options) {
  validateObject(options, 'options');

  if (options.pfx)
    this.pfx = options.pfx;
  else
    this.pfx = undefined;

  if (options.key)
    this.key = options.key;
  else
    this.key = undefined;

  if (options.passphrase)
    this.passphrase = options.passphrase;
  else
    this.passphrase = undefined;

  if (options.cert)
    this.cert = options.cert;
  else
    this.cert = undefined;

  if (options.clientCertEngine)
    this.clientCertEngine = options.clientCertEngine;
  else
    this.clientCertEngine = undefined;

  if (options.ca)
    this.ca = options.ca;
  else
    this.ca = undefined;

  if (options.minVersion)
    this.minVersion = options.minVersion;
  else
    this.minVersion = undefined;

  if (options.maxVersion)
    this.maxVersion = options.maxVersion;
  else
    this.maxVersion = undefined;

  if (options.secureProtocol)
    this.secureProtocol = options.secureProtocol;
  else
    this.secureProtocol = undefined;

  if (options.crl)
    this.crl = options.crl;
  else
    this.crl = undefined;

  this.sigalgs = options.sigalgs;

  if (options.ciphers)
    this.ciphers = options.ciphers;
  else
    this.ciphers = undefined;

  this.ecdhCurve = options.ecdhCurve;

  if (options.dhparam)
    this.dhparam = options.dhparam;
  else
    this.dhparam = undefined;

  if (options.honorCipherOrder !== undefined)
    this.honorCipherOrder = !!options.honorCipherOrder;
  else
    this.honorCipherOrder = true;

  const secureOptions = options.secureOptions || 0;

  if (secureOptions)
    this.secureOptions = secureOptions;
  else
    this.secureOptions = undefined;

  if (options.sessionIdContext) {
    this.sessionIdContext = options.sessionIdContext;
  } else {
    this.sessionIdContext = crypto.createHash('sha1')
      .update(process.argv.join(' '))
      .digest('hex')
      .slice(0, 32);
  }

  if (options.sessionTimeout)
    this.sessionTimeout = options.sessionTimeout;

  if (options.ticketKeys)
    this.ticketKeys = options.ticketKeys;

  this.privateKeyIdentifier = options.privateKeyIdentifier;
  this.privateKeyEngine = options.privateKeyEngine;

  this._sharedCreds = tls.createSecureContext({
    pfx: this.pfx,
    key: this.key,
    passphrase: this.passphrase,
    cert: this.cert,
    clientCertEngine: this.clientCertEngine,
    ca: this.ca,
    ciphers: this.ciphers,
    sigalgs: this.sigalgs,
    ecdhCurve: this.ecdhCurve,
    dhparam: this.dhparam,
    minVersion: this.minVersion,
    maxVersion: this.maxVersion,
    secureProtocol: this.secureProtocol,
    secureOptions: this.secureOptions,
    honorCipherOrder: this.honorCipherOrder,
    crl: this.crl,
    sessionIdContext: this.sessionIdContext,
    ticketKeys: this.ticketKeys,
    sessionTimeout: this.sessionTimeout,
    privateKeyIdentifier: this.privateKeyIdentifier,
    privateKeyEngine: this.privateKeyEngine,
  });
};


Server.prototype._getServerData = function() {
  return {
    ticketKeys: this.getTicketKeys().toString('hex'),
  };
};


Server.prototype._setServerData = function(data) {
  this.setTicketKeys(Buffer.from(data.ticketKeys, 'hex'));
};


Server.prototype.getTicketKeys = function getTicketKeys() {
  return this._sharedCreds.context.getTicketKeys();
};


Server.prototype.setTicketKeys = function setTicketKeys(keys) {
  validateBuffer(keys);
  assert(keys.byteLength === 48,
         'Session ticket keys must be a 48-byte buffer');
  this._sharedCreds.context.setTicketKeys(keys);
};


Server.prototype.setOptions = deprecate(function(options) {
  this.requestCert = options.requestCert === true;
  this.rejectUnauthorized = options.rejectUnauthorized !== false;

  if (options.pfx) this.pfx = options.pfx;
  if (options.key) this.key = options.key;
  if (options.passphrase) this.passphrase = options.passphrase;
  if (options.cert) this.cert = options.cert;
  if (options.clientCertEngine)
    this.clientCertEngine = options.clientCertEngine;
  if (options.ca) this.ca = options.ca;
  if (options.minVersion) this.minVersion = options.minVersion;
  if (options.maxVersion) this.maxVersion = options.maxVersion;
  if (options.secureProtocol) this.secureProtocol = options.secureProtocol;
  if (options.crl) this.crl = options.crl;
  if (options.ciphers) this.ciphers = options.ciphers;
  if (options.ecdhCurve !== undefined)
    this.ecdhCurve = options.ecdhCurve;
  if (options.dhparam) this.dhparam = options.dhparam;
  if (options.sessionTimeout) this.sessionTimeout = options.sessionTimeout;
  if (options.ticketKeys) this.ticketKeys = options.ticketKeys;
  const secureOptions = options.secureOptions || 0;
  if (options.honorCipherOrder !== undefined)
    this.honorCipherOrder = !!options.honorCipherOrder;
  else
    this.honorCipherOrder = true;
  if (secureOptions) this.secureOptions = secureOptions;
  if (options.ALPNProtocols)
    tls.convertALPNProtocols(options.ALPNProtocols, this);
  if (options.sessionIdContext) {
    this.sessionIdContext = options.sessionIdContext;
  } else {
    this.sessionIdContext = crypto.createHash('sha1')
      .update(process.argv.join(' '))
      .digest('hex')
      .slice(0, 32);
  }
  if (options.pskCallback) this[kPskCallback] = options.pskCallback;
  if (options.pskIdentityHint) this[kPskIdentityHint] = options.pskIdentityHint;
  if (options.sigalgs) this.sigalgs = options.sigalgs;
  if (options.privateKeyIdentifier !== undefined)
    this.privateKeyIdentifier = options.privateKeyIdentifier;
  if (options.privateKeyEngine !== undefined)
    this.privateKeyEngine = options.privateKeyEngine;
}, 'Server.prototype.setOptions() is deprecated', 'DEP0122');

// SNI Contexts High-Level API
Server.prototype.addContext = function(servername, context) {
  if (!servername) {
    throw new ERR_TLS_REQUIRED_SERVER_NAME();
  }

  const re = new RegExp(`^${
    servername
      .replace(/([.^$+?\-\\[\]{}])/g, '\\$1')
      .replaceAll('*', '[^.]*')
  }$`);

  const secureContext =
    context instanceof common.SecureContext ? context : tls.createSecureContext(context);
  this._contexts.push([re, secureContext.context]);
};

Server.prototype[EE.captureRejectionSymbol] = function(
  err, event, sock) {

  switch (event) {
    case 'secureConnection':
      sock.destroy(err);
      break;
    default:
      ReflectApply(net.Server.prototype[SymbolFor('nodejs.rejection')], this,
                   [err, event, sock]);
  }
};

function SNICallback(servername, callback) {
  const contexts = this.server._contexts;

  for (let i = contexts.length - 1; i >= 0; --i) {
    const elem = contexts[i];
    if (elem[0].test(servername)) {
      callback(null, elem[1]);
      return;
    }
  }

  callback(null, undefined);
}


// Target API:
//
//  let s = tls.connect({port: 8000, host: "google.com"}, function() {
//    if (!s.authorized) {
//      s.destroy();
//      return;
//    }
//
//    // s.socket;
//
//    s.end("hello world\n");
//  });
//
//
function normalizeConnectArgs(listArgs) {
  const args = net._normalizeArgs(listArgs);
  const options = args[0];
  const cb = args[1];

  // If args[0] was options, then normalize dealt with it.
  // If args[0] is port, or args[0], args[1] is host, port, we need to
  // find the options and merge them in, normalize's options has only
  // the host/port/path args that it knows about, not the tls options.
  // This means that options.host overrides a host arg.
  if (listArgs[1] !== null && typeof listArgs[1] === 'object') {
    ObjectAssign(options, listArgs[1]);
  } else if (listArgs[2] !== null && typeof listArgs[2] === 'object') {
    ObjectAssign(options, listArgs[2]);
  }

  return cb ? [options, cb] : [options];
}

function onConnectSecure() {
  const options = this[kConnectOptions];

  // Check the size of DHE parameter above minimum requirement
  // specified in options.
  const ekeyinfo = this.getEphemeralKeyInfo();
  if (ekeyinfo.type === 'DH' && ekeyinfo.size < options.minDHSize) {
    const err = new ERR_TLS_DH_PARAM_SIZE(ekeyinfo.size);
    debug('client emit:', err);
    this.emit('error', err);
    this.destroy();
    return;
  }

  let verifyError = this._handle.verifyError();

  // Verify that server's identity matches it's certificate's names
  // Unless server has resumed our existing session
  if (!verifyError && !this.isSessionReused()) {
    const hostname = options.servername ||
                   options.host ||
                   (options.socket?._host) ||
                   'localhost';
    const cert = this.getPeerCertificate(true);
    verifyError = options.checkServerIdentity(hostname, cert);
  }

  if (verifyError) {
    this.authorized = false;
    this.authorizationError = verifyError.code || verifyError.message;

    // rejectUnauthorized property can be explicitly defined as `undefined`
    // causing the assignment to default value (`true`) fail. Before assigning
    // it to the tlssock connection options, explicitly check if it is false
    // and update rejectUnauthorized property. The property gets used by
    // TLSSocket connection handler to allow or reject connection if
    // unauthorized.
    // This check is potentially redundant, however it is better to keep it
    // in case the option object gets modified somewhere.
    if (options.rejectUnauthorized !== false) {
      this.destroy(verifyError);
      return;
    }
    debug('client emit secureConnect. rejectUnauthorized: %s, ' +
          'authorizationError: %s', options.rejectUnauthorized,
          this.authorizationError);
  } else {
    this.authorized = true;
    debug('client emit secureConnect. authorized:', this.authorized);
  }
  this.secureConnecting = false;
  this.emit('secureConnect');

  this[kIsVerified] = true;
  const session = this[kPendingSession];
  this[kPendingSession] = null;
  if (session)
    this.emit('session', session);

  this.removeListener('end', onConnectEnd);
}

function onConnectEnd() {
  // NOTE: This logic is shared with _http_client.js
  if (!this._hadError) {
    const options = this[kConnectOptions];
    this._hadError = true;
    const error = new ConnResetException('Client network socket disconnected ' +
                                         'before secure TLS connection was ' +
                                         'established');
    error.path = options.path;
    error.host = options.host;
    error.port = options.port;
    error.localAddress = options.localAddress;
    this.destroy(error);
  }
}

// Arguments: [port,] [host,] [options,] [cb]
exports.connect = function connect(...args) {
  args = normalizeConnectArgs(args);
  let options = args[0];
  const cb = args[1];
  const allowUnauthorized = getAllowUnauthorized();

  options = {
    rejectUnauthorized: !allowUnauthorized,
    ciphers: tls.DEFAULT_CIPHERS,
    checkServerIdentity: tls.checkServerIdentity,
    minDHSize: 1024,
    ...options,
  };

  if (!options.keepAlive)
    options.singleUse = true;

  validateFunction(options.checkServerIdentity, 'options.checkServerIdentity');
  validateNumber(options.minDHSize, 'options.minDHSize', 1);

  const context = options.secureContext || tls.createSecureContext(options);

  const tlssock = new TLSSocket(options.socket, {
    allowHalfOpen: options.allowHalfOpen,
    pipe: !!options.path,
    secureContext: context,
    isServer: false,
    requestCert: true,
    rejectUnauthorized: options.rejectUnauthorized !== false,
    session: options.session,
    ALPNProtocols: options.ALPNProtocols,
    requestOCSP: options.requestOCSP,
    enableTrace: options.enableTrace,
    pskCallback: options.pskCallback,
    highWaterMark: options.highWaterMark,
    onread: options.onread,
    signal: options.signal,
  });

  // rejectUnauthorized property can be explicitly defined as `undefined`
  // causing the assignment to default value (`true`) fail. Before assigning
  // it to the tlssock connection options, explicitly check if it is false
  // and update rejectUnauthorized property. The property gets used by TLSSocket
  // connection handler to allow or reject connection if unauthorized
  options.rejectUnauthorized = options.rejectUnauthorized !== false;

  tlssock[kConnectOptions] = options;

  if (cb)
    tlssock.once('secureConnect', cb);

  if (!options.socket) {
    // If user provided the socket, it's their responsibility to manage its
    // connectivity. If we created one internally, we connect it.
    if (options.timeout) {
      tlssock.setTimeout(options.timeout);
    }

    tlssock.connect(options, tlssock._start);
  }

  tlssock._releaseControl();

  if (options.session)
    tlssock.setSession(options.session);

  if (options.servername) {
    if (!ipServernameWarned && net.isIP(options.servername)) {
      process.emitWarning(
        'Setting the TLS ServerName to an IP address is not permitted by ' +
        'RFC 6066. This will be ignored in a future version.',
        'DeprecationWarning',
        'DEP0123',
      );
      ipServernameWarned = true;
    }
    tlssock.setServername(options.servername);
  }

  if (options.socket)
    tlssock._start();

  tlssock.on('secure', onConnectSecure);
  tlssock.prependListener('end', onConnectEnd);

  return tlssock;
};
