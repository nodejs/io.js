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
  ArrayIsArray,
  Error,
  MathMin,
  ObjectKeys,
  ObjectSetPrototypeOf,
  ReflectApply,
  Symbol,
  SymbolAsyncDispose,
  SymbolFor,
} = primordials;

const net = require('net');
const EE = require('events');
const assert = require('internal/assert');
const {
  parsers,
  freeParser,
  continueExpression,
  chunkExpression,
  kIncomingMessage,
  HTTPParser,
  isLenient,
  _checkInvalidHeaderChar: checkInvalidHeaderChar,
  prepareError,
} = require('_http_common');
const { ConnectionsList } = internalBinding('http_parser');
const {
  kUniqueHeaders,
  parseUniqueHeadersOption,
  OutgoingMessage,
} = require('_http_outgoing');
const {
  kOutHeaders,
  kNeedDrain,
  isTraceHTTPEnabled,
  traceBegin,
  traceEnd,
  getNextTraceEventId,
} = require('internal/http');
const {
  defaultTriggerAsyncIdScope,
  getOrSetAsyncId,
} = require('internal/async_hooks');
const { IncomingMessage } = require('_http_incoming');
const {
  ConnResetException,
  codes: {
    ERR_HTTP_HEADERS_SENT,
    ERR_HTTP_INVALID_STATUS_CODE,
    ERR_HTTP_REQUEST_TIMEOUT,
    ERR_HTTP_SOCKET_ASSIGNED,
    ERR_HTTP_SOCKET_ENCODING,
    ERR_INVALID_ARG_VALUE,
    ERR_INVALID_CHAR,
    ERR_OUT_OF_RANGE,
  },
} = require('internal/errors');
const {
  kEmptyObject,
  promisify,
} = require('internal/util');
const {
  validateInteger,
  validateBoolean,
  validateLinkHeaderValue,
  validateObject,
} = require('internal/validators');
const Buffer = require('buffer').Buffer;
const { setInterval, clearInterval } = require('timers');
let debug = require('internal/util/debuglog').debuglog('http', (fn) => {
  debug = fn;
});

const dc = require('diagnostics_channel');
const onRequestStartChannel = dc.channel('http.server.request.start');
const onResponseCreatedChannel = dc.channel('http.server.response.created');
const onResponseFinishChannel = dc.channel('http.server.response.finish');

const kServerResponse = Symbol('ServerResponse');
const kServerResponseStatistics = Symbol('ServerResponseStatistics');

const {
  hasObserver,
  startPerf,
  stopPerf,
} = require('internal/perf/observe');

const STATUS_CODES = {
  100: 'Continue',                   // RFC 7231 6.2.1
  101: 'Switching Protocols',        // RFC 7231 6.2.2
  102: 'Processing',                 // RFC 2518 10.1 (obsoleted by RFC 4918)
  103: 'Early Hints',                // RFC 8297 2
  200: 'OK',                         // RFC 7231 6.3.1
  201: 'Created',                    // RFC 7231 6.3.2
  202: 'Accepted',                   // RFC 7231 6.3.3
  203: 'Non-Authoritative Information', // RFC 7231 6.3.4
  204: 'No Content',                 // RFC 7231 6.3.5
  205: 'Reset Content',              // RFC 7231 6.3.6
  206: 'Partial Content',            // RFC 7233 4.1
  207: 'Multi-Status',               // RFC 4918 11.1
  208: 'Already Reported',           // RFC 5842 7.1
  226: 'IM Used',                    // RFC 3229 10.4.1
  300: 'Multiple Choices',           // RFC 7231 6.4.1
  301: 'Moved Permanently',          // RFC 7231 6.4.2
  302: 'Found',                      // RFC 7231 6.4.3
  303: 'See Other',                  // RFC 7231 6.4.4
  304: 'Not Modified',               // RFC 7232 4.1
  305: 'Use Proxy',                  // RFC 7231 6.4.5
  307: 'Temporary Redirect',         // RFC 7231 6.4.7
  308: 'Permanent Redirect',         // RFC 7238 3
  400: 'Bad Request',                // RFC 7231 6.5.1
  401: 'Unauthorized',               // RFC 7235 3.1
  402: 'Payment Required',           // RFC 7231 6.5.2
  403: 'Forbidden',                  // RFC 7231 6.5.3
  404: 'Not Found',                  // RFC 7231 6.5.4
  405: 'Method Not Allowed',         // RFC 7231 6.5.5
  406: 'Not Acceptable',             // RFC 7231 6.5.6
  407: 'Proxy Authentication Required', // RFC 7235 3.2
  408: 'Request Timeout',            // RFC 7231 6.5.7
  409: 'Conflict',                   // RFC 7231 6.5.8
  410: 'Gone',                       // RFC 7231 6.5.9
  411: 'Length Required',            // RFC 7231 6.5.10
  412: 'Precondition Failed',        // RFC 7232 4.2
  413: 'Payload Too Large',          // RFC 7231 6.5.11
  414: 'URI Too Long',               // RFC 7231 6.5.12
  415: 'Unsupported Media Type',     // RFC 7231 6.5.13
  416: 'Range Not Satisfiable',      // RFC 7233 4.4
  417: 'Expectation Failed',         // RFC 7231 6.5.14
  418: 'I\'m a Teapot',              // RFC 7168 2.3.3
  421: 'Misdirected Request',        // RFC 7540 9.1.2
  422: 'Unprocessable Entity',       // RFC 4918 11.2
  423: 'Locked',                     // RFC 4918 11.3
  424: 'Failed Dependency',          // RFC 4918 11.4
  425: 'Too Early',                  // RFC 8470 5.2
  426: 'Upgrade Required',           // RFC 2817 and RFC 7231 6.5.15
  428: 'Precondition Required',      // RFC 6585 3
  429: 'Too Many Requests',          // RFC 6585 4
  431: 'Request Header Fields Too Large', // RFC 6585 5
  451: 'Unavailable For Legal Reasons', // RFC 7725 3
  500: 'Internal Server Error',      // RFC 7231 6.6.1
  501: 'Not Implemented',            // RFC 7231 6.6.2
  502: 'Bad Gateway',                // RFC 7231 6.6.3
  503: 'Service Unavailable',        // RFC 7231 6.6.4
  504: 'Gateway Timeout',            // RFC 7231 6.6.5
  505: 'HTTP Version Not Supported', // RFC 7231 6.6.6
  506: 'Variant Also Negotiates',    // RFC 2295 8.1
  507: 'Insufficient Storage',       // RFC 4918 11.5
  508: 'Loop Detected',              // RFC 5842 7.2
  509: 'Bandwidth Limit Exceeded',
  510: 'Not Extended',               // RFC 2774 7
  511: 'Network Authentication Required', // RFC 6585 6
};

