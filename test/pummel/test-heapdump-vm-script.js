'use strict';
require('../common');
const { findByRetainingPath } = require('../common/heap');
const source = 'const foo = 123';
const script = require('vm').createScript(source);

findByRetainingPath('Node / ContextifyScript', [
  { node_name: '(shared function info)' },  // This is the UnboundScript referenced by ContextifyScript.
  { edge_name: 'script' },
  { edge_name: 'source', node_type: 'string', node_name: source },
]);

console.log(script); // Keep the script alive.
