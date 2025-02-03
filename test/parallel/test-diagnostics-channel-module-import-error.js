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
  error: common.mustCall(track('error')),
});

import('does-not-exist').then(
  common.mustNotCall(),
  common.mustCall((error) => {
    const expectedParentURL = pathToFileURL(module.filename).href;
    // Verify order and contents of each event
    assert.deepStrictEqual(events, [
      {
        name: 'start',
        parentURL: expectedParentURL,
        url: 'does-not-exist',
      },
      {
        name: 'end',
        parentURL: expectedParentURL,
        url: 'does-not-exist',
      },
      {
        name: 'error',
        parentURL: expectedParentURL,
        url: 'does-not-exist',
        error,
      },
      {
        name: 'asyncStart',
        parentURL: expectedParentURL,
        url: 'does-not-exist',
        error,
      },
      {
        name: 'asyncEnd',
        parentURL: expectedParentURL,
        url: 'does-not-exist',
        error,
      },
    ]);
  })
);
