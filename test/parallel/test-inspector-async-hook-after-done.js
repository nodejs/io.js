'use strict';

const common = require('../common');

common.skipIfInspectorDisabled();

const { isMainThread } = require('worker_threads');

if (!isMainThread) {
  common.skip('This test only works on a main thread');
}

const assert = require('assert');
const { Worker } = require('worker_threads');
const { Session } = require('inspector');

const session = new Session();

let done = false;

function onAttachToWorker({ params: { sessionId } }) {
  let id = 1;
  function postToWorkerInspector(method, params) {
    session.post('NodeWorker.sendMessageToWorker', {
      sessionId,
      message: JSON.stringify({ id: id++, method, params })
    }, () => console.log(`Message ${method} received the response`));
  }

  // Wait for the notification
  function onMessageReceived({ params: { message } }) {
    if (!message ||
      JSON.parse(message).method !== 'NodeRuntime.waitingForDisconnect') {
      session.once('NodeWorker.receivedMessageFromWorker', onMessageReceived);
      return;
    }
    // Force a call to node::inspector::Agent::ToggleAsyncHook by changing the
    // async call stack depth
    postToWorkerInspector('Debugger.setAsyncCallStackDepth', { maxDepth: 1 });
    // This is were the original crash happened
    session.post('NodeWorker.detach', { sessionId }, () => {
      done = true;
    });
  }

  onMessageReceived({ params: { message: null } });
  // Enable the debugger, otherwise setAsyncCallStackDepth does nothing
  postToWorkerInspector('Debugger.enable');
  // Start waiting for disconnect notification
  postToWorkerInspector('NodeRuntime.notifyWhenWaitingForDisconnect',
                        { enabled: true });
  // start worker
  postToWorkerInspector('Runtime.runIfWaitingForDebugger');
}

session.connect();

session.on('NodeWorker.attachedToWorker', common.mustCall(onAttachToWorker));

session.post('NodeWorker.enable', { waitForDebuggerOnStart: true }, () => {
  new Worker('console.log("Worker is done")', { eval: true })
    .once('exit', () => {
      setTimeout(() => {
        assert.strictEqual(done, true);
        console.log('Test is done');
      }, 0);
    });
});
