'use strict';

const common = require('../common');
const v8 = require('v8');

process.on('warning', common.mustNotCall());
v8.deserialize(v8.serialize(Buffer.alloc(0)));
v8.deserialize(v8.serialize({ a: new Int32Array(1024) }));
v8.deserialize(v8.serialize({ b: new Int16Array(8192) }));
v8.deserialize(v8.serialize({ c: new Uint32Array(1024) }));
v8.deserialize(v8.serialize({ d: new Uint16Array(8192) }));
