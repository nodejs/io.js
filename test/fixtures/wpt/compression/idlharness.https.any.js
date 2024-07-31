// META: script=/resources/WebIDLParser.js
// META: script=/resources/idlharness.js

'use strict';

// https://wicg.github.io/compression/

idl_test(
  ['compression'],
  ['streams'],
  idl_array => {
    idl_array.add_objects({
      CompressionStream: ['new CompressionStream("deflate")'],
      DecompressionStream: ['new DecompressionStream("deflate")'],
    });
  }
);
