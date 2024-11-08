'use strict';

let internalTTy;
function lazyInternalTTY() {
  internalTTy ??= require('internal/tty');
  return internalTTy;
}

module.exports = {
  blue: '',
  green: '',
  white: '',
  red: '',
  gray: '',
  clear: '',
  reset: '',
  hasColors: false,
  shouldColorize(stream) {
    if (process.env.FORCE_COLOR !== undefined) {
      return lazyInternalTTY().getColorDepth() > 2;
    }
    return stream?.isTTY && (
      typeof stream.getColorDepth === 'function' ?
        stream.getColorDepth() > 2 : true);
  },
  refresh() {
    const hasColors = module.exports.shouldColorize(process.stderr);
    module.exports.blue = hasColors ? '\u001b[34m' : '';
    module.exports.green = hasColors ? '\u001b[32m' : '';
    module.exports.white = hasColors ? '\u001b[39m' : '';
    module.exports.yellow = hasColors ? '\u001b[33m' : '';
    module.exports.red = hasColors ? '\u001b[31m' : '';
    module.exports.gray = hasColors ? '\u001b[90m' : '';
    module.exports.clear = hasColors ? '\u001bc' : '';
    module.exports.reset = hasColors ? '\u001b[0m' : '';
    module.exports.hasColors = hasColors;
  },
};

module.exports.refresh();
