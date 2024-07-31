'use strict';

require('../common');
const assert = require('assert');
const {
  WebSocket: NodeHttpWebSocket,
  CloseEvent: NodeHttpCloseEvent,
  MessageEvent: NodeHttpMessageEvent
} = require('node:http');

// Compare with global objects
assert.strictEqual(NodeHttpWebSocket, WebSocket);
assert.strictEqual(NodeHttpCloseEvent, CloseEvent);
assert.strictEqual(NodeHttpMessageEvent, MessageEvent);
