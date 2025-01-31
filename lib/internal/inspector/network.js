'use strict';

const {
  NumberMAX_SAFE_INTEGER,
  Symbol,
} = primordials;

const { now } = require('internal/perf/utils');
const kInspectorRequestId = Symbol('kInspectorRequestId');

// https://chromedevtools.github.io/devtools-protocol/1-3/Network/#type-ResourceType
const kResourceType = {
  Document: 'Document',
  Stylesheet: 'Stylesheet',
  Image: 'Image',
  Media: 'Media',
  Font: 'Font',
  Script: 'Script',
  TextTrack: 'TextTrack',
  XHR: 'XHR',
  Fetch: 'Fetch',
  Prefetch: 'Prefetch',
  EventSource: 'EventSource',
  WebSocket: 'WebSocket',
  Manifest: 'Manifest',
  SignedExchange: 'SignedExchange',
  Ping: 'Ping',
  CSPViolationReport: 'CSPViolationReport',
  Preflight: 'Preflight',
  Other: 'Other',
};

/**
 * Return a monotonically increasing time in seconds since an arbitrary point in the past.
 * @returns {number}
 */
function getMonotonicTime() {
  return now() / 1000;
}

let requestId = 0;
function getNextRequestId() {
  if (requestId === NumberMAX_SAFE_INTEGER) {
    requestId = 0;
  }
  return `node-network-event-${++requestId}`;
};

module.exports = {
  kInspectorRequestId,
  kResourceType,
  getMonotonicTime,
  getNextRequestId,
};