const kOnExecute = HTTPParser.kOnExecute | 0;
const kOnTimeout = HTTPParser.kOnTimeout | 0;
const kLenientAll = HTTPParser.kLenientAll | 0;
const kLenientNone = HTTPParser.kLenientNone | 0;
const kConnections = Symbol('http.server.connections');
const kConnectionsCheckingInterval = Symbol('http.server.connectionsCheckingInterval');

const HTTP_SERVER_TRACE_EVENT_NAME = 'http.server.request';
// TODO(jazelly): make this configurable
const HTTP_SERVER_KEEP_ALIVE_TIMEOUT_BUFFER = 1000;

class HTTPServerAsyncResource {
  constructor(type, socket) {
    this.type = type;
    this.socket = socket;
  }
}

function ServerResponse(req, options) {
  OutgoingMessage.call(this, options);

  if (req.method === 'HEAD') this._hasBody = false;

  this.req = req;
  this.sendDate = true;
  this._sent100 = false;
  this._expect_continue = false;

  if (req.httpVersionMajor < 1 || req.httpVersionMinor < 1) {
    this.useChunkedEncodingByDefault = chunkExpression.test(req.headers.te);
    this.shouldKeepAlive = false;
  }

  if (hasObserver('http')) {
    startPerf(this, kServerResponseStatistics, {
      type: 'http',
      name: 'HttpRequest',
      detail: {
        req: {
          method: req.method,
          url: req.url,
          headers: req.headers,
        },
      },
    });
  }
  if (isTraceHTTPEnabled()) {
    this._traceEventId = getNextTraceEventId();
    traceBegin(HTTP_SERVER_TRACE_EVENT_NAME, this._traceEventId);
  }
  if (onResponseCreatedChannel.hasSubscribers) {
    onResponseCreatedChannel.publish({
      request: req,
      response: this,
    });
  }
}
ObjectSetPrototypeOf(ServerResponse.prototype, OutgoingMessage.prototype);
ObjectSetPrototypeOf(ServerResponse, OutgoingMessage);

ServerResponse.prototype._finish = function _finish() {
  if (this[kServerResponseStatistics] && hasObserver('http')) {
    stopPerf(this, kServerResponseStatistics, {
      detail: {
        res: {
          statusCode: this.statusCode,
          statusMessage: this.statusMessage,
          headers: typeof this.getHeaders === 'function' ? this.getHeaders() : {},
        },
      },
    });
  }
  OutgoingMessage.prototype._finish.call(this);
  if (isTraceHTTPEnabled() && typeof this._traceEventId === 'number') {
    const data = {
      url: this.req?.url,
      statusCode: this.statusCode,
    };
    traceEnd(HTTP_SERVER_TRACE_EVENT_NAME, this._traceEventId, data);
  }
};


ServerResponse.prototype.statusCode = 200;
ServerResponse.prototype.statusMessage = undefined;

function onServerResponseClose() {
  // EventEmitter.emit makes a copy of the 'close' listeners array before
  // calling the listeners. detachSocket() unregisters onServerResponseClose
  // but if detachSocket() is called, directly or indirectly, by a 'close'
  // listener, onServerResponseClose is still in that copy of the listeners
  // array. That is, in the example below, b still gets called even though
  // it's been removed by a:
  //
  //   const EventEmitter = require('events');
  //   const obj = new EventEmitter();
  //   obj.on('event', a);
  //   obj.on('event', b);
  //   function a() { obj.removeListener('event', b) }
  //   function b() { throw "BAM!" }
  //   obj.emit('event');  // throws
  //
  // Ergo, we need to deal with stale 'close' events and handle the case
  // where the ServerResponse object has already been deconstructed.
  // Fortunately, that requires only a single if check. :-)
  if (this._httpMessage) {
    emitCloseNT(this._httpMessage);
  }
}

