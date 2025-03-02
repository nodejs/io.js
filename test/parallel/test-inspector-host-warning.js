'use strict';

const common = require('../common');
common.skipIfInspectorDisabled();
common.skipIfWorker();

const inspector = require('inspector');
inspector.open(0, '0.0.0.0', false);
common.expectWarning(
  'SecurityWarning',
  'Binding the inspector to a public IP with an open port is insecure, ' +
    'as it allows external hosts to connect to the inspector ' +
    'and perform a remote code execution attack. ' +
    'Documentation can be found at ' +
    'https://nodejs.org/api/cli.html#--inspecthostport'
);
inspector.close();
