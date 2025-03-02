'use strict';

const {
  JSONParse,
  JSONStringify,
  SafeMap,
  SymbolDispose,
} = primordials;

const {
  ERR_INSPECTOR_ALREADY_ACTIVATED,
  ERR_INSPECTOR_ALREADY_CONNECTED,
  ERR_INSPECTOR_CLOSED,
  ERR_INSPECTOR_COMMAND,
  ERR_INSPECTOR_NOT_AVAILABLE,
  ERR_INSPECTOR_NOT_CONNECTED,
  ERR_INSPECTOR_NOT_ACTIVE,
  ERR_INSPECTOR_NOT_WORKER,
} = require('internal/errors').codes;

const { isLoopback } = require('internal/net');

const { hasInspector } = internalBinding('config');
if (!hasInspector)
  throw new ERR_INSPECTOR_NOT_AVAILABLE();

const EventEmitter = require('events');
const { queueMicrotask } = require('internal/process/task_queues');
const { kEmptyObject } = require('internal/util');
const {
  isUint32,
  validateFunction,
  validateInt32,
  validateObject,
  validateString,
} = require('internal/validators');
const { isMainThread } = require('worker_threads');
const { _debugEnd } = internalBinding('process_methods');

const {
  Connection,
  MainThreadConnection,
  open,
  url,
  isEnabled,
  waitForDebugger,
  console,
  emitProtocolEvent,
} = internalBinding('inspector');

class Session extends EventEmitter {
  #connection = null;
  #nextId = 1;
  #messageCallbacks = new SafeMap();

  /**
   * Connects the session to the inspector back-end.
   * @returns {void}
   */
  connect() {
    if (this.#connection)
      throw new ERR_INSPECTOR_ALREADY_CONNECTED('The inspector session');
    this.#connection = new Connection((message) => this.#onMessage(message));
  }

  /**
   * Connects the session to the main thread
   * inspector back-end.
   * @returns {void}
   */
  connectToMainThread() {
    if (isMainThread)
      throw new ERR_INSPECTOR_NOT_WORKER();
    if (this.#connection)
      throw new ERR_INSPECTOR_ALREADY_CONNECTED('The inspector session');
    this.#connection =
      new MainThreadConnection(
        (message) => queueMicrotask(() => this.#onMessage(message)));
  }

  #onMessage(message) {
    const parsed = JSONParse(message);
    try {
      if (parsed.id) {
        const callback = this.#messageCallbacks.get(parsed.id);
        this.#messageCallbacks.delete(parsed.id);
        if (callback) {
          if (parsed.error) {
            return callback(
              new ERR_INSPECTOR_COMMAND(parsed.error.code, parsed.error.message),
            );
          }

          callback(null, parsed.result);
        }
      } else {
        this.emit(parsed.method, parsed);
        this.emit('inspectorNotification', parsed);
      }
    } catch (error) {
      process.emitWarning(error);
    }
  }

  /**
   * Posts a message to the inspector back-end.
   * @param {string} method
   * @param {Record<unknown, unknown>} [params]
   * @param {Function} [callback]
   * @returns {void}
   */
  post(method, params, callback) {
    validateString(method, 'method');
    if (!callback && typeof params === 'function') {
      callback = params;
      params = null;
    }
    if (params) {
      validateObject(params, 'params');
    }
    if (callback) {
      validateFunction(callback, 'callback');
    }

    if (!this.#connection) {
      throw new ERR_INSPECTOR_NOT_CONNECTED();
    }
    const id = this.#nextId++;
    const message = { id, method };
    if (params) {
      message.params = params;
    }
    if (callback) {
      this.#messageCallbacks.set(id, callback);
    }
    this.#connection.dispatch(JSONStringify(message));
  }

  /**
   * Immediately closes the session, all pending
   * message callbacks will be called with an
   * error.
   * @returns {void}
   */
  disconnect() {
    if (!this.#connection)
      return;
    this.#connection.disconnect();
    this.#connection = null;
    const remainingCallbacks = this.#messageCallbacks.values();
    for (const callback of remainingCallbacks) {
      process.nextTick(callback, new ERR_INSPECTOR_CLOSED());
    }
    this.#messageCallbacks.clear();
    this.#nextId = 1;
  }
}

/**
 * Activates inspector on host and port.
 * @param {number} [port]
 * @param {string} [host]
 * @param {boolean} [wait]
 * @returns {void}
 */
function inspectorOpen(port, host, wait) {
  if (isEnabled()) {
    throw new ERR_INSPECTOR_ALREADY_ACTIVATED();
  }
  // inspectorOpen() currently does not typecheck its arguments and adding
  // such checks would be a potentially breaking change. However, the native
  // open() function requires the port to fit into a 16-bit unsigned integer,
  // causing an integer overflow otherwise, so we at least need to prevent that.
  if (isUint32(port)) {
    validateInt32(port, 'port', 0, 65535);
  }
  if (host && !isLoopback(host)) {
    process.emitWarning(
      'Binding the inspector to a public IP with an open port is insecure, ' +
        'as it allows external hosts to connect to the inspector ' +
        'and perform a remote code execution attack. ' +
        'Documentation can be found at ' +
      'https://nodejs.org/api/cli.html#--inspecthostport',
      'SecurityWarning',
    );
  }

  open(port, host);
  if (wait)
    waitForDebugger();

  return { __proto__: null, [SymbolDispose]() { _debugEnd(); } };
}

/**
 * Blocks until a client (existing or connected later)
 * has sent the `Runtime.runIfWaitingForDebugger`
 * command.
 * @returns {void}
 */
function inspectorWaitForDebugger() {
  if (!waitForDebugger())
    throw new ERR_INSPECTOR_NOT_ACTIVE();
}

function broadcastToFrontend(eventName, params = kEmptyObject) {
  validateString(eventName, 'eventName');
  validateObject(params, 'params');
  emitProtocolEvent(eventName, params);
}

const Network = {
  requestWillBeSent: (params) => broadcastToFrontend('Network.requestWillBeSent', params),
  responseReceived: (params) => broadcastToFrontend('Network.responseReceived', params),
  loadingFinished: (params) => broadcastToFrontend('Network.loadingFinished', params),
  loadingFailed: (params) => broadcastToFrontend('Network.loadingFailed', params),
};

module.exports = {
  open: inspectorOpen,
  close: _debugEnd,
  url,
  waitForDebugger: inspectorWaitForDebugger,
  console,
  Session,
  Network,
};