ServerResponse.prototype.assignSocket = function assignSocket(socket) {
  if (socket._httpMessage) {
    throw new ERR_HTTP_SOCKET_ASSIGNED();
  }
  socket._httpMessage = this;
  socket.on('close', onServerResponseClose);
  this.socket = socket;
  this.emit('socket', socket);
  this._flush();
};

ServerResponse.prototype.detachSocket = function detachSocket(socket) {
  assert(socket._httpMessage === this);
  socket.removeListener('close', onServerResponseClose);
  socket._httpMessage = null;
  this.socket = null;
};

ServerResponse.prototype.writeContinue = function writeContinue(cb) {
  this._writeRaw('HTTP/1.1 100 Continue\r\n\r\n', 'ascii', cb);
  this._sent100 = true;
};

ServerResponse.prototype.writeProcessing = function writeProcessing(cb) {
  this._writeRaw('HTTP/1.1 102 Processing\r\n\r\n', 'ascii', cb);
};

ServerResponse.prototype.writeEarlyHints = function writeEarlyHints(hints, cb) {
  let head = 'HTTP/1.1 103 Early Hints\r\n';

  validateObject(hints, 'hints');

  if (hints.link === null || hints.link === undefined) {
    return;
  }

  const link = validateLinkHeaderValue(hints.link);

  if (link.length === 0) {
    return;
  }

  head += 'Link: ' + link + '\r\n';

  for (const key of ObjectKeys(hints)) {
    if (key !== 'link') {
      head += key + ': ' + hints[key] + '\r\n';
    }
  }

  head += '\r\n';

  this._writeRaw(head, 'ascii', cb);
};

ServerResponse.prototype._implicitHeader = function _implicitHeader() {
  this.writeHead(this.statusCode);
};

ServerResponse.prototype.writeHead = writeHead;
function writeHead(statusCode, reason, obj) {

  if (this._header) {
    throw new ERR_HTTP_HEADERS_SENT('write');
  }

  const originalStatusCode = statusCode;

  statusCode |= 0;
  if (statusCode < 100 || statusCode > 999) {
    throw new ERR_HTTP_INVALID_STATUS_CODE(originalStatusCode);
  }


  if (typeof reason === 'string') {
    // writeHead(statusCode, reasonPhrase[, headers])
    this.statusMessage = reason;
  } else {
    // writeHead(statusCode[, headers])
    this.statusMessage ||= STATUS_CODES[statusCode] || 'unknown';
    obj ??= reason;
  }
  this.statusCode = statusCode;

  let headers;
  if (this[kOutHeaders]) {
    // Slow-case: when progressive API and header fields are passed.
    let k;
    if (ArrayIsArray(obj)) {
      if (obj.length % 2 !== 0) {
        throw new ERR_INVALID_ARG_VALUE('headers', obj);
      }

      // Headers in obj should override previous headers but still
      // allow explicit duplicates. To do so, we first remove any
      // existing conflicts, then use appendHeader.

      for (let n = 0; n < obj.length; n += 2) {
        k = obj[n + 0];
        this.removeHeader(k);
      }

      for (let n = 0; n < obj.length; n += 2) {
        k = obj[n + 0];
        if (k) this.appendHeader(k, obj[n + 1]);
      }
    } else if (obj) {
      const keys = ObjectKeys(obj);
      // Retain for(;;) loop for performance reasons
      // Refs: https://github.com/nodejs/node/pull/30958
      for (let i = 0; i < keys.length; i++) {
        k = keys[i];
        if (k) this.setHeader(k, obj[k]);
      }
    }
    // Only progressive api is used
    headers = this[kOutHeaders];
  } else {
    // Only writeHead() called
    headers = obj;
  }

  if (checkInvalidHeaderChar(this.statusMessage))
    throw new ERR_INVALID_CHAR('statusMessage');

  const statusLine = `HTTP/1.1 ${statusCode} ${this.statusMessage}\r\n`;

  if (statusCode === 204 || statusCode === 304 ||
      (statusCode >= 100 && statusCode <= 199)) {
    // RFC 2616, 10.2.5:
    // The 204 response MUST NOT include a message-body, and thus is always
    // terminated by the first empty line after the header fields.
    // RFC 2616, 10.3.5:
    // The 304 response MUST NOT contain a message-body, and thus is always
    // terminated by the first empty line after the header fields.
    // RFC 2616, 10.1 Informational 1xx:
    // This class of status code indicates a provisional response,
    // consisting only of the Status-Line and optional headers, and is
    // terminated by an empty line.
    this._hasBody = false;
  }

  // Don't keep alive connections where the client expects 100 Continue
  // but we sent a final status; they may put extra bytes on the wire.
  if (this._expect_continue && !this._sent100) {
    this.shouldKeepAlive = false;
  }

  this._storeHeader(statusLine, headers);

  return this;
}

