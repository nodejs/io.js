'use strict';

const common = require('../common.js');
const crypto = require('crypto');
const http = require('http');

const GUID = '258EAFA5-E914-47DA-95CA-C5AB0DC85B11';

const configs = {
  size: [64, 16 * 1024, 128 * 1024, 1024 * 1024],
  useBinary: ['true', 'false'],
  roundtrips: [5000, 1000, 100],
};

const bench = common.createBenchmark(main, configs);

function createFrame(data, opcode) {
  let infoLength = 2;
  let payloadLength = data.length;

  if (payloadLength >= 65536) {
    infoLength += 8;
    payloadLength = 127;
  } else if (payloadLength > 125) {
    infoLength += 2;
    payloadLength = 126;
  }

  const info = Buffer.alloc(infoLength);

  info[0] = opcode | 0x80;
  info[1] = payloadLength;

  if (payloadLength === 126) {
    info.writeUInt16BE(data.length, 2);
  } else if (payloadLength === 127) {
    info[2] = info[3] = 0;
    info.writeUIntBE(data.length, 4, 6);
  }

  return Buffer.concat([info, data]);
}

function main(conf) {
  const frame = createFrame(Buffer.alloc(conf.size).fill('.'), conf.useBinary === 'true' ? 2 : 1);
  const server = http.createServer();
  server.on('upgrade', (req, socket) => {
    const key = crypto
      .createHash('sha1')
      .update(req.headers['sec-websocket-key'] + GUID)
      .digest('base64');

    let bytesReceived = 0;
    let roundtrip = 0;

    socket.on('data', function onData(chunk) {
      bytesReceived += chunk.length;

      if (bytesReceived === frame.length + 4) { // +4 for the mask.
        // Message completely received.
        bytesReceived = 0;

        if (++roundtrip === conf.roundtrips) {
          socket.removeListener('data', onData);
          socket.resume();
          socket.end();
          server.close();

          bench.end(conf.roundtrips);
        } else {
          socket.write(frame);
        }
      }
    });

    socket.write(
      [
        'HTTP/1.1 101 Switching Protocols',
        'Upgrade: websocket',
        'Connection: Upgrade',
        `Sec-WebSocket-Accept: ${key}`,
        '\r\n',
      ].join('\r\n'),
    );

    socket.write(frame);
  });

  server.listen(0, () => {
    const ws = new WebSocket(`ws://localhost:${server.address().port}`);
    ws.addEventListener('open', () => {
      bench.start();
    });

    ws.addEventListener('message', (event) => {
      ws.send(event.data);
    });
  });
}
