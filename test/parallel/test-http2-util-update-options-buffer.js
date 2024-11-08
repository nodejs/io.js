// Flags: --expose-internals
'use strict';

const common = require('../common');
if (!common.hasCrypto)
  common.skip('missing crypto');

// Test coverage for the updateOptionsBuffer method used internally
// by the http2 implementation.

const { updateOptionsBuffer } = require('internal/http2/util');
const { internalBinding } = require('internal/test/binding');
const { optionsBuffer } = internalBinding('http2');
const { ok, strictEqual } = require('assert');

const IDX_OPTIONS_MAX_DEFLATE_DYNAMIC_TABLE_SIZE = 0;
const IDX_OPTIONS_MAX_RESERVED_REMOTE_STREAMS = 1;
const IDX_OPTIONS_MAX_SEND_HEADER_BLOCK_LENGTH = 2;
const IDX_OPTIONS_PEER_MAX_CONCURRENT_STREAMS = 3;
const IDX_OPTIONS_PADDING_STRATEGY = 4;
const IDX_OPTIONS_MAX_HEADER_LIST_PAIRS = 5;
const IDX_OPTIONS_MAX_OUTSTANDING_PINGS = 6;
const IDX_OPTIONS_MAX_OUTSTANDING_SETTINGS = 7;
const IDX_OPTIONS_MAX_SESSION_MEMORY = 8;
const IDX_OPTIONS_MAX_SETTINGS = 9;
const IDX_OPTIONS_STREAM_RESET_RATE = 10;
const IDX_OPTIONS_STREAM_RESET_BURST = 11;
const IDX_OPTIONS_FLAGS = 12;

{
  updateOptionsBuffer({
    maxDeflateDynamicTableSize: 1,
    maxReservedRemoteStreams: 2,
    maxSendHeaderBlockLength: 3,
    peerMaxConcurrentStreams: 4,
    paddingStrategy: 5,
    maxHeaderListPairs: 6,
    maxOutstandingPings: 7,
    maxOutstandingSettings: 8,
    maxSessionMemory: 9,
    maxSettings: 10,
    streamResetRate: 11,
    streamResetBurst: 12,
  });

  strictEqual(optionsBuffer[IDX_OPTIONS_MAX_DEFLATE_DYNAMIC_TABLE_SIZE], 1);
  strictEqual(optionsBuffer[IDX_OPTIONS_MAX_RESERVED_REMOTE_STREAMS], 2);
  strictEqual(optionsBuffer[IDX_OPTIONS_MAX_SEND_HEADER_BLOCK_LENGTH], 3);
  strictEqual(optionsBuffer[IDX_OPTIONS_PEER_MAX_CONCURRENT_STREAMS], 4);
  strictEqual(optionsBuffer[IDX_OPTIONS_PADDING_STRATEGY], 5);
  strictEqual(optionsBuffer[IDX_OPTIONS_MAX_HEADER_LIST_PAIRS], 6);
  strictEqual(optionsBuffer[IDX_OPTIONS_MAX_OUTSTANDING_PINGS], 7);
  strictEqual(optionsBuffer[IDX_OPTIONS_MAX_OUTSTANDING_SETTINGS], 8);
  strictEqual(optionsBuffer[IDX_OPTIONS_MAX_SESSION_MEMORY], 9);
  strictEqual(optionsBuffer[IDX_OPTIONS_MAX_SETTINGS], 10);
  strictEqual(optionsBuffer[IDX_OPTIONS_STREAM_RESET_RATE], 11);
  strictEqual(optionsBuffer[IDX_OPTIONS_STREAM_RESET_BURST], 12);

  const flags = optionsBuffer[IDX_OPTIONS_FLAGS];

  ok(flags & (1 << IDX_OPTIONS_MAX_DEFLATE_DYNAMIC_TABLE_SIZE));
  ok(flags & (1 << IDX_OPTIONS_MAX_RESERVED_REMOTE_STREAMS));
  ok(flags & (1 << IDX_OPTIONS_MAX_SEND_HEADER_BLOCK_LENGTH));
  ok(flags & (1 << IDX_OPTIONS_PEER_MAX_CONCURRENT_STREAMS));
  ok(flags & (1 << IDX_OPTIONS_PADDING_STRATEGY));
  ok(flags & (1 << IDX_OPTIONS_MAX_HEADER_LIST_PAIRS));
  ok(flags & (1 << IDX_OPTIONS_MAX_OUTSTANDING_PINGS));
  ok(flags & (1 << IDX_OPTIONS_MAX_OUTSTANDING_SETTINGS));
  ok(flags & (1 << IDX_OPTIONS_MAX_SETTINGS));
  ok(flags & (1 << IDX_OPTIONS_STREAM_RESET_RATE));
  ok(flags & (1 << IDX_OPTIONS_STREAM_RESET_BURST));
}

{
  optionsBuffer[IDX_OPTIONS_MAX_SEND_HEADER_BLOCK_LENGTH] = 0;
  optionsBuffer[IDX_OPTIONS_MAX_OUTSTANDING_PINGS] = 0;

  updateOptionsBuffer({
    maxDeflateDynamicTableSize: 1,
    maxReservedRemoteStreams: 2,
    peerMaxConcurrentStreams: 4,
    paddingStrategy: 5,
    maxHeaderListPairs: 6
  });

  strictEqual(optionsBuffer[IDX_OPTIONS_MAX_DEFLATE_DYNAMIC_TABLE_SIZE], 1);
  strictEqual(optionsBuffer[IDX_OPTIONS_MAX_RESERVED_REMOTE_STREAMS], 2);
  strictEqual(optionsBuffer[IDX_OPTIONS_PEER_MAX_CONCURRENT_STREAMS], 4);
  strictEqual(optionsBuffer[IDX_OPTIONS_PADDING_STRATEGY], 5);
  strictEqual(optionsBuffer[IDX_OPTIONS_MAX_HEADER_LIST_PAIRS], 6);
  strictEqual(optionsBuffer[IDX_OPTIONS_MAX_SEND_HEADER_BLOCK_LENGTH], 0);
  strictEqual(optionsBuffer[IDX_OPTIONS_MAX_OUTSTANDING_PINGS], 0);

  const flags = optionsBuffer[IDX_OPTIONS_FLAGS];

  ok(flags & (1 << IDX_OPTIONS_MAX_DEFLATE_DYNAMIC_TABLE_SIZE));
  ok(flags & (1 << IDX_OPTIONS_MAX_RESERVED_REMOTE_STREAMS));
  ok(flags & (1 << IDX_OPTIONS_PEER_MAX_CONCURRENT_STREAMS));
  ok(flags & (1 << IDX_OPTIONS_PADDING_STRATEGY));
  ok(flags & (1 << IDX_OPTIONS_MAX_HEADER_LIST_PAIRS));

  ok(!(flags & (1 << IDX_OPTIONS_MAX_SEND_HEADER_BLOCK_LENGTH)));
  ok(!(flags & (1 << IDX_OPTIONS_MAX_OUTSTANDING_PINGS)));
}
