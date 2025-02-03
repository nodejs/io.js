'use strict';
const common = require('../common');
const assert = require('assert');
const dc = require('diagnostics_channel');
const { pathToFileURL } = require('url');

const trace = dc.tracingChannel('module.import');
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
  asyncStart: common.mustCall(track('asyncStart')),
  asyncEnd: common.mustCall(track('asyncEnd')),
  error: common.mustNotCall(track('error')),
});

import('http').then(
  common.mustCall((result) => {
    const expectedParentURL = pathToFileURL(module.filename).href;
    // Verify order and contents of each event
    assert.deepStrictEqual(events, [
      {
        name: 'start',
        parentURL: expectedParentURL,
        url: 'http',
      },
      {
        name: 'end',
        parentURL: expectedParentURL,
        url: 'http',
      },
      {
        name: 'asyncStart',
        parentURL: expectedParentURL,
        url: 'http',
        result,
      },
      {
        name: 'asyncEnd',
        parentURL: expectedParentURL,
        url: 'http',
        result,
      },
    ]);
  }),
  common.mustNotCall(),
);
