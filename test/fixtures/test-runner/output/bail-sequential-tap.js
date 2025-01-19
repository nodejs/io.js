'use strict';

require('../../../common');
const fixtures = require('../../../common/fixtures');
const spawn = require('node:child_process').spawn;

spawn(process.execPath,
      ['--no-warnings',
       '--test-reporter=tap',
       '--test-bail',
       fixtures.path('test-runner/bailout/sequential-output/test.js'),
      ],
      {
        stdio: 'inherit',
      },
);
