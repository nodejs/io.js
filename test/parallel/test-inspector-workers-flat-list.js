'use strict';
const common = require('../common');

common.skipIfInspectorDisabled();

const { Worker, isMainThread, parentPort, workerData } =
  require('worker_threads');

if (!isMainThread || workerData !== 'launched by test') {
  common.skip('This test only works on a main thread');
}

const { Session } = require('inspector');

const MAX_DEPTH = 3;

let rootWorker = null;

const runTest = common.mustCall(function() {
  let reportedWorkersCount = 0;
  const session = new Session();
  session.connect();
  session.on('NodeWorker.attachedToWorker', common.mustCall(
    ({ params: { workerInfo } }) => {
      console.log(`Worker ${workerInfo.title} was reported`);
      if (++reportedWorkersCount === MAX_DEPTH) {
        rootWorker.postMessage({ done: true });
      }
    }, MAX_DEPTH));
  session.post('NodeWorker.enable', { waitForDebuggerOnStart: false });
});

function processMessage({ child }) {
  console.log(`Worker ${child} is running`);
  if (child === MAX_DEPTH) {
    runTest();
  }
}

function workerCallback(message) {
  parentPort.postMessage(message);
}

function startWorker(depth, messageCallback) {
  const worker = new Worker(__filename, { workerData: 'launched by test' });
  worker.on('message', messageCallback);
  worker.postMessage({ depth });
  return worker;
}

function runMainThread() {
  rootWorker = startWorker(1, processMessage);
}

function runChildWorkerThread() {
  let worker = null;
  parentPort.on('message', ({ child, depth, done }) => {
    if (done) {
      if (worker) {
        worker.postMessage({ done: true });
      }
      parentPort.close();
    } else if (depth) {
      parentPort.postMessage({ child: depth });
      if (depth < MAX_DEPTH) {
        worker = startWorker(depth + 1, workerCallback);
      }
    } else if (child) {
      parentPort.postMessage({ child });
    }
  });
}

if (isMainThread) {
  runMainThread();
} else {
  runChildWorkerThread();
}
