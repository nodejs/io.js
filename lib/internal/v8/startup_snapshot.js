'use strict';

const {
  validateFunction,
} = require('internal/validators');
const {
  codes: {
    ERR_DUPLICATE_STARTUP_SNAPSHOT_MAIN_FUNCTION,
    ERR_NOT_BUILDING_SNAPSHOT,
    ERR_NOT_SUPPORTED_IN_SNAPSHOT,
  },
} = require('internal/errors');

const {
  setSerializeCallback,
  setDeserializeCallback,
  setDeserializeMainFunction: _setDeserializeMainFunction,
  isBuildingSnapshotBuffer,
} = internalBinding('mksnapshot');

function isBuildingSnapshot() {
  return isBuildingSnapshotBuffer[0];
}

function throwIfNotBuildingSnapshot() {
  if (!isBuildingSnapshot()) {
    throw new ERR_NOT_BUILDING_SNAPSHOT();
  }
}

function throwIfBuildingSnapshot(reason) {
  if (isBuildingSnapshot()) {
    throw new ERR_NOT_SUPPORTED_IN_SNAPSHOT(reason);
  }
}

const deserializeCallbacks = [];
let deserializeCallbackIsSet = false;
function runDeserializeCallbacks() {
  while (deserializeCallbacks.length > 0) {
    const { 0: callback, 1: data } = deserializeCallbacks.shift();
    callback(data);
  }
}

function addDeserializeCallback(callback, data) {
  throwIfNotBuildingSnapshot();
  validateFunction(callback, 'callback');
  if (!deserializeCallbackIsSet) {
    // TODO(joyeecheung): when the main function handling is done in JS,
    // the deserialize callbacks can always be invoked. For now only
    // store it in C++ when it's actually used to avoid unnecessary
    // C++ -> JS costs.
    setDeserializeCallback(runDeserializeCallbacks);
    deserializeCallbackIsSet = true;
  }
  deserializeCallbacks.push([callback, data]);
}

const serializeCallbacks = [];
const afterUserSerializeCallbacks = [];  // Callbacks to run after user callbacks
function runSerializeCallbacks() {
  while (serializeCallbacks.length > 0) {
    const { 0: callback, 1: data } = serializeCallbacks.shift();
    callback(data);
  }
  while (afterUserSerializeCallbacks.length > 0) {
    const { 0: callback, 1: data } = afterUserSerializeCallbacks.shift();
    callback(data);
  }
}

function addSerializeCallback(callback, data) {
  throwIfNotBuildingSnapshot();
  validateFunction(callback, 'callback');
  serializeCallbacks.push([callback, data]);
}

function addAfterUserSerializeCallback(callback, data) {
  afterUserSerializeCallbacks.push([callback, data]);
}

function initializeCallbacks() {
  // Only run the serialize callbacks in snapshot building mode, otherwise
  // they throw.
  if (isBuildingSnapshot()) {
    setSerializeCallback(runSerializeCallbacks);
  }
}

let deserializeMainIsSet = false;
function setDeserializeMainFunction(callback, data) {
  throwIfNotBuildingSnapshot();
  // TODO(joyeecheung): In lib/internal/bootstrap/node.js, create a default
  // main function to run the lib/internal/main scripts and make sure that
  // the main function set in the snapshot building process takes precedence.
  validateFunction(callback, 'callback');
  if (deserializeMainIsSet) {
    throw new ERR_DUPLICATE_STARTUP_SNAPSHOT_MAIN_FUNCTION();
  }
  deserializeMainIsSet = true;

  _setDeserializeMainFunction(function deserializeMain() {
    const {
      prepareMainThreadExecution,
      markBootstrapComplete,
    } = require('internal/process/pre_execution');

    // This should be in sync with run_main_module.js until we make that
    // a built-in main function.
    // TODO(joyeecheung): make a copy of argv[0] and insert it as argv[1].
    prepareMainThreadExecution(false);
    markBootstrapComplete();
    callback(data);
  });
}

module.exports = {
  initializeCallbacks,
  runDeserializeCallbacks,
  throwIfBuildingSnapshot,
  // Exposed to require('v8').startupSnapshot
  namespace: {
    addDeserializeCallback,
    addSerializeCallback,
    setDeserializeMainFunction,
    isBuildingSnapshot,
  },
  addAfterUserSerializeCallback,
};