// Docs-only deprecated: DEP0063
ServerResponse.prototype.writeHeader = ServerResponse.prototype.writeHead;

function storeHTTPOptions(options) {
  this[kIncomingMessage] = options.IncomingMessage || IncomingMessage;
  this[kServerResponse] = options.ServerResponse || ServerResponse;

  const maxHeaderSize = options.maxHeaderSize;
  if (maxHeaderSize !== undefined)
    validateInteger(maxHeaderSize, 'maxHeaderSize', 0);
  this.maxHeaderSize = maxHeaderSize;

  const insecureHTTPParser = options.insecureHTTPParser;
  if (insecureHTTPParser !== undefined)
    validateBoolean(insecureHTTPParser, 'options.insecureHTTPParser');
  this.insecureHTTPParser = insecureHTTPParser;

  const requestTimeout = options.requestTimeout;
  if (requestTimeout !== undefined) {
    validateInteger(requestTimeout, 'requestTimeout', 0);
    this.requestTimeout = requestTimeout;
  } else {
    this.requestTimeout = 300_000; // 5 minutes
  }

  const headersTimeout = options.headersTimeout;
  if (headersTimeout !== undefined) {
    validateInteger(headersTimeout, 'headersTimeout', 0);
    this.headersTimeout = headersTimeout;
  } else {
    this.headersTimeout = MathMin(60_000, this.requestTimeout); // Minimum between 60 seconds or requestTimeout
  }

  if (this.requestTimeout > 0 && this.headersTimeout > 0 && this.headersTimeout > this.requestTimeout) {
    throw new ERR_OUT_OF_RANGE('headersTimeout', '<= requestTimeout', headersTimeout);
  }

  const keepAliveTimeout = options.keepAliveTimeout;
  if (keepAliveTimeout !== undefined) {
    validateInteger(keepAliveTimeout, 'keepAliveTimeout', 0);
    this.keepAliveTimeout = keepAliveTimeout;
  } else {
    this.keepAliveTimeout = 5_000; // 5 seconds;
  }

  const connectionsCheckingInterval = options.connectionsCheckingInterval;
  if (connectionsCheckingInterval !== undefined) {
    validateInteger(connectionsCheckingInterval, 'connectionsCheckingInterval', 0);
    this.connectionsCheckingInterval = connectionsCheckingInterval;
  } else {
    this.connectionsCheckingInterval = 30_000; // 30 seconds
  }

  const requireHostHeader = options.requireHostHeader;
  if (requireHostHeader !== undefined) {
    validateBoolean(requireHostHeader, 'options.requireHostHeader');
    this.requireHostHeader = requireHostHeader;
  } else {
    this.requireHostHeader = true;
  }

  const joinDuplicateHeaders = options.joinDuplicateHeaders;
  if (joinDuplicateHeaders !== undefined) {
    validateBoolean(joinDuplicateHeaders, 'options.joinDuplicateHeaders');
  }
  this.joinDuplicateHeaders = joinDuplicateHeaders;

  const rejectNonStandardBodyWrites = options.rejectNonStandardBodyWrites;
  if (rejectNonStandardBodyWrites !== undefined) {
    validateBoolean(rejectNonStandardBodyWrites, 'options.rejectNonStandardBodyWrites');
    this.rejectNonStandardBodyWrites = rejectNonStandardBodyWrites;
  } else {
    this.rejectNonStandardBodyWrites = false;
  }
}

function setupConnectionsTracking() {
  // Start connection handling
  this[kConnections] ||= new ConnectionsList();

  if (this[kConnectionsCheckingInterval]) {
    clearInterval(this[kConnectionsCheckingInterval]);
  }
  // This checker is started without checking whether any headersTimeout or requestTimeout is non zero
  // otherwise it would not be started if such timeouts are modified after createServer.
  this[kConnectionsCheckingInterval] =
    setInterval(checkConnections.bind(this), this.connectionsCheckingInterval).unref();
}

function httpServerPreClose(server) {
  server.closeIdleConnections();
  clearInterval(server[kConnectionsCheckingInterval]);
}

function Server(options, requestListener) {
  if (!(this instanceof Server)) return new Server(options, requestListener);

  if (typeof options === 'function') {
    requestListener = options;
    options = kEmptyObject;
  } else if (options == null) {
    options = kEmptyObject;
  } else {
    validateObject(options, 'options');
  }

  storeHTTPOptions.call(this, options);
  net.Server.call(
    this,
    { allowHalfOpen: true, noDelay: options.noDelay ?? true,
      keepAlive: options.keepAlive,
      keepAliveInitialDelay: options.keepAliveInitialDelay,
      highWaterMark: options.highWaterMark });

  if (requestListener) {
    this.on('request', requestListener);
  }

  // Similar option to this. Too lazy to write my own docs.
  // http://www.squid-cache.org/Doc/config/half_closed_clients/
  // https://wiki.squid-cache.org/SquidFaq/InnerWorkings#What_is_a_half-closed_filedescriptor.3F
  this.httpAllowHalfOpen = false;

  this.on('connection', connectionListener);
  this.on('listening', setupConnectionsTracking);

  this.timeout = 0;
  this.maxHeadersCount = null;
  this.maxRequestsPerSocket = 0;

  this[kUniqueHeaders] = parseUniqueHeadersOption(options.uniqueHeaders);
}
ObjectSetPrototypeOf(Server.prototype, net.Server.prototype);
ObjectSetPrototypeOf(Server, net.Server);

