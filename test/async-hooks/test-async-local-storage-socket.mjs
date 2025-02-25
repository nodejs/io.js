import '../common/index.mjs';

// Regression tests for https://github.com/nodejs/node/issues/40693

import { deepStrictEqual } from 'assert';
import { createServer, connect } from 'net';
import { AsyncLocalStorage } from 'async_hooks';

createServer((socket) => {
    socket.write('Hello, world!');
    socket.pipe(socket);
  })
  .listen(0, function() {
    const asyncLocalStorage = new AsyncLocalStorage();
    const store = { val: 'abcd' };
    asyncLocalStorage.run(store, () => {
      const client = connect({ port: this.address().port });
      client.on('data', () => {
        deepStrictEqual(asyncLocalStorage.getStore(), store);
        client.end();
        this.close();
      });
    });
  });
