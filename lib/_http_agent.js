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
  NumberParseInt,
  ObjectKeys,
  ObjectSetPrototypeOf,
  ObjectValues,
  Symbol,
} = primordials;

const net = require('net');
const EventEmitter = require('events');
let debug = require('internal/util/debuglog').debuglog('http', (fn) => {
  debug = fn;
});
const { AsyncResource } = require('async_hooks');
const { async_id_symbol } = require('internal/async_hooks').symbols;
const {
  kEmptyObject,
  once,
} = require('internal/util');
const {
  validateNumber,
  validateOneOf,
  validateString,
} = require('internal/validators');

const kOnKeylog = Symbol('onkeylog');
const kRequestOptions = Symbol('requestOptions');
const kRequestAsyncResource = Symbol('requestAsyncResource');

// TODO(jazelly): make this configurable
const HTTP_AGENT_KEEP_ALIVE_TIMEOUT_BUFFER = 1000;
// New Agent code.

// The largest departure from the previous implementation is that
// an Agent instance holds connections for a variable number of host:ports.
// Surprisingly, this is still API compatible as far as third parties are
// concerned. The only code that really notices the difference is the
// request object.

// Another departure is that all code related to HTTP parsing is in
// ClientRequest.onSocket(). The Agent is now *strictly*
// concerned with managing a connection pool.

class ReusedHandle {
  constructor(type, handle) {
    this.type = type;
    this.handle = handle;
  }
}

function freeSocketErrorListener(err) {
  const socket = this;
  debug('SOCKET ERROR on FREE socket:', err.message, err.stack);
  socket.destroy();
  socket.emit('agentRemove');
}

function Agent(options) {
  if (!(this instanceof Agent))
    return new Agent(options);

  EventEmitter.call(this);

  this.defaultPort = 80;
  this.protocol = 'http:';

  this.options = { __proto__: null, ...options };

  if (this.options.noDelay === undefined)
    this.options.noDelay = true;

  // Don't confuse net and make it think that we're connecting to a pipe
  this.options.path = null;
  this.requests = { __proto__: null };
  this.sockets = { __proto__: null };
  this.freeSockets = { __proto__: null };
  this.keepAliveMsecs = this.options.keepAliveMsecs || 1000;
  this.keepAlive = this.options.keepAlive || false;
  this.maxSockets = this.options.maxSockets || Agent.defaultMaxSockets;
  this.maxFreeSockets = this.options.maxFreeSockets || 256;
  this.scheduling = this.options.scheduling || 'lifo';
  this.maxTotalSockets = this.options.maxTotalSockets;
  this.totalSocketCount = 0;

  validateOneOf(this.scheduling, 'scheduling', ['fifo', 'lifo']);

  if (this.maxTotalSockets !== undefined) {
    validateNumber(this.maxTotalSockets, 'maxTotalSockets', 1);
  } else {
    this.maxTotalSockets = Infinity;
  }

  this.on('free', (socket, options) => {
    const name = this.getName(options);
    debug('agent.on(free)', name);

    // TODO(ronag): socket.destroy(err) might have been called
    // before coming here and have an 'error' scheduled. In the
    // case of socket.destroy() below this 'error' has no handler
    // and could cause unhandled exception.

    if (!socket.writable) {
      socket.destroy();
      return;
    }

    const requests = this.requests[name];
    if (requests?.length) {
      const req = requests.shift();
      const reqAsyncRes = req[kRequestAsyncResource];
      if (reqAsyncRes) {
        // Run request within the original async context.
        reqAsyncRes.runInAsyncScope(() => {
          asyncResetHandle(socket);
          setRequestSocket(this, req, socket);
        });
        req[kRequestAsyncResource] = null;
      } else {
        setRequestSocket(this, req, socket);
      }
      if (requests.length === 0) {
        delete this.requests[name];
      }
      return;
    }

    // If there are no pending requests, then put it in
    // the freeSockets pool, but only if we're allowed to do so.
    const req = socket._httpMessage;
    if (!req || !req.shouldKeepAlive || !this.keepAlive) {
      socket.destroy();
      return;
    }

    const freeSockets = this.freeSockets[name] || [];
    const freeLen = freeSockets.length;
    let count = freeLen;
    if (this.sockets[name])
      count += this.sockets[name].length;

    if (this.totalSocketCount > this.maxTotalSockets ||
        count > this.maxSockets ||
        freeLen >= this.maxFreeSockets ||
        !this.keepSocketAlive(socket)) {
      socket.destroy();
      return;
    }

    this.freeSockets[name] = freeSockets;
    socket[async_id_symbol] = -1;
    socket._httpMessage = null;
    this.removeSocket(socket, options);

    socket.once('error', freeSocketErrorListener);
    freeSockets.push(socket);
  });

  // Don't emit keylog events unless there is a listener for them.
  this.on('newListener', maybeEnableKeylog);
}
ObjectSetPrototypeOf(Agent.prototype, EventEmitter.prototype);
ObjectSetPrototypeOf(Agent, EventEmitter);

