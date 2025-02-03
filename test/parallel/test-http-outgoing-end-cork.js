'use strict';
const common = require('../common');
const assert = require('assert');
const http = require('http');

const REQ_TIMEOUT = common.platformTimeout(500); // Set max ms of request time before abort

// Set total allowed test timeout to avoid infinite loop
// that will hang test suite
const TOTAL_TEST_TIMEOUT = common.platformTimeout(1000);

// Placeholder for sockets handled, to make sure that we
// will reach a socket re-use case.
const handledSockets = new Set();

let metReusedSocket = false; // Flag for request loop termination.

const doubleEndResponse = (res) => {
  // First end the request while sending some normal data
  res.end('regular end of request', 'utf8', common.mustCall());
  // Make sure the response socket is uncorked after first call of end
  assert.strictEqual(res.writableCorked, 0);
  res.end(); // Double end the response to prep for next socket re-use.
};

const sendDrainNeedingData = (res) => {
  // Send data to socket more than the high watermark so that
  // it definitely needs drain
  const highWaterMark = res.socket.writableHighWaterMark;
  const bufferToSend = Buffer.alloc(highWaterMark + 100);
  const ret = res.write(bufferToSend); // Write the request data.
  // Make sure that we had back pressure on response stream.
  assert.strictEqual(ret, false);
  res.once('drain', () => res.end()); // End on drain.
};

const server = http.createServer((req, res) => {
  const { socket: responseSocket } = res;
  if (handledSockets.has(responseSocket)) { // re-used socket, send big data!
    metReusedSocket = true; // stop request loop
    console.debug('FOUND REUSED SOCKET!');
    sendDrainNeedingData(res);
  } else { // not used again
    // add to make sure we recognise it when we meet socket again
    handledSockets.add(responseSocket);
    doubleEndResponse(res);
  }
});

server.listen(0); // Start the server on a random port.

const sendRequest = (agent) => new Promise((resolve, reject) => {
  const timeout = setTimeout(common.mustNotCall(() => {
    reject(new Error('Request timed out'));
  }), REQ_TIMEOUT);
  http.get({
    port: server.address().port,
    path: '/',
    agent
  }, common.mustCall((res) => {
    const resData = [];
    res.on('data', (data) => resData.push(data));
    res.on('end', common.mustCall(() => {
      const totalData = resData.reduce((total, elem) => total + elem.length, 0);
      clearTimeout(timeout); // Cancel rejection timeout.
      resolve(totalData); // fulfill promise
    }));
  }));
});

server.once('listening', async () => {
  const testTimeout = setTimeout(common.mustNotCall(() => {
    console.error('Test running for a while but could not met re-used socket');
    process.exit(1);
  }), TOTAL_TEST_TIMEOUT);
  // Explicitly start agent to force socket reuse.
  const agent = new http.Agent({ keepAlive: true });
  // Start the request loop
  let reqNo = 0;
  while (!metReusedSocket) {
    try {
      console.log(`Sending req no ${++reqNo}`);
      const totalData = await sendRequest(agent);
      console.log(`${totalData} bytes were received for request ${reqNo}`);
    } catch (err) {
      console.error(err);
      process.exit(1);
    }
  }
  // Successfully tested conditions and ended loop
  clearTimeout(testTimeout);
  console.log('Closing server');
  agent.destroy();
  server.close();
});
