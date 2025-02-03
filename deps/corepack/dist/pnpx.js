#!/usr/bin/env node
process.env.COREPACK_ENABLE_DOWNLOAD_PROMPT??='1'
require('module').enableCompileCache?.();
require('./lib/corepack.cjs').runMain(['pnpx', ...process.argv.slice(2)]);