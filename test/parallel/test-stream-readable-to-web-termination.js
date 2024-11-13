'use strict';
require('../common');
const { Readable } = require('stream');

{
  const r = Readable.from([]);
  // Cancelling reader while closing should not cause uncaught exceptions
  r.on('close', () => reader.cancel());

  const reader = Readable.toWeb(r).getReader();
  reader.read();
}
