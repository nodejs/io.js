'use strict';

const common = require('../common');

const bench = common.createBenchmark(main, {
  len: [4, 8, 16, 32],
  n: [1e5],
}, {
  flags: ['--expose-internals', '--no-warnings'],
});

function main({ len, n }) {
  const { HTTPParser } = common.binding('http_parser');
  const REQUEST = HTTPParser.REQUEST;
  const kOnHeaders = HTTPParser.kOnHeaders | 0;
  const kOnHeadersComplete = HTTPParser.kOnHeadersComplete | 0;
  const kOnBody = HTTPParser.kOnBody | 0;
  const kOnMessageComplete = HTTPParser.kOnMessageComplete | 0;
  const CRLF = '\r\n';

  function processHeader(header, n) {
    const parser = newParser(REQUEST);

    bench.start();
    for (let i = 0; i < n; i++) {
      parser.execute(header, 0, header.length);
      parser.initialize(REQUEST, {});
    }
    bench.end(n);
  }

  function newParser(type) {
    const parser = new HTTPParser();
    parser.initialize(type, {});

    parser.headers = [];

    parser[kOnHeaders] = function() { };
    parser[kOnHeadersComplete] = function() { };
    parser[kOnBody] = function() { };
    parser[kOnMessageComplete] = function() { };

    return parser;
  }

  let header = `GET /hello HTTP/1.1${CRLF}Content-Type: text/plain${CRLF}`;

  for (let i = 0; i < len; i++) {
    header += `X-Filler${i}: ${Math.random().toString(36).substring(2)}${CRLF}`;
  }
  header += CRLF;

  processHeader(Buffer.from(header), n);
}
