'use strict';
const common = require('../common');
const assert = require('assert');
const dc = require('diagnostics_channel');

const trace = dc.tracingChannel('module.require');
const events = [];
let lastEvent;

function track(name) {
  return (event) => {
    // Verify every event after the first is the same object
    if (events.length) {
      assert.strictEqual(event, lastEvent);
    }
    lastEvent = event;

    events.push({ name, ...event });
  };
}

trace.subscribe({
  start: common.mustCall(track('start')),
  end: common.mustCall(track('end')),
  asyncStart: common.mustNotCall(track('asyncStart')),
  asyncEnd: common.mustNotCall(track('asyncEnd')),
  error: common.mustNotCall(track('error')),
});

const result = require('http');

// Verify order and contents of each event
assert.deepStrictEqual(events, [
  {
    name: 'start',
    parentFilename: module.filename,
    id: 'http',
  },
  {
    name: 'end',
    parentFilename: module.filename,
    id: 'http',
    result,
  },
]);
