'use strict';

const common = require('../common');
if ((!common.hasCrypto) || (!common.hasIntl)) {
  common.skip('ESLint tests require crypto and Intl');
}

common.skipIfEslintMissing();

const RuleTester = require('../../tools/eslint/node_modules/eslint').RuleTester;
const rule = require('../../tools/eslint-rules/alphabetize-primordials');

new RuleTester()
  .run('alphabetize-primordials', rule, {
    valid: [
      'new Array()',
      '"use strict";const {\nArray\n} = primordials;',
      '"use strict";const {\n\tArray,\n\tDate,\n} = primordials',
      '"use strict";const {\nDate,Array\n} = notPrimordials',
      '"use strict";const {\nDate,globalThis:{Array}\n} = primordials',
      '"use strict";const {\nBigInt,globalThis:{Array,Date,SharedArrayBuffer,parseInt}\n} = primordials',
      '"use strict";const {\nFunctionPrototypeBind,Uint32Array,globalThis:{SharedArrayBuffer}\n} = primordials',
      {
        code: '"use strict";const fs = require("fs");const {\nArray\n} = primordials',
        options: [{ enforceTopPosition: false }],
      },
    ],
    invalid: [
      {
        code: '"use strict";const {Array} = primordials;',
        errors: [{ message: /destructuring from primordials should be multiline/ }],
      },
      {
        code: '"use strict";const fs = require("fs");const {Date,Array} = primordials',
        errors: [
          { message: /destructuring from primordials should be multiline/ },
          { message: /destructuring from primordials should be the first expression/ },
          { message: /Date >= Array/ },
        ],
      },
      {
        code: 'function fn() {"use strict";const {\nArray,\n} = primordials}',
        errors: [{ message: /destructuring from primordials should be the first expression/ }],
      },
      {
        code: '"use strict";const {\nDate,Array} = primordials',
        errors: [{ message: /Date >= Array/ }],
      },
      {
        code: '"use strict";const {\nglobalThis:{Date, Array}} = primordials',
        errors: [{ message: /Date >= Array/ }],
      },
    ]
  });
