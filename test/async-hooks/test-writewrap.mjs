import { mustCall } from '../common/index.mjs';
import { strictEqual } from 'assert';
import process from 'process';
import initHooks from './init-hooks.mjs';
import { checkInvocations } from './hook-checks.mjs';
import { createServer, connect } from 'net';

const hooks = initHooks();
hooks.enable();

//
// Creating server and listening on port
//
const server = createServer()
  .on('listening', mustCall(onlistening))
  .on('connection', mustCall(onconnection))
  .listen(0);

strictEqual(hooks.activitiesOfTypes('WRITEWRAP').length, 0);

function onlistening() {
  strictEqual(hooks.activitiesOfTypes('WRITEWRAP').length, 0);
  //
  // Creating client and connecting it to server
  //
  connect(server.address().port)
    .on('connect', mustCall(onconnect));

  strictEqual(hooks.activitiesOfTypes('WRITEWRAP').length, 0);
}

function checkDestroyedWriteWraps(n, stage) {
  const as = hooks.activitiesOfTypes('WRITEWRAP');
  strictEqual(as.length, n,
                     `${as.length} out of ${n} WRITEWRAPs when ${stage}`);

  function checkValidWriteWrap(w) {
    strictEqual(w.type, 'WRITEWRAP');
    strictEqual(typeof w.uid, 'number');
    strictEqual(typeof w.triggerAsyncId, 'number');

    checkInvocations(w, { init: 1 }, `when ${stage}`);
  }
  as.forEach(checkValidWriteWrap);
}

function onconnection(conn) {
  conn.write('hi');  // Let the client know we're ready.
  conn.resume();
  //
  // Server received client connection
  //
  checkDestroyedWriteWraps(0, 'server got connection');
}

function onconnect() {
  //
  // Client connected to server
  //
  checkDestroyedWriteWraps(0, 'client connected');

  this.once('data', mustCall(ondata));
}

function ondata() {
  //
  // Writing data to client socket
  //
  const write = () => {
    let writeFinished = false;
    this.write('f'.repeat(1280000), () => {
      writeFinished = true;
    });
    process.nextTick(() => {
      if (writeFinished) {
        // Synchronous finish, write more data immediately.
        writeFinished = false;
        write();
      } else {
        // Asynchronous write; this is what we are here for.
        onafterwrite(this);
      }
    });
  };
  write();
}

function onafterwrite(self) {
  checkDestroyedWriteWraps(1, 'client destroyed');
  self.end();

  checkDestroyedWriteWraps(1, 'client destroyed');

  //
  // Closing server
  //
  server.close(mustCall(onserverClosed));
  checkDestroyedWriteWraps(1, 'server closing');
}

function onserverClosed() {
  checkDestroyedWriteWraps(1, 'server closed');
}

process.on('exit', onexit);

function onexit() {
  hooks.disable();
  hooks.sanityCheck('WRITEWRAP');
  checkDestroyedWriteWraps(1, 'process exits');
}