Server.prototype.close = function() {
  httpServerPreClose(this);
  ReflectApply(net.Server.prototype.close, this, arguments);
  return this;
};

Server.prototype[SymbolAsyncDispose] = async function() {
  return promisify(this.close).call(this);
};

Server.prototype.closeAllConnections = function() {
  if (!this[kConnections]) {
    return;
  }

  const connections = this[kConnections].all();

  for (let i = 0, l = connections.length; i < l; i++) {
    connections[i].socket.destroy();
  }
};

Server.prototype.closeIdleConnections = function() {
  if (!this[kConnections]) {
    return;
  }

  const connections = this[kConnections].idle();

  for (let i = 0, l = connections.length; i < l; i++) {
    if (connections[i].socket._httpMessage && !connections[i].socket._httpMessage.finished) {
      continue;
    }

    connections[i].socket.destroy();
  }
};

Server.prototype.setTimeout = function setTimeout(msecs, callback) {
  this.timeout = msecs;
  if (callback)
    this.on('timeout', callback);
  return this;
};

Server.prototype[EE.captureRejectionSymbol] = function(err, event, ...args) {
  switch (event) {
    case 'request': {
      const { 1: res } = args;
      if (!res.headersSent && !res.writableEnded) {
        // Don't leak headers.
        const names = res.getHeaderNames();
        for (let i = 0; i < names.length; i++) {
          res.removeHeader(names[i]);
        }
        res.statusCode = 500;
        res.end(STATUS_CODES[500]);
      } else {
        res.destroy();
      }
      break;
    }
    default:
      net.Server.prototype[SymbolFor('nodejs.rejection')]
        .apply(this, arguments);
  }
};

function checkConnections() {
  if (this.headersTimeout === 0 && this.requestTimeout === 0) {
    return;
  }

  const expired = this[kConnections].expired(this.headersTimeout, this.requestTimeout);

  for (let i = 0; i < expired.length; i++) {
    const socket = expired[i].socket;

    if (socket) {
      onRequestTimeout(socket);
    }
  }
}

function connectionListener(socket) {
  defaultTriggerAsyncIdScope(
    getOrSetAsyncId(socket), connectionListenerInternal, this, socket,
  );
}

function connectionListenerInternal(server, socket) {
  debug('SERVER new http connection');

  // Ensure that the server property of the socket is correctly set.
  // See https://github.com/nodejs/node/issues/13435
  socket.server = server;

  // If the user has added a listener to the server,
  // request, or response, then it's their responsibility.
  // otherwise, destroy on timeout by default
  if (server.timeout && typeof socket.setTimeout === 'function')
    socket.setTimeout(server.timeout);
  socket.on('timeout', socketOnTimeout);

  const parser = parsers.alloc();

  const lenient = server.insecureHTTPParser === undefined ?
    isLenient() : server.insecureHTTPParser;

  // TODO(addaleax): This doesn't play well with the
  // `async_hooks.currentResource()` proposal, see
  // https://github.com/nodejs/node/pull/21313
  parser.initialize(
    HTTPParser.REQUEST,
    new HTTPServerAsyncResource('HTTPINCOMINGMESSAGE', socket),
    server.maxHeaderSize || 0,
    lenient ? kLenientAll : kLenientNone,
    server[kConnections],
  );
  parser.socket = socket;
  socket.parser = parser;

  // Propagate headers limit from server instance to parser
  if (typeof server.maxHeadersCount === 'number') {
    parser.maxHeaderPairs = server.maxHeadersCount << 1;
  }

  const state = {
    onData: null,
    onEnd: null,
    onClose: null,
    onDrain: null,
    outgoing: [],
    incoming: [],
    // `outgoingData` is an approximate amount of bytes queued through all
    // inactive responses. If more data than the high watermark is queued - we
    // need to pause TCP socket/HTTP parser, and wait until the data will be
    // sent to the client.
    outgoingData: 0,
    requestsCount: 0,
    keepAliveTimeoutSet: false,
  };
  state.onData = socketOnData.bind(undefined,
                                   server, socket, parser, state);
  state.onEnd = socketOnEnd.bind(undefined,
                                 server, socket, parser, state);
  state.onClose = socketOnClose.bind(undefined,
                                     socket, state);
  state.onDrain = socketOnDrain.bind(undefined,
                                     socket, state);
  socket.on('data', state.onData);
  socket.on('error', socketOnError);
  socket.on('end', state.onEnd);
  socket.on('close', state.onClose);
  socket.on('drain', state.onDrain);
  parser.onIncoming = parserOnIncoming.bind(undefined,
                                            server, socket, state);

  // We are consuming socket, so it won't get any actual data
  socket.on('resume', onSocketResume);
  socket.on('pause', onSocketPause);

  // Overrides to unconsume on `data`, `readable` listeners
  socket.on = generateSocketListenerWrapper('on');
  socket.addListener = generateSocketListenerWrapper('addListener');
  socket.prependListener = generateSocketListenerWrapper('prependListener');
  socket.setEncoding = socketSetEncoding;

  // We only consume the socket if it has never been consumed before.
  if (socket._handle?.isStreamBase &&
      !socket._handle._consumed) {
    parser._consumed = true;
    socket._handle._consumed = true;
    parser.consume(socket._handle);
  }
  parser[kOnExecute] =
    onParserExecute.bind(undefined,
                         server, socket, parser, state);

  parser[kOnTimeout] =
    onParserTimeout.bind(undefined,
                         server, socket);

  socket._paused = false;
}