function maybeEnableKeylog(eventName) {
  if (eventName === 'keylog') {
    this.removeListener('newListener', maybeEnableKeylog);
    // Future sockets will listen on keylog at creation.
    const agent = this;
    this[kOnKeylog] = function onkeylog(keylog) {
      agent.emit('keylog', keylog, this);
    };
    // Existing sockets will start listening on keylog now.
    const sockets = ObjectValues(this.sockets);
    for (let i = 0; i < sockets.length; i++) {
      sockets[i].on('keylog', this[kOnKeylog]);
    }
  }
}

Agent.defaultMaxSockets = Infinity;

Agent.prototype.createConnection = net.createConnection;

// Get the key for a given set of request options
Agent.prototype.getName = function getName(options = kEmptyObject) {
  let name = options.host || 'localhost';

  name += ':';
  if (options.port)
    name += options.port;

  name += ':';
  if (options.localAddress)
    name += options.localAddress;

  // Pacify parallel/test-http-agent-getname by only appending
  // the ':' when options.family is set.
  if (options.family === 4 || options.family === 6)
    name += `:${options.family}`;

  if (options.socketPath)
    name += `:${options.socketPath}`;

  return name;
};

Agent.prototype.addRequest = function addRequest(req, options, port/* legacy */,
                                                 localAddress/* legacy */) {
  // Legacy API: addRequest(req, host, port, localAddress)
  if (typeof options === 'string') {
    options = {
      __proto__: null,
      host: options,
      port,
      localAddress,
    };
  }

  options = { __proto__: null, ...options, ...this.options };
  if (options.socketPath)
    options.path = options.socketPath;

  normalizeServerName(options, req);

  const name = this.getName(options);
  this.sockets[name] ||= [];

  const freeSockets = this.freeSockets[name];
  let socket;
  if (freeSockets) {
    while (freeSockets.length && freeSockets[0].destroyed) {
      freeSockets.shift();
    }
    socket = this.scheduling === 'fifo' ?
      freeSockets.shift() :
      freeSockets.pop();
    if (!freeSockets.length)
      delete this.freeSockets[name];
  }

  const freeLen = freeSockets ? freeSockets.length : 0;
  const sockLen = freeLen + this.sockets[name].length;

  if (socket) {
    asyncResetHandle(socket);
    this.reuseSocket(socket, req);
    setRequestSocket(this, req, socket);
    this.sockets[name].push(socket);
  } else if (sockLen < this.maxSockets &&
             this.totalSocketCount < this.maxTotalSockets) {
    debug('call onSocket', sockLen, freeLen);
    // If we are under maxSockets create a new one.
    this.createSocket(req, options, (err, socket) => {
      if (err)
        req.onSocket(socket, err);
      else
        setRequestSocket(this, req, socket);
    });
  } else {
    debug('wait for socket');
    // We are over limit so we'll add it to the queue.
    this.requests[name] ||= [];

    // Used to create sockets for pending requests from different origin
    req[kRequestOptions] = options;
    // Used to capture the original async context.
    req[kRequestAsyncResource] = new AsyncResource('QueuedRequest');

    this.requests[name].push(req);
  }
};

