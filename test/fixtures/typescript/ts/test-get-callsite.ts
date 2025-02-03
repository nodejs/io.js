const { getCallSites } = require('node:util');

interface CallSite {
  A;
  B;
}

const callSite = getCallSites()[0];

console.log('getCallSite: ', callSite);
