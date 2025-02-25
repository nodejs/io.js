import '../common/index.mjs';

// Regression tests for https://github.com/nodejs/node/issues/40693

import { deepStrictEqual } from 'assert';
import { createSocket } from 'dgram';
import { AsyncLocalStorage } from 'async_hooks';

createSocket('udp4')
  .on('message', function(msg, rinfo) { this.send(msg, rinfo.port); })
  .on('listening', function() {
    const asyncLocalStorage = new AsyncLocalStorage();
    const store = { val: 'abcd' };
    asyncLocalStorage.run(store, () => {
      const client = createSocket('udp4');
      client.on('message', (msg, rinfo) => {
        deepStrictEqual(asyncLocalStorage.getStore(), store);
        client.close();
        this.close();
      });
      client.send('Hello, world!', this.address().port);
    });
  })
  .bind(0);
