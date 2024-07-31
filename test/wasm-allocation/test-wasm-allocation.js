// Flags: --disable-wasm-trap-handler
// Test that with limited virtual memory space, --disable-wasm-trap-handler
// allows WASM to at least run with inline bound checks.
'use strict';

require('../common');
new WebAssembly.Memory({ initial: 10, maximum: 100 });
