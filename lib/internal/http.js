'use strict';

const {
  Date,
  Symbol,
} = primordials;

const { setUnrefTimeout } = require('internal/timers');
const { getCategoryEnabledBuffer, trace } = internalBinding('trace_events');
const {
  CHAR_LOWERCASE_B,
  CHAR_LOWERCASE_E,
} = require('internal/constants');

let utcCache;

function utcDate() {
  if (!utcCache) cache();
  return utcCache;
}

function cache() {
  const d = new Date();
  utcCache = d.toUTCString();
  setUnrefTimeout(resetCache, 1000 - d.getMilliseconds());
}

function resetCache() {
  utcCache = undefined;
}

let traceEventId = 0;

function getNextTraceEventId() {
  return ++traceEventId;
}

const httpEnabled = getCategoryEnabledBuffer('node.http');

function isTraceHTTPEnabled() {
  return httpEnabled[0] > 0;
}

const traceEventCategory = 'node,node.http';

function traceBegin(...args) {
  trace(CHAR_LOWERCASE_B, traceEventCategory, ...args);
}

function traceEnd(...args) {
  trace(CHAR_LOWERCASE_E, traceEventCategory, ...args);
}

module.exports = {
  kOutHeaders: Symbol('kOutHeaders'),
  kNeedDrain: Symbol('kNeedDrain'),
  utcDate,
  traceBegin,
  traceEnd,
  getNextTraceEventId,
  isTraceHTTPEnabled,
};
