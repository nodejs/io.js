'use strict';

const common = require('../common.js');
const http = require('http');

const bench = common.createBenchmark(main, {
  fewHeaders: {
    n: [10],
    len: [1, 5],
    duration: 5,
  },
  mediumHeaders: {
    n: [50],
    len: [1, 10],
    duration: 5,
  },
  manyHeaders: {
    n: [600],
    len: [1, 100],
    duration: 5,
  },
}, { byGroups: true });

function main({ len, n, duration }) {
  const headers = {
    'Connection': 'keep-alive',
    'Transfer-Encoding': 'chunked',
  };

  const Is = [...Array(n / len).keys()];
  const Js = [...Array(len).keys()];

  for (const i of Is) {
    headers[`foo${i}`] = Js.map(() => `some header value ${i}`);
  }

  const server = http.createServer((req, res) => {
    res.writeHead(200, headers);
    res.end();
  });

  server.listen(0, () => {
    bench.http({
      path: '/',
      connections: 10,
      duration,
      port: server.address().port,
    }, () => {
      server.close();
    });
  });
}
