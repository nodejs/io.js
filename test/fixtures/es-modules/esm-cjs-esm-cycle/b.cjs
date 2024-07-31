const result = require('./a.mjs');
module.exports = result;
console.log('require a.mjs in b.cjs', result.default);
