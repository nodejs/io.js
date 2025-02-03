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
  ObjectDefineProperty,
} = primordials;

const { validateInteger } = require('internal/validators');
const httpAgent = require('_http_agent');
const { ClientRequest } = require('_http_client');
const { methods, parsers } = require('_http_common');
const { IncomingMessage } = require('_http_incoming');
const {
  validateHeaderName,
  validateHeaderValue,
  OutgoingMessage,
} = require('_http_outgoing');
const {
  _connectionListener,
  STATUS_CODES,
  Server,
  ServerResponse,
} = require('_http_server');
let maxHeaderSize;
let undici;

/**
 * Returns a new instance of `http.Server`.
 * @param {{
 *   IncomingMessage?: IncomingMessage;
 *   ServerResponse?: ServerResponse;
 *   insecureHTTPParser?: boolean;
 *   maxHeaderSize?: number;
 *   requireHostHeader?: boolean;
 *   joinDuplicateHeaders?: boolean;
 *   highWaterMark?: number;
 *   rejectNonStandardBodyWrites?: boolean;
 *   }} [opts]
 * @param {Function} [requestListener]
 * @returns {Server}
 */
function createServer(opts, requestListener) {
  return new Server(opts, requestListener);
}

/**
 * @typedef {object} HTTPRequestOptions
 * @property {httpAgent.Agent | boolean} [agent] Controls Agent behavior.
 * @property {string} [auth] Basic authentication ('user:password') to compute an Authorization header.
 * @property {Function} [createConnection] Produces a socket/stream to use when the agent option is not used.
 * @property {number} [defaultPort] Default port for the protocol.
 * @property {number} [family] IP address family to use when resolving host or hostname.
 * @property {object} [headers] An object containing request headers.
 * @property {number} [hints] Optional dns.lookup() hints.
 * @property {string} [host] A domain name or IP address of the server to issue the request to.
 * @property {string} [hostname] Alias for host.
 * @property {boolean} [insecureHTTPParser] Use an insecure HTTP parser that accepts invalid HTTP headers when true.
 * @property {boolean} [joinDuplicateHeaders] Multiple header that joined with `,` field line values.
 * @property {string} [localAddress] Local interface to bind for network connections.
 * @property {number} [localPort] Local port to connect from.
 * @property {Function} [lookup] Custom lookup function. Default: dns.lookup().
 * @property {number} [maxHeaderSize] Overrides the --max-http-header-size value for responses received from the server.
 * @property {string} [method] A string specifying the HTTP request method.
 * @property {string} [path] Request path.
 * @property {number} [port] Port of remote server.
 * @property {string} [protocol] Protocol to use.
 * @property {boolean} [setHost] Specifies whether or not to automatically add the Host header.
 * @property {AbortSignal} [signal] An AbortSignal that may be used to abort an ongoing request.
 * @property {string} [socketPath] Unix domain socket.
 * @property {number} [timeout] A number specifying the socket timeout in milliseconds.
 * @property {Array} [uniqueHeaders] A list of request headers that should be sent only once.
 */

/**
 * Makes an HTTP request.
 * @param {string | URL} url
 * @param {HTTPRequestOptions} [options]
 * @param {Function} [cb]
 * @returns {ClientRequest}
 */
function request(url, options, cb) {
  return new ClientRequest(url, options, cb);
}

/**
 * Makes a `GET` HTTP request.
 * @param {string | URL} url
 * @param {HTTPRequestOptions} [options]
 * @param {Function} [cb]
 * @returns {ClientRequest}
 */
function get(url, options, cb) {
  const req = request(url, options, cb);
  req.end();
  return req;
}

/**
 * Lazy loads WebSocket, CloseEvent and MessageEvent classes from undici
 * @returns {object} An object containing WebSocket, CloseEvent, and MessageEvent classes.
 */
function lazyUndici() {
  return undici ??= require('internal/deps/undici/undici');
}

module.exports = {
  _connectionListener,
  METHODS: methods.toSorted(),
  STATUS_CODES,
  Agent: httpAgent.Agent,
  ClientRequest,
  IncomingMessage,
  OutgoingMessage,
  Server,
  ServerResponse,
  createServer,
  validateHeaderName,
  validateHeaderValue,
  get,
  request,
  setMaxIdleHTTPParsers(max) {
    validateInteger(max, 'max', 1);
    parsers.max = max;
  },
};

ObjectDefineProperty(module.exports, 'maxHeaderSize', {
  __proto__: null,
  configurable: true,
  enumerable: true,
  get() {
    if (maxHeaderSize === undefined) {
      const { getOptionValue } = require('internal/options');
      maxHeaderSize = getOptionValue('--max-http-header-size');
    }

    return maxHeaderSize;
  },
});

ObjectDefineProperty(module.exports, 'globalAgent', {
  __proto__: null,
  configurable: true,
  enumerable: true,
  get() {
    return httpAgent.globalAgent;
  },
  set(value) {
    httpAgent.globalAgent = value;
  },
});

ObjectDefineProperty(module.exports, 'WebSocket', {
  __proto__: null,
  configurable: true,
  enumerable: true,
  get() {
    return lazyUndici().WebSocket;
  },
});

ObjectDefineProperty(module.exports, 'CloseEvent', {
  __proto__: null,
  configurable: true,
  enumerable: true,
  get() {
    return lazyUndici().CloseEvent;
  },
});

ObjectDefineProperty(module.exports, 'MessageEvent', {
  __proto__: null,
  configurable: true,
  enumerable: true,
  get() {
    return lazyUndici().MessageEvent;
  },
});
