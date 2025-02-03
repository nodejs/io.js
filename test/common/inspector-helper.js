'use strict';
const common = require('../common');
const assert = require('assert');
const fs = require('fs');
const http = require('http');
const fixtures = require('../common/fixtures');
const { spawn } = require('child_process');
const { URL, pathToFileURL } = require('url');
const { EventEmitter, once } = require('events');

const _MAINSCRIPT = fixtures.path('loop.js');
const DEBUG = false;
const TIMEOUT = common.platformTimeout(15 * 1000);

function spawnChildProcess(inspectorFlags, scriptContents, scriptFile) {
  const args = [].concat(inspectorFlags);
  if (scriptContents) {
    args.push('-e', scriptContents);
  } else {
    args.push(scriptFile);
  }
  const child = spawn(process.execPath, args);

  const handler = tearDown.bind(null, child);
  process.on('exit', handler);
  process.on('uncaughtException', handler);
  process.on('unhandledRejection', handler);
  process.on('SIGINT', handler);

  return child;
}

function makeBufferingDataCallback(dataCallback) {
  let buffer = Buffer.alloc(0);
  return (data) => {
    const newData = Buffer.concat([buffer, data]);
    const str = newData.toString('utf8');
    const lines = str.replace(/\r/g, '').split('\n');
    if (str.endsWith('\n'))
      buffer = Buffer.alloc(0);
    else
      buffer = Buffer.from(lines.pop(), 'utf8');
    for (const line of lines)
      dataCallback(line);
  };
}

function tearDown(child, err) {
  child.kill();
  if (err) {
    console.error(err);
    process.exit(1);
  }
}

function parseWSFrame(buffer) {
  // Protocol described in https://tools.ietf.org/html/rfc6455#section-5
  let message = null;
  if (buffer.length < 2)
    return { length: 0, message };
  if (buffer[0] === 0x88 && buffer[1] === 0x00) {
    return { length: 2, message, closed: true };
  }
  assert.strictEqual(buffer[0], 0x81);
  let dataLen = 0x7F & buffer[1];
  let bodyOffset = 2;
  if (buffer.length < bodyOffset + dataLen)
    return 0;
  if (dataLen === 126) {
    dataLen = buffer.readUInt16BE(2);
    bodyOffset = 4;
  } else if (dataLen === 127) {
    assert(buffer[2] === 0 && buffer[3] === 0, 'Inspector message too big');
    dataLen = buffer.readUIntBE(4, 6);
    bodyOffset = 10;
  }
  if (buffer.length < bodyOffset + dataLen)
    return { length: 0, message };
  const jsonPayload =
    buffer.slice(bodyOffset, bodyOffset + dataLen).toString('utf8');
  try {
    message = JSON.parse(jsonPayload);
  } catch (e) {
    console.error(`JSON.parse() failed for: ${jsonPayload}`);
    throw e;
  }
  if (DEBUG)
    console.log('[received]', JSON.stringify(message));
  return { length: bodyOffset + dataLen, message };
}

function formatWSFrame(message) {
  const messageBuf = Buffer.from(JSON.stringify(message));

  const wsHeaderBuf = Buffer.allocUnsafe(16);
  wsHeaderBuf.writeUInt8(0x81, 0);
  let byte2 = 0x80;
  const bodyLen = messageBuf.length;

  let maskOffset = 2;
  if (bodyLen < 126) {
    byte2 = 0x80 + bodyLen;
  } else if (bodyLen < 65536) {
    byte2 = 0xFE;
    wsHeaderBuf.writeUInt16BE(bodyLen, 2);
    maskOffset = 4;
  } else {
    byte2 = 0xFF;
    wsHeaderBuf.writeUInt32BE(bodyLen, 2);
    wsHeaderBuf.writeUInt32BE(0, 6);
    maskOffset = 10;
  }
  wsHeaderBuf.writeUInt8(byte2, 1);
  wsHeaderBuf.writeUInt32BE(0x01020408, maskOffset);

  for (let i = 0; i < messageBuf.length; i++)
    messageBuf[i] = messageBuf[i] ^ (1 << (i % 4));

  return Buffer.concat([wsHeaderBuf.slice(0, maskOffset + 4), messageBuf]);
}

