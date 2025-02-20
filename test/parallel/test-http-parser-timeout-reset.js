// Flags: --expose-internals
'use strict';
const common = require('../common');

const net = require('net');
const { internalBinding } = require('internal/test/binding');
const { HTTPParser } = internalBinding('http_parser');

const server = net.createServer((socket) => {
  socket.write('HTTP/1.1 200 OK\r\n');
  socket.write('Transfer-Encoding: chunked\r\n\r\n');
  setTimeout(() => {
    socket.write('1\r\n');
    socket.write('\n\r\n');
    setTimeout(() => {
      socket.write('1\r\n');
      socket.write('\n\r\n');
      setImmediate(() => {
        socket.destroy();
        server.close();
      });
    }, 500);
  }, 500);
}).listen(0, () => {
  const socket = net.connect(server.address().port);
  const parser = new HTTPParser(HTTPParser.RESPONSE, false);
  parser.initialize(
    HTTPParser.RESPONSE,
    {},
    0,
    0,
  );

  parser[HTTPParser.kOnTimeout] = common.mustNotCall();

  parser[HTTPParser.kOnHeaders] = common.mustNotCall();

  parser[HTTPParser.kOnExecute] = common.mustCallAtLeast(3);

  parser[HTTPParser.kOnHeadersComplete] = common.mustCall();

  parser[HTTPParser.kOnBody] = common.mustCall(2);

  parser[HTTPParser.kOnMessageComplete] = common.mustNotCall();

  parser.consume(socket._handle);
});
