'use strict';
require('../common');
const { Readable } = require('stream');

{
  const r = Readable.from(['data']);

  const wrapper = Readable.fromWeb(Readable.toWeb(r));

  wrapper.on('data', () => {
    // Destroying wrapper while emitting data should not cause uncaught
    // exceptions
    wrapper.destroy();
  });
}