class InspectorSession {
  constructor(socket, instance) {
    this._instance = instance;
    this._socket = socket;
    this._nextId = 1;
    this._commandResponsePromises = new Map();
    this._unprocessedNotifications = [];
    this._notificationCallback = null;
    this._scriptsIdsByUrl = new Map();
    this._pausedDetails = null;

    let buffer = Buffer.alloc(0);
    socket.on('data', (data) => {
      buffer = Buffer.concat([buffer, data]);
      do {
        const { length, message, closed } = parseWSFrame(buffer);
        if (!length)
          break;

        if (closed) {
          socket.write(Buffer.from([0x88, 0x00]));  // WS close frame
        }
        buffer = buffer.slice(length);
        if (message)
          this._onMessage(message);
      } while (true);
    });
    this._terminationPromise = new Promise((resolve) => {
      socket.once('close', resolve);
    });
  }


  waitForServerDisconnect() {
    return this._terminationPromise;
  }

  async disconnect() {
    this._socket.destroy();
    return this.waitForServerDisconnect();
  }

  _onMessage(message) {
    if (message.id) {
      const { resolve, reject } = this._commandResponsePromises.get(message.id);
      this._commandResponsePromises.delete(message.id);
      if (message.result)
        resolve(message.result);
      else
        reject(message.error);
    } else {
      if (message.method === 'Debugger.scriptParsed') {
        const { scriptId, url } = message.params;
        this._scriptsIdsByUrl.set(scriptId, url);
        const fileUrl = url.startsWith('file:') ?
          url : pathToFileURL(url).toString();
        if (fileUrl === this.scriptURL().toString()) {
          this.mainScriptId = scriptId;
        }
      }
      if (message.method === 'Debugger.paused')
        this._pausedDetails = message.params;
      if (message.method === 'Debugger.resumed')
        this._pausedDetails = null;

      if (this._notificationCallback) {
        // In case callback needs to install another
        const callback = this._notificationCallback;
        this._notificationCallback = null;
        callback(message);
      } else {
        this._unprocessedNotifications.push(message);
      }
    }
  }

  unprocessedNotifications() {
    return this._unprocessedNotifications;
  }

  _sendMessage(message) {
    const msg = JSON.parse(JSON.stringify(message)); // Clone!
    msg.id = this._nextId++;
    if (DEBUG)
      console.log('[sent]', JSON.stringify(msg));

    const responsePromise = new Promise((resolve, reject) => {
      this._commandResponsePromises.set(msg.id, { resolve, reject });
    });

    return new Promise(
      (resolve) => this._socket.write(formatWSFrame(msg), resolve))
      .then(() => responsePromise);
  }

  send(commands) {
    if (Array.isArray(commands)) {
      // Multiple commands means the response does not matter. There might even
      // never be a response.
      return Promise
        .all(commands.map((command) => this._sendMessage(command)))
        .then(() => {});
    }
    return this._sendMessage(commands);
  }

  waitForNotification(methodOrPredicate, description) {
    const desc = description || methodOrPredicate;
    const message = `Timed out waiting for matching notification (${desc})`;
    return fires(
      this._asyncWaitForNotification(methodOrPredicate), message, TIMEOUT);
  }

  async _asyncWaitForNotification(methodOrPredicate) {
    function matchMethod(notification) {
      return notification.method === methodOrPredicate;
    }
    const predicate =
        typeof methodOrPredicate === 'string' ? matchMethod : methodOrPredicate;
    let notification = null;
    do {
      if (this._unprocessedNotifications.length) {
        notification = this._unprocessedNotifications.shift();
      } else {
        notification = await new Promise(
          (resolve) => this._notificationCallback = resolve);
      }
    } while (!predicate(notification));
    return notification;
  }

  _isBreakOnLineNotification(message, line, expectedScriptPath) {
    if (message.method === 'Debugger.paused') {
      const callFrame = message.params.callFrames[0];
      const location = callFrame.location;
      const scriptPath = this._scriptsIdsByUrl.get(location.scriptId);
      assert.strictEqual(decodeURIComponent(scriptPath),
                         decodeURIComponent(expectedScriptPath),
                         `${scriptPath} !== ${expectedScriptPath}`);
      assert.strictEqual(location.lineNumber, line);
      return true;
    }
  }

