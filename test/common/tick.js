'use strict';

module.exports = function tick(x, cb) {
  function ontick() {
    if (--x === 0) {
      if (typeof cb === 'function') cb();
    } else {
      setImmediate(ontick);
    }
  }
  setImmediate(ontick);
};
