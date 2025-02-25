import '../common/index.mjs';
import { strictEqual } from 'assert';
import { executionAsyncResource, executionAsyncId, createHook } from 'async_hooks';
import { createServer, get } from 'http';

const hooked = {};
createHook({
  init(asyncId, type, triggerAsyncId, resource) {
    hooked[asyncId] = resource;
  }
}).enable();

const server = createServer((req, res) => {
  res.write('hello');
  setTimeout(() => {
    res.end(' world!');
  }, 1000);
});

server.listen(0, () => {
  strictEqual(executionAsyncResource(), hooked[executionAsyncId()]);
  get({ port: server.address().port }, (res) => {
    strictEqual(executionAsyncResource(), hooked[executionAsyncId()]);
    res.on('data', () => {
      strictEqual(executionAsyncResource(), hooked[executionAsyncId()]);
    });
    res.on('end', () => {
      strictEqual(executionAsyncResource(), hooked[executionAsyncId()]);
      server.close();
    });
  });
});
