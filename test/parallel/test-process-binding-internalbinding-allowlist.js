// Flags: --no-warnings
'use strict';

const common = require('../common');
const assert = require('assert');

// Assert that allowed internalBinding modules are accessible via
// process.binding().
assert(process.binding('buffer'));
assert(process.binding('cares_wrap'));
assert(process.binding('constants'));
assert(process.binding('contextify'));
assert(process.binding('fs'));
assert(process.binding('fs_event_wrap'));
if (common.hasIntl) {
  assert(process.binding('icu'));
}
assert(process.binding('inspector'));
assert(process.binding('js_stream'));
assert(process.binding('natives'));
assert(process.binding('os'));
assert(process.binding('pipe_wrap'));
assert(process.binding('spawn_sync'));
assert(process.binding('stream_wrap'));
assert(process.binding('tcp_wrap'));
if (common.hasCrypto) { // eslint-disable-line node-core/crypto-check
  assert(process.binding('tls_wrap'));
}
assert(process.binding('tty_wrap'));
assert(process.binding('udp_wrap'));
assert(process.binding('util'));
assert(process.binding('uv'));
assert(process.binding('zlib'));
