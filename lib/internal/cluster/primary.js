'use strict';

const {
  ArrayPrototypePush,
  ArrayPrototypeSlice,
  ArrayPrototypeSome,
  ObjectKeys,
  ObjectValues,
  SafeMap,
  StringPrototypeStartsWith,
} = primordials;
const {
  codes: {
    ERR_SOCKET_BAD_PORT,
  },
} = require('internal/errors');

const assert = require('internal/assert');
const { fork } = require('child_process');
const path = require('path');
const EventEmitter = require('events');
const RoundRobinHandle = require('internal/cluster/round_robin_handle');
const SharedHandle = require('internal/cluster/shared_handle');
const Worker = require('internal/cluster/worker');
const { getInspectPort, isUsingInspector } = require('internal/util/inspector');
const { internal, sendHelper } = require('internal/cluster/utils');
const cluster = new EventEmitter();
const intercom = new EventEmitter();
const SCHED_NONE = 1;
const SCHED_RR = 2;

module.exports = cluster;

const handles = new SafeMap();
cluster.isWorker = false;
cluster.isMaster = true; // Deprecated alias. Must be same as isPrimary.
cluster.isPrimary = true;
cluster.Worker = Worker;
cluster.workers = {};
cluster.settings = {};
cluster.SCHED_NONE = SCHED_NONE;  // Leave it to the operating system.
cluster.SCHED_RR = SCHED_RR;      // Primary distributes connections.

let ids = 0;
let initialized = false;

// XXX(bnoordhuis) Fold cluster.schedulingPolicy into cluster.settings?
let schedulingPolicy = process.env.NODE_CLUSTER_SCHED_POLICY;
if (schedulingPolicy === 'rr')
  schedulingPolicy = SCHED_RR;
else if (schedulingPolicy === 'none')
  schedulingPolicy = SCHED_NONE;
else if (process.platform === 'win32') {
  // Round-robin doesn't perform well on
  // Windows due to the way IOCP is wired up.
  schedulingPolicy = SCHED_NONE;
} else
  schedulingPolicy = SCHED_RR;

cluster.schedulingPolicy = schedulingPolicy;

cluster.setupPrimary = function(options) {
  const settings = {
    args: ArrayPrototypeSlice(process.argv, 2),
    exec: process.argv[1],
    execArgv: process.execArgv,
    silent: false,
    ...cluster.settings,
    ...options,
  };

  // Tell V8 to write profile data for each process to a separate file.
  // Without --logfile=v8-%p.log, everything ends up in a single, unusable
  // file. (Unusable because what V8 logs are memory addresses and each
  // process has its own memory mappings.)
  if (ArrayPrototypeSome(settings.execArgv,
                         (s) => StringPrototypeStartsWith(s, '--prof')) &&
      !ArrayPrototypeSome(settings.execArgv,
                          (s) => StringPrototypeStartsWith(s, '--logfile='))) {
    settings.execArgv = [...settings.execArgv, '--logfile=v8-%p.log'];
  }

  cluster.settings = settings;

  if (initialized === true)
    return process.nextTick(setupSettingsNT, settings);

  initialized = true;
  schedulingPolicy = cluster.schedulingPolicy;  // Freeze policy.
  assert(schedulingPolicy === SCHED_NONE || schedulingPolicy === SCHED_RR,
         `Bad cluster.schedulingPolicy: ${schedulingPolicy}`);

  process.nextTick(setupSettingsNT, settings);

  process.on('internalMessage', (message) => {
    if (message.cmd !== 'NODE_DEBUG_ENABLED')
      return;

    for (const worker of ObjectValues(cluster.workers)) {
      if (worker.state === 'online' || worker.state === 'listening') {
        process._debugProcess(worker.process.pid);
      } else {
        worker.once('online', function() {
          process._debugProcess(this.process.pid);
        });
      }
    }
  });
};

// Deprecated alias must be same as setupPrimary
cluster.setupMaster = cluster.setupPrimary;

function setupSettingsNT(settings) {
  cluster.emit('setup', settings);
}

function createWorkerProcess(id, env) {
  const workerEnv = { ...process.env, ...env, NODE_UNIQUE_ID: `${id}` };
  const execArgv = [...cluster.settings.execArgv];

  if (cluster.settings.inspectPort === null) {
    throw new ERR_SOCKET_BAD_PORT('Port', null, true);
  }
  if (isUsingInspector(cluster.settings.execArgv)) {
    ArrayPrototypePush(execArgv, `--inspect-port=${getInspectPort(cluster.settings.inspectPort)}`);
  }

  return fork(cluster.settings.exec, cluster.settings.args, {
    cwd: cluster.settings.cwd,
    env: workerEnv,
    serialization: cluster.settings.serialization,
    silent: cluster.settings.silent,
    windowsHide: cluster.settings.windowsHide,
    execArgv: execArgv,
    stdio: cluster.settings.stdio,
    gid: cluster.settings.gid,
    uid: cluster.settings.uid,
  });
}

function removeWorker(worker) {
  assert(worker);
  delete cluster.workers[worker.id];

  if (ObjectKeys(cluster.workers).length === 0) {
    assert(handles.size === 0, 'Resource leak detected.');
    intercom.emit('disconnect');
  }
}