Agent.prototype.createSocket = function createSocket(req, options, cb) {
  options = { __proto__: null, ...options, ...this.options };
  if (options.socketPath)
    options.path = options.socketPath;

  normalizeServerName(options, req);

  const name = this.getName(options);
  options._agentKey = name;

  debug('createConnection', name, options);
  options.encoding = null;

  const oncreate = once((err, s) => {
    if (err)
      return cb(err);
    this.sockets[name] ||= [];
    this.sockets[name].push(s);
    this.totalSocketCount++;
    debug('sockets', name, this.sockets[name].length, this.totalSocketCount);
    installListeners(this, s, options);
    cb(null, s);
  });
  // When keepAlive is true, pass the related options to createConnection
  if (this.keepAlive) {
    options.keepAlive = this.keepAlive;
    options.keepAliveInitialDelay = this.keepAliveMsecs;
  }
  const newSocket = this.createConnection(options, oncreate);
  if (newSocket)
    oncreate(null, newSocket);
};

function normalizeServerName(options, req) {
  if (!options.servername && options.servername !== '')
    options.servername = calculateServerName(options, req);
}

function calculateServerName(options, req) {
  let servername = options.host;
  const hostHeader = req.getHeader('host');
  if (hostHeader) {
    validateString(hostHeader, 'options.headers.host');

    // abc => abc
    // abc:123 => abc
    // [::1] => ::1
    // [::1]:123 => ::1
    if (hostHeader[0] === '[') {
      const index = hostHeader.indexOf(']');
      if (index === -1) {
        // Leading '[', but no ']'. Need to do something...
        servername = hostHeader;
      } else {
        servername = hostHeader.substring(1, index);
      }
    } else {
      servername = hostHeader.split(':', 1)[0];
    }
  }
  // Don't implicitly set invalid (IP) servernames.
  if (net.isIP(servername))
    servername = '';
  return servername;
}

function installListeners(agent, s, options) {
  function onFree() {
    debug('CLIENT socket onFree');
    agent.emit('free', s, options);
  }
  s.on('free', onFree);

  function onClose(err) {
    debug('CLIENT socket onClose');
    // This is the only place where sockets get removed from the Agent.
    // If you want to remove a socket from the pool, just close it.
    // All socket errors end in a close event anyway.
    agent.totalSocketCount--;
    agent.removeSocket(s, options);
  }
  s.on('close', onClose);

  function onTimeout() {
    debug('CLIENT socket onTimeout');

    // Destroy if in free list.
    // TODO(ronag): Always destroy, even if not in free list.
    const sockets = agent.freeSockets;
    if (ObjectKeys(sockets).some((name) => sockets[name].includes(s))) {
      return s.destroy();
    }
  }
  s.on('timeout', onTimeout);

  function onRemove() {
    // We need this function for cases like HTTP 'upgrade'
    // (defined by WebSockets) where we need to remove a socket from the
    // pool because it'll be locked up indefinitely
    debug('CLIENT socket onRemove');
    agent.totalSocketCount--;
    agent.removeSocket(s, options);
    s.removeListener('close', onClose);
    s.removeListener('free', onFree);
    s.removeListener('timeout', onTimeout);
    s.removeListener('agentRemove', onRemove);
  }
  s.on('agentRemove', onRemove);

  if (agent[kOnKeylog]) {
    s.on('keylog', agent[kOnKeylog]);
  }
}