function socketSetEncoding() {
  throw new ERR_HTTP_SOCKET_ENCODING();
}

function updateOutgoingData(socket, state, delta) {
  state.outgoingData += delta;
  socketOnDrain(socket, state);
}

function socketOnDrain(socket, state) {
  const needPause = state.outgoingData > socket.writableHighWaterMark;

  // If we previously paused, then start reading again.
  if (socket._paused && !needPause) {
    socket._paused = false;
    if (socket.parser)
      socket.parser.resume();
    socket.resume();
  }

  const msg = socket._httpMessage;
  if (msg && !msg.finished && msg[kNeedDrain]) {
    msg[kNeedDrain] = false;
    msg.emit('drain');
  }
}

function socketOnTimeout() {
  const req = this.parser?.incoming;
  const reqTimeout = req && !req.complete && req.emit('timeout', this);
  const res = this._httpMessage;
  const resTimeout = res && res.emit('timeout', this);
  const serverTimeout = this.server.emit('timeout', this);

  if (!reqTimeout && !resTimeout && !serverTimeout)
    this.destroy();
}

function socketOnClose(socket, state) {
  debug('server socket close');
  freeParser(socket.parser, null, socket);
  abortIncoming(state.incoming);
}

function abortIncoming(incoming) {
  while (incoming.length) {
    const req = incoming.shift();
    req.destroy(new ConnResetException('aborted'));
  }
  // Abort socket._httpMessage ?
}

function socketOnEnd(server, socket, parser, state) {
  const ret = parser.finish();

  if (ret instanceof Error) {
    debug('parse error');
    // socketOnError has additional logic and will call socket.destroy(err).
    socketOnError.call(socket, ret);
  } else if (!server.httpAllowHalfOpen) {
    socket.end();
  } else if (state.outgoing.length) {
    state.outgoing[state.outgoing.length - 1]._last = true;
  } else if (socket._httpMessage) {
    socket._httpMessage._last = true;
  } else {
    socket.end();
  }
}

function socketOnData(server, socket, parser, state, d) {
  assert(!socket._paused);
  debug('SERVER socketOnData %d', d.length);

  const ret = parser.execute(d);
  onParserExecuteCommon(server, socket, parser, state, ret, d);
}

function onRequestTimeout(socket) {
  // socketOnError has additional logic and will call socket.destroy(err).
  socketOnError.call(socket, new ERR_HTTP_REQUEST_TIMEOUT());
}

function onParserExecute(server, socket, parser, state, ret) {
  // When underlying `net.Socket` instance is consumed - no
  // `data` events are emitted, and thus `socket.setTimeout` fires the
  // callback even if the data is constantly flowing into the socket.
  // See, https://github.com/nodejs/node/commit/ec2822adaad76b126b5cccdeaa1addf2376c9aa6
  socket._unrefTimer();
  debug('SERVER socketOnParserExecute %d', ret);
  onParserExecuteCommon(server, socket, parser, state, ret, undefined);
}

function onParserTimeout(server, socket) {
  const serverTimeout = server.emit('timeout', socket);

  if (!serverTimeout)
    socket.destroy();
}

const noop = () => {};
const badRequestResponse = Buffer.from(
  `HTTP/1.1 400 ${STATUS_CODES[400]}\r\n` +
  'Connection: close\r\n\r\n', 'ascii',
);
const requestTimeoutResponse = Buffer.from(
  `HTTP/1.1 408 ${STATUS_CODES[408]}\r\n` +
  'Connection: close\r\n\r\n', 'ascii',
);
const requestHeaderFieldsTooLargeResponse = Buffer.from(
  `HTTP/1.1 431 ${STATUS_CODES[431]}\r\n` +
  'Connection: close\r\n\r\n', 'ascii',
);

const requestChunkExtensionsTooLargeResponse = Buffer.from(
  `HTTP/1.1 413 ${STATUS_CODES[413]}\r\n` +
  'Connection: close\r\n\r\n', 'ascii',
);

