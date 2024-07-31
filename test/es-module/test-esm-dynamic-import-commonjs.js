'use strict';

const common = require('../common');
const fixtures = require('../common/fixtures');
const assert = require('node:assert');

(async () => {

  // Make sure that the CommonJS module lexer has been initialized.
  // See https://github.com/nodejs/node/blob/v21.1.0/lib/internal/modules/esm/translators.js#L61-L81.
  await import(fixtures.fileURL('empty.js'));

  let tickDuringCJSImport = false;
  process.nextTick(() => { tickDuringCJSImport = true; });
  await import(fixtures.fileURL('empty.cjs'));

  assert(!tickDuringCJSImport);

})().then(common.mustCall());
