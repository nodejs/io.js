'use strict';
require('../common');
const assert = require('assert');
const net = require('net');

{
  const invalidKeys = [
    'objectMode',
    'readableObjectMode',
    'writableObjectMode',
  ];
  invalidKeys.forEach((invalidKey) => {
    const option = {
      port: 8080,
      [invalidKey]: true
    };
    const message = `The property 'options.${invalidKey}' is not supported. Received true`;

    assert.throws(() => {
      net.createConnection(option);
    }, {
      code: 'ERR_INVALID_ARG_VALUE',
      name: 'TypeError',
      message: new RegExp(message)
    });
  });
}

{
  assert.throws(() => {
    net.createConnection({
      host: ['192.168.0.1'],
      port: 8080,
    });
  }, {
    code: 'ERR_INVALID_ARG_TYPE',
    name: 'TypeError',
  });
}