function socketOnError(e) {
  // Ignore further errors
  this.removeListener('error', socketOnError);

  if (this.listenerCount('error', noop) === 0) {
    this.on('error', noop);
  }

  if (!this.server.emit('clientError', e, this)) {
    // Caution must be taken to avoid corrupting the remote peer.
    // Reply an error segment if there is no in-flight `ServerResponse`,
    // or no data of the in-flight one has been written yet to this socket.
    if (this.writable &&
        (!this._httpMessage || !this._httpMessage._headerSent)) {
      let response;

      switch (e.code) {
        case 'HPE_HEADER_OVERFLOW':
          response = requestHeaderFieldsTooLargeResponse;
          break;
        case 'HPE_CHUNK_EXTENSIONS_OVERFLOW':
          response = requestChunkExtensionsTooLargeResponse;
          break;
        case 'ERR_HTTP_REQUEST_TIMEOUT':
          response = requestTimeoutResponse;
          break;
        default:
          response = badRequestResponse;
          break;
      }

      this.write(response);
    }
    this.destroy(e);
  }
}

function onParserExecuteCommon(server, socket, parser, state, ret, d) {
  resetSocketTimeout(server, socket, state);

  if (ret instanceof Error) {
    prepareError(ret, parser, d);
    debug('parse error', ret);
    socketOnError.call(socket, ret);
  } else if (parser.incoming?.upgrade) {
    // Upgrade or CONNECT
    const req = parser.incoming;
    debug('SERVER upgrade or connect', req.method);

    d ||= parser.getCurrentBuffer();

    socket.removeListener('data', state.onData);
    socket.removeListener('end', state.onEnd);
    socket.removeListener('close', state.onClose);
    socket.removeListener('drain', state.onDrain);
    socket.removeListener('error', socketOnError);
    socket.removeListener('timeout', socketOnTimeout);
    unconsume(parser, socket);
    parser.finish();
    freeParser(parser, req, socket);
    parser = null;

    const eventName = req.method === 'CONNECT' ? 'connect' : 'upgrade';
    if (eventName === 'upgrade' || server.listenerCount(eventName) > 0) {
      debug('SERVER have listener for %s', eventName);
      const bodyHead = d.slice(ret, d.length);

      socket.readableFlowing = null;

      server.emit(eventName, req, socket, bodyHead);
    } else {
      // Got CONNECT method, but have no handler.
      socket.destroy();
    }
  } else if (parser.incoming && parser.incoming.method === 'PRI') {
    debug('SERVER got PRI request');
    socket.destroy();
  }

  if (socket._paused && socket.parser) {
    // onIncoming paused the socket, we should pause the parser as well
    debug('pause parser');
    socket.parser.pause();
  }
}

function clearIncoming(req) {
  req ||= this;
  const parser = req.socket?.parser;
  // Reset the .incoming property so that the request object can be gc'ed.
  if (parser && parser.incoming === req) {
    if (req.readableEnded) {
      parser.incoming = null;
    } else {
      req.on('end', clearIncoming);
    }
  }
}

function resOnFinish(req, res, socket, state, server) {
  if (onResponseFinishChannel.hasSubscribers) {
    onResponseFinishChannel.publish({
      request: req,
      response: res,
      socket,
      server,
    });
  }

  // Usually the first incoming element should be our request.  it may
  // be that in the case abortIncoming() was called that the incoming
  // array will be empty.
  assert(state.incoming.length === 0 || state.incoming[0] === req);

  state.incoming.shift();

  // If the user never called req.read(), and didn't pipe() or
  // .resume() or .on('data'), then we call req._dump() so that the
  // bytes will be pulled off the wire.
  if (!req._consuming && !req._readableState.resumeScheduled)
    req._dump();

  res.detachSocket(socket);
  clearIncoming(req);
  process.nextTick(emitCloseNT, res);

  if (res._last) {
    if (typeof socket.destroySoon === 'function') {
      socket.destroySoon();
    } else {
      socket.end();
    }
  } else if (state.outgoing.length === 0) {
    if (server.keepAliveTimeout && typeof socket.setTimeout === 'function') {
      // Increase the internal timeout wrt the advertised value to reduce
      // the likelihood of ECONNRESET errors.
      socket.setTimeout(server.keepAliveTimeout + HTTP_SERVER_KEEP_ALIVE_TIMEOUT_BUFFER);
      state.keepAliveTimeoutSet = true;
    }
  } else {
    // Start sending the next message
    const m = state.outgoing.shift();
    if (m) {
      m.assignSocket(socket);
    }
  }
}

function emitCloseNT(self) {
  if (!self._closed) {
    self.destroyed = true;
    self._closed = true;
    self.emit('close');
  }
}

