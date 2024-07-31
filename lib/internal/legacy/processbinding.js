'use strict';
const {
  ArrayPrototypeFilter,
  ArrayPrototypeIncludes,
  ObjectEntries,
  ObjectFromEntries,
  SafeArrayIterator,
} = primordials;
const { types } = require('util');

module.exports = {
  util() {
    return ObjectFromEntries(new SafeArrayIterator(ArrayPrototypeFilter(
      ObjectEntries(types),
      ({ 0: key }) => {
        return ArrayPrototypeIncludes([
          'isArrayBuffer',
          'isArrayBufferView',
          'isAsyncFunction',
          'isDataView',
          'isDate',
          'isExternal',
          'isMap',
          'isMapIterator',
          'isNativeError',
          'isPromise',
          'isRegExp',
          'isSet',
          'isSetIterator',
          'isTypedArray',
          'isUint8Array',
          'isAnyArrayBuffer',
        ], key);
      })));
  },
  natives() {
    const { natives: result, configs } = internalBinding('builtins');
    // Legacy feature: process.binding('natives').config contains stringified
    // config.gypi. We do not use this object internally so it's fine to mutate
    // it.
    result.configs = configs;
    return result;
  },
};
