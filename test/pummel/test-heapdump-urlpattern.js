// Flags: --expose-internals
'use strict';
require('../common');
const { validateSnapshotNodes } = require('../common/heap');
const { URLPattern } = require('node:url');

validateSnapshotNodes('Node / URLPattern', []);
const urlPattern = new URLPattern('https://example.com/:id');
validateSnapshotNodes('Node / URLPattern', [
  {
    children: [
      { node_name: 'Node / ada::url_pattern', edge_name: 'url_pattern' },
    ],
  },
]);
validateSnapshotNodes('Node / ada::url_pattern', [
  {
    children: [
      { node_name: 'Node / ada::url_pattern_component', edge_name: 'protocol_component' },
    ],
  },
]);

// Use `urlPattern`.
console.log(urlPattern);
