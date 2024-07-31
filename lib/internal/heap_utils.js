'use strict';
const {
  ArrayPrototypeMap,
  Symbol,
  Uint8Array,
} = primordials;
const {
  kUpdateTimer,
  onStreamRead,
} = require('internal/stream_base_commons');
const { owner_symbol } = require('internal/async_hooks').symbols;
const { Readable } = require('stream');
const {
  validateObject,
  validateBoolean,
  validateFunction,
} = require('internal/validators');
const {
  codes: {
    ERR_INVALID_ARG_VALUE,
  },
} = require('internal/errors');
const { kEmptyObject, emitExperimentalWarning } = require('internal/util');
const {
  queryObjects: _queryObjects,
} = internalBinding('internal_only_v8');
const {
  inspect,
} = require('internal/util/inspect');
const kHandle = Symbol('kHandle');

function getHeapSnapshotOptions(options = kEmptyObject) {
  validateObject(options, 'options');
  const {
    exposeInternals = false,
    exposeNumericValues = false,
  } = options;
  validateBoolean(exposeInternals, 'options.exposeInternals');
  validateBoolean(exposeNumericValues, 'options.exposeNumericValues');
  return new Uint8Array([+exposeInternals, +exposeNumericValues]);
}

class HeapSnapshotStream extends Readable {
  constructor(handle) {
    super({ autoDestroy: true });
    this[kHandle] = handle;
    handle[owner_symbol] = this;
    handle.onread = onStreamRead;
  }

  _read() {
    if (this[kHandle])
      this[kHandle].readStart();
  }

  _destroy() {
    // Release the references on the handle so that
    // it can be garbage collected.
    this[kHandle][owner_symbol] = undefined;
    this[kHandle] = undefined;
  }

  [kUpdateTimer]() {
    // Does nothing
  }
}

const inspectOptions = {
  __proto__: null,
  depth: 0,
};
function queryObjects(ctor, options = kEmptyObject) {
  validateFunction(ctor, 'constructor');
  if (options !== kEmptyObject) {
    validateObject(options, 'options');
  }
  const format = options.format || 'count';
  if (format !== 'count' && format !== 'summary') {
    throw new ERR_INVALID_ARG_VALUE('options.format', format);
  }
  emitExperimentalWarning('v8.queryObjects()');
  // Matching the console API behavior - just access the .prototype.
  const objects = _queryObjects(ctor.prototype);
  if (format === 'count') {
    return objects.length;
  }
  // options.format is 'summary'.
  return ArrayPrototypeMap(objects, (object) => inspect(object, inspectOptions));
}

module.exports = {
  getHeapSnapshotOptions,
  HeapSnapshotStream,
  queryObjects,
};
