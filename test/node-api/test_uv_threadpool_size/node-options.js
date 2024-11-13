'use strict';

const common = require('../../common');
const assert = require('assert');
const path = require('path');
const { spawnSync } = require('child_process');

if (process.config.variables.node_without_node_options) {
  common.skip('missing NODE_OPTIONS support');
}

const uvThreadPoolPath = '../../fixtures/dotenv/uv-threadpool.env';

// Should update UV_THREADPOOL_SIZE
const filePath = path.join(__dirname, `./build/${common.buildType}/test_uv_threadpool_size`);
const code = `
   const { test } = require(${JSON.stringify(filePath)});
   const size = parseInt(process.env.UV_THREADPOOL_SIZE, 10);
   require('assert').strictEqual(size, 4);
   test(size);
 `.trim();
const child = spawnSync(
  process.execPath,
  [ `--env-file=${uvThreadPoolPath}`, '--eval', code ],
  { cwd: __dirname, encoding: 'utf-8' },
);
assert.strictEqual(child.stderr, '');
assert.strictEqual(child.status, 0);
