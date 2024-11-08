const { getCallSites } = require('node:util');

interface CallSite {
  A;
  B;
}

const callSite = getCallSites({ sourceMap: false })[0];

console.log('mapCallSite: ', callSite);
