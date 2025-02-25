import { hasCrypto, skip } from '../common/index.mjs';
if (!hasCrypto)
  skip('missing crypto');

// Regression tests for https://github.com/nodejs/node/issues/40693

import { deepStrictEqual } from 'assert';
import { readKey } from '../common/fixtures.mjs';
import { createServer, connect } from 'tls';
import { AsyncLocalStorage } from 'async_hooks';

const options = {
  cert: readKey('rsa_cert.crt'),
  key: readKey('rsa_private.pem'),
  rejectUnauthorized: false
};

createServer(options, (socket) => {
    socket.write('Hello, world!');
    socket.pipe(socket);
  })
  .listen(0, function() {
    const asyncLocalStorage = new AsyncLocalStorage();
    const store = { val: 'abcd' };
    asyncLocalStorage.run(store, () => {
      const client = connect({ port: this.address().port, ...options });
      client.on('data', () => {
        deepStrictEqual(asyncLocalStorage.getStore(), store);
        client.end();
        this.close();
      });
    });
  });