function removeHandlesForWorker(worker) {
  assert(worker);

  for (const { 0: key, 1: handle } of handles) {
    if (handle.remove(worker))
      handles.delete(key);
  }
}

cluster.fork = function(env) {
  cluster.setupPrimary();
  const id = ++ids;
  const workerProcess = createWorkerProcess(id, env);
  const worker = new Worker({
    id: id,
    process: workerProcess,
  });

  worker.on('message', function(message, handle) {
    cluster.emit('message', this, message, handle);
  });

  worker.process.once('exit', (exitCode, signalCode) => {
    /*
     * Remove the worker from the workers list only
     * if it has disconnected, otherwise we might
     * still want to access it.
     */
    if (!worker.isConnected()) {
      removeHandlesForWorker(worker);
      removeWorker(worker);
    }

    worker.exitedAfterDisconnect = !!worker.exitedAfterDisconnect;
    worker.state = 'dead';
    worker.emit('exit', exitCode, signalCode);
    cluster.emit('exit', worker, exitCode, signalCode);
  });

  worker.process.once('disconnect', () => {
    /*
     * Now is a good time to remove the handles
     * associated with this worker because it is
     * not connected to the primary anymore.
     */
    removeHandlesForWorker(worker);

    /*
     * Remove the worker from the workers list only
     * if its process has exited. Otherwise, we might
     * still want to access it.
     */
    if (worker.isDead())
      removeWorker(worker);

    worker.exitedAfterDisconnect = !!worker.exitedAfterDisconnect;
    worker.state = 'disconnected';
    worker.emit('disconnect');
    cluster.emit('disconnect', worker);
  });

  worker.process.on('internalMessage', internal(worker, onmessage));
  process.nextTick(emitForkNT, worker);
  cluster.workers[worker.id] = worker;
  return worker;
};

function emitForkNT(worker) {
  cluster.emit('fork', worker);
}

cluster.disconnect = function(cb) {
  const workers = ObjectValues(cluster.workers);

  if (workers.length === 0) {
    process.nextTick(() => intercom.emit('disconnect'));
  } else {
    for (const worker of workers) {
      if (worker.isConnected()) {
        worker.disconnect();
      }
    }
  }

  if (typeof cb === 'function')
    intercom.once('disconnect', cb);
};

const methodMessageMapping = {
  close,
  exitedAfterDisconnect,
  listening,
  online,
  queryServer,
};

function onmessage(message, handle) {
  const worker = this;

  const fn = methodMessageMapping[message.act];

  if (typeof fn === 'function')
    fn(worker, message);
}

function online(worker) {
  worker.state = 'online';
  worker.emit('online');
  cluster.emit('online', worker);
}

function exitedAfterDisconnect(worker, message) {
  worker.exitedAfterDisconnect = true;
  send(worker, { ack: message.seq });
}

function queryServer(worker, message) {
  // Stop processing if worker already disconnecting
  if (worker.exitedAfterDisconnect)
    return;

  const key = `${message.address}:${message.port}:${message.addressType}:` +
              `${message.fd}:${message.index}`;
  let handle = handles.get(key);

  if (handle === undefined) {
    let address = message.address;

    // Find shortest path for unix sockets because of the ~100 byte limit
    if (message.port < 0 && typeof address === 'string' &&
        process.platform !== 'win32') {

      address = path.relative(process.cwd(), address);

      if (message.address.length < address.length)
        address = message.address;
    }

    // UDP is exempt from round-robin connection balancing for what should
    // be obvious reasons: it's connectionless. There is nothing to send to
    // the workers except raw datagrams and that's pointless.
    if (schedulingPolicy !== SCHED_RR ||
        message.addressType === 'udp4' ||
        message.addressType === 'udp6') {
      handle = new SharedHandle(key, address, message);
    } else {
      handle = new RoundRobinHandle(key, address, message);
    }

    handles.set(key, handle);
  }

  handle.data ||= message.data;

  // Set custom server data
  handle.add(worker, (errno, reply, handle) => {
    const { data } = handles.get(key);

    if (errno)
      handles.delete(key);  // Gives other workers a chance to retry.

    send(worker, {
      errno,
      key,
      ack: message.seq,
      data,
      ...reply,
    }, handle);
  });
}

function listening(worker, message) {
  const info = {
    addressType: message.addressType,
    address: message.address,
    port: message.port,
    fd: message.fd,
  };

  worker.state = 'listening';
  worker.emit('listening', info);
  cluster.emit('listening', worker, info);
}

// Server in worker is closing, remove from list. The handle may have been
// removed by a prior call to removeHandlesForWorker() so guard against that.
function close(worker, message) {
  const key = message.key;
  const handle = handles.get(key);

  if (handle && handle.remove(worker))
    handles.delete(key);
}

function send(worker, message, handle, cb) {
  return sendHelper(worker.process, message, handle, cb);
}

// Extend generic Worker with methods specific to the primary process.
Worker.prototype.disconnect = function() {
  this.exitedAfterDisconnect = true;
  send(this, { act: 'disconnect' });
  removeHandlesForWorker(this);
  removeWorker(this);
  return this;
};

Worker.prototype.destroy = function(signo) {
  const signal = signo || 'SIGTERM';
  this.process.kill(signal);
};