Agent.prototype.removeSocket = function removeSocket(s, options) {
  const name = this.getName(options);
  debug('removeSocket', name, 'writable:', s.writable);
  const sets = [this.sockets];

  // If the socket was destroyed, remove it from the free buffers too.
  if (!s.writable)
    sets.push(this.freeSockets);

  for (let sk = 0; sk < sets.length; sk++) {
    const sockets = sets[sk];

    if (sockets[name]) {
      const index = sockets[name].indexOf(s);
      if (index !== -1) {
        sockets[name].splice(index, 1);
        // Don't leak
        if (sockets[name].length === 0)
          delete sockets[name];
      }
    }
  }

  let req;
  if (this.requests[name]?.length) {
    debug('removeSocket, have a request, make a socket');
    req = this.requests[name][0];
  } else {
    // TODO(rickyes): this logic will not be FIFO across origins.
    // There might be older requests in a different origin, but
    // if the origin which releases the socket has pending requests
    // that will be prioritized.
    const keys = ObjectKeys(this.requests);
    for (let i = 0; i < keys.length; i++) {
      const prop = keys[i];
      // Check whether this specific origin is already at maxSockets
      if (this.sockets[prop]?.length) break;
      debug('removeSocket, have a request with different origin,' +
        ' make a socket');
      req = this.requests[prop][0];
      options = req[kRequestOptions];
      break;
    }
  }

  if (req && options) {
    req[kRequestOptions] = undefined;
    // If we have pending requests and a socket gets closed make a new one
    this.createSocket(req, options, (err, socket) => {
      if (err)
        req.onSocket(socket, err);
      else
        socket.emit('free');
    });
  }

};

Agent.prototype.keepSocketAlive = function keepSocketAlive(socket) {
  socket.setKeepAlive(true, this.keepAliveMsecs);
  socket.unref();

  let agentTimeout = this.options.timeout || 0;
  let canKeepSocketAlive = true;

  if (socket._httpMessage?.res) {
    const keepAliveHint = socket._httpMessage.res.headers['keep-alive'];

    if (keepAliveHint) {
      const hint = /^timeout=(\d+)/.exec(keepAliveHint)?.[1];

      if (hint) {
        // Let the timer expire before the announced timeout to reduce
        // the likelihood of ECONNRESET errors
        let serverHintTimeout = (NumberParseInt(hint) * 1000) - HTTP_AGENT_KEEP_ALIVE_TIMEOUT_BUFFER;
        serverHintTimeout = serverHintTimeout > 0 ? serverHintTimeout : 0;
        if (serverHintTimeout === 0) {
          // Cannot safely reuse the socket because the server timeout is
          // too short
          canKeepSocketAlive = false;
        } else if (serverHintTimeout < agentTimeout) {
          agentTimeout = serverHintTimeout;
        }
      }
    }
  }

  if (socket.timeout !== agentTimeout) {
    socket.setTimeout(agentTimeout);
  }

  return canKeepSocketAlive;
};

Agent.prototype.reuseSocket = function reuseSocket(socket, req) {
  debug('have free socket');
  socket.removeListener('error', freeSocketErrorListener);
  req.reusedSocket = true;
  socket.ref();
};

Agent.prototype.destroy = function destroy() {
  const sets = [this.freeSockets, this.sockets];
  for (let s = 0; s < sets.length; s++) {
    const set = sets[s];
    const keys = ObjectKeys(set);
    for (let v = 0; v < keys.length; v++) {
      const setName = set[keys[v]];
      for (let n = 0; n < setName.length; n++) {
        setName[n].destroy();
      }
    }
  }
};

function setRequestSocket(agent, req, socket) {
  req.onSocket(socket);
  const agentTimeout = agent.options.timeout || 0;
  if (req.timeout === undefined || req.timeout === agentTimeout) {
    return;
  }
  socket.setTimeout(req.timeout);
}

function asyncResetHandle(socket) {
  // Guard against an uninitialized or user supplied Socket.
  const handle = socket._handle;
  if (handle && typeof handle.asyncReset === 'function') {
    // Assign the handle a new asyncId and run any destroy()/init() hooks.
    handle.asyncReset(new ReusedHandle(handle.getProviderType(), handle));
    socket[async_id_symbol] = handle.getAsyncId();
  }
}

module.exports = {
  Agent,
  globalAgent: new Agent({ keepAlive: true, scheduling: 'lifo', timeout: 5000 }),
};
