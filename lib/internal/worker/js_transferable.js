'use strict';
const {
  Error,
  StringPrototypeSplit,
} = primordials;
const {
  codes: {
    ERR_MISSING_ARGS,
  },
} = require('internal/errors');
const webidl = require('internal/webidl');
const {
  messaging_deserialize_symbol,
  messaging_transfer_symbol,
  messaging_clone_symbol,
  messaging_transfer_list_symbol,
} = internalBinding('symbols');
const {
  setDeserializerCreateObjectFunction,
  structuredClone: nativeStructuredClone,
} = internalBinding('messaging');
const {
  privateSymbols: {
    transfer_mode_private_symbol,
  },
  constants: {
    kDisallowCloneAndTransfer,
    kTransferable,
    kCloneable,
  },
} = internalBinding('util');

function setup() {
  // Register the handler that will be used when deserializing JS-based objects
  // from .postMessage() calls. The format of `deserializeInfo` is generally
  // 'module:Constructor', e.g. 'internal/fs/promises:FileHandle'.
  setDeserializerCreateObjectFunction((deserializeInfo) => {
    const { 0: module, 1: ctor } = StringPrototypeSplit(deserializeInfo, ':');
    const Ctor = require(module)[ctor];
    if (typeof Ctor !== 'function' ||
        typeof Ctor.prototype[messaging_deserialize_symbol] !== 'function') {
      // Not one of the official errors because one should not be able to get
      // here without messing with Node.js internals.
      // eslint-disable-next-line no-restricted-syntax
      throw new Error(`Unknown deserialize spec ${deserializeInfo}`);
    }

    return new Ctor();
  });
}

/**
 * Mark an object as being transferable or customized cloneable in
 * `.postMessage()`.
 * This should only applied to host objects like Web API interfaces, Node.js'
 * built-in objects.
 * Objects marked as cloneable and transferable should implement the method
 * `@@kClone` and `@@kTransfer` respectively. Method `@@kDeserialize` is
 * required to deserialize the data to a new instance.
 *
 * Example implementation of a cloneable interface (assuming its located in
 * `internal/my_interface.js`):
 *
 * ```
 * class MyInterface {
 *   constructor(...args) {
 *     markTransferMode(this, true);
 *     this.args = args;
 *   }
 *   [kDeserialize](data) {
 *     this.args = data.args;
 *   }
 *   [kClone]() {
 *     return {
 *        data: { args: this.args },
 *        deserializeInfo: 'internal/my_interface:MyInterface',
 *     }
 *   }
 * }
 *
 * module.exports = {
 *   MyInterface,
 * };
 * ```
 * @param {object} obj Host objects that can be either cloned or transferred.
 * @param {boolean} [cloneable] if the object can be cloned and `@@kClone` is
 *                              implemented.
 * @param {boolean} [transferable] if the object can be transferred and
 *                                 `@@kTransfer` is implemented.
 */
function markTransferMode(obj, cloneable = false, transferable = false) {
  if ((typeof obj !== 'object' && typeof obj !== 'function') || obj === null)
    return;  // This object is a primitive and therefore already untransferable.
  let mode = kDisallowCloneAndTransfer;
  if (cloneable) mode |= kCloneable;
  if (transferable) mode |= kTransferable;
  obj[transfer_mode_private_symbol] = mode;
}


webidl.converters.StructuredSerializeOptions = webidl
  .createDictionaryConverter(
    [
      {
        key: 'transfer',
        converter: webidl.converters['sequence<object>'],
        defaultValue: () => [],
      },
    ],
  );

function structuredClone(value, options) {
  if (arguments.length === 0) {
    throw new ERR_MISSING_ARGS('The value argument must be specified');
  }

  const idlOptions = webidl.converters.StructuredSerializeOptions(
    options,
    {
      __proto__: null,
      prefix: "Failed to execute 'structuredClone'",
      context: 'Options',
    },
  );

  const serializedData = nativeStructuredClone(value, idlOptions);
  return serializedData;
}

module.exports = {
  markTransferMode,
  setup,
  structuredClone,
  kClone: messaging_clone_symbol,
  kDeserialize: messaging_deserialize_symbol,
  kTransfer: messaging_transfer_symbol,
  kTransferList: messaging_transfer_list_symbol,
};
