import { mustCall } from '../common/index.mjs';
import { strictEqual } from 'assert';
import { AsyncLocalStorage } from 'async_hooks';
import { Agent, createServer, get } from 'http';

const asyncLocalStorage = new AsyncLocalStorage();

const agent = new Agent({
  maxSockets: 1
});

const N = 3;
let responses = 0;

const server = createServer(mustCall((req, res) => {
  res.end('ok');
}, N));

server.listen(0, mustCall(() => {
  const port = server.address().port;

  for (let i = 0; i < N; i++) {
    asyncLocalStorage.run(i, () => {
      get({ agent, port }, mustCall((res) => {
        strictEqual(asyncLocalStorage.getStore(), i);
        if (++responses === N) {
          server.close();
          agent.destroy();
        }
        res.resume();
      }));
    });
  }
}));