// The following callback is issued after the headers have been read on a
// new message. In this callback we setup the response object and pass it
// to the user.
function parserOnIncoming(server, socket, state, req, keepAlive) {
  resetSocketTimeout(server, socket, state);

  if (req.upgrade) {
    req.upgrade = req.method === 'CONNECT' ||
                  server.listenerCount('upgrade') > 0;
    if (req.upgrade)
      return 2;
  }

  state.incoming.push(req);

  // If the writable end isn't consuming, then stop reading
  // so that we don't become overwhelmed by a flood of
  // pipelined requests that may never be resolved.
  if (!socket._paused) {
    const ws = socket._writableState;
    if (ws.needDrain || state.outgoingData >= socket.writableHighWaterMark) {
      socket._paused = true;
      // We also need to pause the parser, but don't do that until after
      // the call to execute, because we may still be processing the last
      // chunk.
      socket.pause();
    }
  }

  const res = new server[kServerResponse](req,
                                          {
                                            highWaterMark: socket.writableHighWaterMark,
                                            rejectNonStandardBodyWrites: server.rejectNonStandardBodyWrites,
                                          });
  res._keepAliveTimeout = server.keepAliveTimeout;
  res._maxRequestsPerSocket = server.maxRequestsPerSocket;
  res._onPendingData = updateOutgoingData.bind(undefined,
                                               socket, state);

  res.shouldKeepAlive = keepAlive;
  res[kUniqueHeaders] = server[kUniqueHeaders];

  if (onRequestStartChannel.hasSubscribers) {
    onRequestStartChannel.publish({
      request: req,
      response: res,
      socket,
      server,
    });
  }

  if (socket._httpMessage) {
    // There are already pending outgoing res, append.
    state.outgoing.push(res);
  } else {
    res.assignSocket(socket);
  }

  // When we're finished writing the response, check if this is the last
  // response, if so destroy the socket.
  res.on('finish',
         resOnFinish.bind(undefined,
                          req, res, socket, state, server));

  let handled = false;


  if (req.httpVersionMajor === 1 && req.httpVersionMinor === 1) {

    // From RFC 7230 5.4 https://datatracker.ietf.org/doc/html/rfc7230#section-5.4
    // A server MUST respond with a 400 (Bad Request) status code to any
    // HTTP/1.1 request message that lacks a Host header field
    if (server.requireHostHeader && req.headers.host === undefined) {
      res.writeHead(400, ['Connection', 'close']);
      res.end();
      return 0;
    }

    const isRequestsLimitSet = (
      typeof server.maxRequestsPerSocket === 'number' &&
      server.maxRequestsPerSocket > 0
    );

    if (isRequestsLimitSet) {
      state.requestsCount++;
      res.maxRequestsOnConnectionReached = (
        server.maxRequestsPerSocket <= state.requestsCount);
    }

    if (isRequestsLimitSet &&
      (server.maxRequestsPerSocket < state.requestsCount)) {
      handled = true;
      server.emit('dropRequest', req, socket);
      res.writeHead(503);
      res.end();
    } else if (req.headers.expect !== undefined) {
      handled = true;

      if (continueExpression.test(req.headers.expect)) {
        res._expect_continue = true;
        if (server.listenerCount('checkContinue') > 0) {
          server.emit('checkContinue', req, res);
        } else {
          res.writeContinue();
          server.emit('request', req, res);
        }
      } else if (server.listenerCount('checkExpectation') > 0) {
        server.emit('checkExpectation', req, res);
      } else {
        res.writeHead(417);
        res.end();
      }
    }
  }

  if (!handled) {
    server.emit('request', req, res);
  }

  return 0;  // No special treatment.
}

function resetSocketTimeout(server, socket, state) {
  if (!state.keepAliveTimeoutSet)
    return;

  socket.setTimeout(server.timeout || 0);
  state.keepAliveTimeoutSet = false;
}

function onSocketResume() {
  // It may seem that the socket is resumed, but this is an enemy's trick to
  // deceive us! `resume` is emitted asynchronously, and may be called from
  // `incoming.readStart()`. Stop the socket again here, just to preserve the
  // state.
  //
  // We don't care about stream semantics for the consumed socket anyway.
  if (this._paused) {
    this.pause();
    return;
  }

  if (this._handle && !this._handle.reading) {
    this._handle.reading = true;
    this._handle.readStart();
  }
}

function onSocketPause() {
  if (this._handle?.reading) {
    this._handle.reading = false;
    this._handle.readStop();
  }
}

function unconsume(parser, socket) {
  if (socket._handle) {
    if (parser._consumed)
      parser.unconsume();
    parser._consumed = false;
    socket.removeListener('pause', onSocketPause);
    socket.removeListener('resume', onSocketResume);
  }
}

function generateSocketListenerWrapper(originalFnName) {
  return function socketListenerWrap(ev, fn) {
    const res = net.Socket.prototype[originalFnName].call(this,
                                                          ev, fn);
    if (!this.parser) {
      this.on = net.Socket.prototype.on;
      this.addListener = net.Socket.prototype.addListener;
      this.prependListener = net.Socket.prototype.prependListener;
      return res;
    }

    if (ev === 'data' || ev === 'readable')
      unconsume(this.parser, this);

    return res;
  };
}

module.exports = {
  STATUS_CODES,
  Server,
  ServerResponse,
  setupConnectionsTracking,
  storeHTTPOptions,
  _connectionListener: connectionListener,
  kServerResponse,
  httpServerPreClose,
  kConnectionsCheckingInterval,
};