  waitForBreakOnLine(line, url) {
    return this
      .waitForNotification(
        (notification) =>
          this._isBreakOnLineNotification(notification, line, url),
        `break on ${url}:${line}`);
  }

  waitForPauseOnStart() {
    return this
      .waitForNotification(
        (notification) =>
          notification.method === 'Debugger.paused' && notification.params.reason === 'Break on start',
        'break on start');
  }

  pausedDetails() {
    return this._pausedDetails;
  }

  _matchesConsoleOutputNotification(notification, type, values) {
    if (!Array.isArray(values))
      values = [ values ];
    if (notification.method === 'Runtime.consoleAPICalled') {
      const params = notification.params;
      if (params.type === type) {
        let i = 0;
        for (const value of params.args) {
          if (value.value !== values[i++])
            return false;
        }
        return i === values.length;
      }
    }
  }

  waitForConsoleOutput(type, values) {
    const desc = `Console output matching ${JSON.stringify(values)}`;
    return this.waitForNotification(
      (notification) => this._matchesConsoleOutputNotification(notification,
                                                               type, values),
      desc);
  }

  async runToCompletion() {
    console.log('[test]', 'Verify node waits for the frontend to disconnect');
    await this.send({ 'method': 'Debugger.resume' });
    await this.waitForNotification((notification) => {
      if (notification.method === 'Debugger.paused') {
        this.send({ 'method': 'Debugger.resume' });
      }
      return notification.method === 'Runtime.executionContextDestroyed' &&
        notification.params.executionContextId === 1;
    });
    await this.waitForDisconnect();
  }

  async waitForDisconnect() {
    while ((await this._instance.nextStderrString()) !==
              'Waiting for the debugger to disconnect...');
    await this.disconnect();
  }

  scriptPath() {
    return this._instance.scriptPath();
  }

  script() {
    return this._instance.script();
  }

  scriptURL() {
    return pathToFileURL(this.scriptPath());
  }
}

class NodeInstance extends EventEmitter {
  constructor(inspectorFlags = ['--inspect-brk=0', '--expose-internals'],
              scriptContents = '',
              scriptFile = _MAINSCRIPT,
              logger = console) {
    super();

    this._logger = logger;
    this._scriptPath = scriptFile;
    this._script = scriptFile ? null : scriptContents;
    this._portCallback = null;
    this.resetPort();
    this._process = spawnChildProcess(inspectorFlags, scriptContents,
                                      scriptFile);
    this._running = true;
    this._stderrLineCallback = null;
    this._unprocessedStderrLines = [];

    this._process.stdout.on('data', makeBufferingDataCallback(
      (line) => {
        this.emit('stdout', line);
        this._logger.log('[out]', line);
      }));

    this._process.stderr.on('data', makeBufferingDataCallback(
      (message) => this.onStderrLine(message)));

    this._shutdownPromise = new Promise((resolve) => {
      this._process.once('exit', (exitCode, signal) => {
        if (signal) {
          this._logger.error(`[err] child process crashed, signal ${signal}`);
        }
        resolve({ exitCode, signal });
        this._running = false;
      });
    });
  }

  get pid() {
    return this._process.pid;
  }

  resetPort() {
    this.portPromise = new Promise((resolve) => this._portCallback = resolve);
  }

  static async startViaSignal(scriptContents) {
    const instance = new NodeInstance(
      ['--expose-internals', '--inspect-port=0'],
      `${scriptContents}\nprocess._rawDebug('started');`, undefined);
    const msg = 'Timed out waiting for process to start';
    while (await fires(instance.nextStderrString(), msg, TIMEOUT) !== 'started');
    process._debugProcess(instance._process.pid);
    return instance;
  }

  onStderrLine(line) {
    this.emit('stderr', line);
    this._logger.log('[err]', line);
    if (this._portCallback) {
      const matches = line.match(/Debugger listening on ws:\/\/.+:(\d+)\/.+/);
      if (matches) {
        this._portCallback(matches[1]);
        this._portCallback = null;
      }
    }
    if (this._stderrLineCallback) {
      this._stderrLineCallback(line);
      this._stderrLineCallback = null;
    } else {
      this._unprocessedStderrLines.push(line);
    }
  }

