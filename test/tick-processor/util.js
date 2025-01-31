'use strict';

// Utilities for the tick-processor tests
const {
  isWindows,
  isSunOS,
  isAIX,
  isFreeBSD,
} = require('../common');

const { endianness } = require('os');

function isLinuxPPCBE() {
  return (process.platform === 'linux') &&
         (process.arch === 'ppc64') &&
         (endianness() === 'BE');
}

module.exports = {
  isCPPSymbolsNotMapped: isWindows ||
                         isSunOS ||
                         isAIX ||
                         isLinuxPPCBE() ||
                         isFreeBSD,
};
