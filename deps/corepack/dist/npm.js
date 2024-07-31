#!/usr/bin/env node
process.env.COREPACK_ENABLE_DOWNLOAD_PROMPT??='1'
require('./lib/corepack.cjs').runMain(['npm', ...process.argv.slice(2)]);