  httpGet(host, path, hostHeaderValue) {
    this._logger.log('[test]', `Testing ${path}`);
    const headers = hostHeaderValue ? { 'Host': hostHeaderValue } : null;
    return this.portPromise.then((port) => new Promise((resolve, reject) => {
      const req = http.get({ host, port, family: 4, path, headers }, (res) => {
        let response = '';
        res.setEncoding('utf8');
        res
          .on('data', (data) => response += data.toString())
          .on('end', () => {
            resolve(response);
          });
      });
      req.on('error', reject);
    })).then((response) => {
      try {
        return JSON.parse(response);
      } catch (e) {
        e.body = response;
        throw e;
      }
    });
  }

  async sendUpgradeRequest() {
    const response = await this.httpGet(null, '/json/list');
    const devtoolsUrl = response[0].webSocketDebuggerUrl;
    const port = await this.portPromise;
    return http.get({
      port,
      family: 4,
      path: new URL(devtoolsUrl).pathname,
      headers: {
        'Connection': 'Upgrade',
        'Upgrade': 'websocket',
        'Sec-WebSocket-Version': 13,
        'Sec-WebSocket-Key': 'key==',
      },
    });
  }

  async connectInspectorSession() {
    this._logger.log('[test]', 'Connecting to a child Node process');
    const upgradeRequest = await this.sendUpgradeRequest();
    return new Promise((resolve) => {
      upgradeRequest
        .on('upgrade',
            (message, socket) => resolve(new InspectorSession(socket, this)))
        .on('response', common.mustNotCall('Upgrade was not received'));
    });
  }

  async expectConnectionDeclined() {
    this._logger.log('[test]', 'Checking upgrade is not possible');
    const upgradeRequest = await this.sendUpgradeRequest();
    return new Promise((resolve) => {
      upgradeRequest
          .on('upgrade', common.mustNotCall('Upgrade was received'))
          .on('response', (response) =>
            response.on('data', () => {})
                    .on('end', () => resolve(response.statusCode)));
    });
  }

  expectShutdown() {
    return this._shutdownPromise;
  }

  nextStderrString() {
    if (this._unprocessedStderrLines.length)
      return Promise.resolve(this._unprocessedStderrLines.shift());
    return new Promise((resolve) => this._stderrLineCallback = resolve);
  }

  write(message) {
    this._process.stdin.write(message);
  }

  kill() {
    this._process.kill();
    return this.expectShutdown();
  }

  scriptPath() {
    return this._scriptPath;
  }

  script() {
    if (this._script === null)
      this._script = fs.readFileSync(this.scriptPath(), 'utf8');
    return this._script;
  }
}

function onResolvedOrRejected(promise, callback) {
  return promise.then((result) => {
    callback();
    return result;
  }, (error) => {
    callback();
    throw error;
  });
}

function timeoutPromise(error, timeoutMs) {
  let clearCallback = null;
  let done = false;
  const promise = onResolvedOrRejected(new Promise((resolve, reject) => {
    const timeout = setTimeout(() => reject(error), timeoutMs);
    clearCallback = () => {
      if (done)
        return;
      clearTimeout(timeout);
      resolve();
    };
  }), () => done = true);
  promise.clear = clearCallback;
  return promise;
}

// Returns a new promise that will propagate `promise` resolution or rejection
// if that happens within the `timeoutMs` timespan, or rejects with `error` as
// a reason otherwise.
function fires(promise, error, timeoutMs) {
  const timeout = timeoutPromise(error, timeoutMs);
  return Promise.race([
    onResolvedOrRejected(promise, () => timeout.clear()),
    timeout,
  ]);
}

/**
 * When waiting for inspector events, there might be no handles on the event
 * loop, and leads to process exits.
 *
 * This function provides a utility to wait until a inspector event for a certain
 * time.
 */
function waitUntil(session, eventName, timeout = 1000) {
  const resolvers = Promise.withResolvers();
  const timer = setTimeout(() => {
    resolvers.reject(new Error(`Wait for inspector event ${eventName} timed out`));
  }, timeout);

  once(session, eventName)
    .then((res) => {
      resolvers.resolve(res);
      clearTimeout(timer);
    }, (error) => {
      // This should never happen.
      resolvers.reject(error);
      clearTimeout(timer);
    });

  return resolvers.promise;
}

module.exports = {
  NodeInstance,
  waitUntil,
};
