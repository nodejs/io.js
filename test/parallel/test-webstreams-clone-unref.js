'use strict';

require('../common');
const { ok } = require('node:assert');

// This test verifies that cloned ReadableStream and WritableStream instances
// do not keep the process alive. The test fails if it timesout (it should just
// exit immediately)

const rs1 = new ReadableStream();
const ws1 = new WritableStream();

const [rs2, ws2] = structuredClone([rs1, ws1], { transfer: [rs1, ws1] });

ok(rs2 instanceof ReadableStream);
ok(ws2 instanceof WritableStream);
