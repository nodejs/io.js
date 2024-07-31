#!/usr/bin/env node
process.env.COREPACK_ENABLE_DOWNLOAD_PROMPT??='0';
require('./lib/corepack.cjs').runMain(process.argv.slice(2));