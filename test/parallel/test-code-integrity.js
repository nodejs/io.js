// Flags: --expose-internals

'use strict';

require('../common');
const assert = require('node:assert');
const { describe, it } = require('node:test');
const ci = require('internal/code_integrity');

describe('cjs loader code integrity integration tests', () => {
  it('should throw an error if a .js file does not pass code integrity policy',
     (t) => {
       t.mock.method(ci, ci.isAllowedToExecuteFile.name, () => { return false; });

       assert.throws(
         () => {
           require('../fixtures/code_integrity_test.js');
         },
         {
           code: 'ERR_CODE_INTEGRITY_VIOLATION',
         },
       );
     }
  );
  it('should NOT throw an error if a .js file passes code integrity policy',
     (t) => {
       t.mock.method(ci, ci.isAllowedToExecuteFile.name, () => { return true; });

       assert.ok(
         require('../fixtures/code_integrity_test.js')
       );
     }
  );
  it('should throw an error if a .json file does not pass code integrity policy',
     (t) => {
       t.mock.method(ci, ci.isAllowedToExecuteFile.name, () => { return false; });

       assert.throws(
         () => {
           require('../fixtures/code_integrity_test.json');
         },
         {
           code: 'ERR_CODE_INTEGRITY_VIOLATION',
         },
       );
     }
  );
  it('should NOT throw an error if a .json file passes code integrity policy',
     (t) => {
       t.mock.method(ci, ci.isAllowedToExecuteFile.name, () => { return true; });

       assert.ok(
         require('../fixtures/code_integrity_test.json')
       );
     }
  );
  it('should throw an error if a .node file does not pass code integrity policy',
     (t) => {
       t.mock.method(ci, ci.isAllowedToExecuteFile.name, () => { return false; });

       assert.throws(
         () => {
           require('../fixtures/code_integrity_test.node');
         },
         {
           code: 'ERR_CODE_INTEGRITY_VIOLATION',
         },
       );
     }
  );
});
