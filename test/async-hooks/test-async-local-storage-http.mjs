import '../common/index.mjs';
import { strictEqual } from 'assert';
import { AsyncLocalStorage } from 'async_hooks';
import { createServer, get } from 'http';

const asyncLocalStorage = new AsyncLocalStorage();
const server = createServer((req, res) => {
  res.end('ok');
});

server.listen(0, () => {
  asyncLocalStorage.run(new Map(), () => {
    const store = asyncLocalStorage.getStore();
    store.set('hello', 'world');
    get({ host: 'localhost', port: server.address().port }, () => {
      strictEqual(asyncLocalStorage.getStore().get('hello'), 'world');
      server.close();
    });
  });
});
