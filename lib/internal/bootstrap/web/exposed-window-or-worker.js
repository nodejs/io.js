'use strict';

/**
 * This file exposes web interfaces that is defined with the WebIDL
 * Exposed=Window + Exposed=(Window,Worker) extended attribute or exposed in
 * WindowOrWorkerGlobalScope mixin.
 * See more details at https://webidl.spec.whatwg.org/#Exposed and
 * https://html.spec.whatwg.org/multipage/webappapis.html#windoworworkerglobalscope.
 */

const {
  ObjectDefineProperty,
  ObjectGetOwnPropertyDescriptor,
  globalThis,
} = primordials;

const {
  defineOperation,
  defineLazyProperties,
  defineReplaceableLazyAttribute,
  exposeLazyInterfaces,
} = require('internal/util');

const {
  ERR_INVALID_THIS,
  ERR_NO_CRYPTO,
} = require('internal/errors').codes;

// https://html.spec.whatwg.org/multipage/webappapis.html#windoworworkerglobalscope
const timers = require('timers');
defineOperation(globalThis, 'clearInterval', timers.clearInterval);
defineOperation(globalThis, 'clearTimeout', timers.clearTimeout);
defineOperation(globalThis, 'setInterval', timers.setInterval);
defineOperation(globalThis, 'setTimeout', timers.setTimeout);

const {
  queueMicrotask,
} = require('internal/process/task_queues');
defineOperation(globalThis, 'queueMicrotask', queueMicrotask);

defineLazyProperties(
  globalThis,
  'internal/worker/js_transferable',
  ['structuredClone'],
);
defineLazyProperties(globalThis, 'buffer', ['atob', 'btoa']);

// https://html.spec.whatwg.org/multipage/web-messaging.html#broadcasting-to-other-browsing-contexts
exposeLazyInterfaces(globalThis, 'internal/worker/io', ['BroadcastChannel']);
exposeLazyInterfaces(globalThis, 'internal/worker/io', [
  'MessageChannel', 'MessagePort',
]);
// https://www.w3.org/TR/FileAPI/#dfn-Blob
exposeLazyInterfaces(globalThis, 'internal/blob', ['Blob']);
// https://www.w3.org/TR/FileAPI/#dfn-file
exposeLazyInterfaces(globalThis, 'internal/file', ['File']);
// https://www.w3.org/TR/hr-time-2/#the-performance-attribute
exposeLazyInterfaces(globalThis, 'perf_hooks', [
  'Performance', 'PerformanceEntry', 'PerformanceMark', 'PerformanceMeasure',
  'PerformanceObserver', 'PerformanceObserverEntryList', 'PerformanceResourceTiming',
]);

defineReplaceableLazyAttribute(globalThis, 'perf_hooks', ['performance']);

// https://w3c.github.io/FileAPI/#creating-revoking
const { installObjectURLMethods } = require('internal/url');
installObjectURLMethods();

let fetchImpl;
// https://fetch.spec.whatwg.org/#fetch-method
ObjectDefineProperty(globalThis, 'fetch', {
  __proto__: null,
  configurable: true,
  enumerable: true,
  writable: true,
  value: function fetch(input, init = undefined) { // eslint-disable-line func-name-matching
    if (!fetchImpl) { // Implement lazy loading of undici module for fetch function
      const undiciModule = require('internal/deps/undici/undici');
      fetchImpl = undiciModule.fetch;
    }
    return fetchImpl(input, init);
  },
});

// https://xhr.spec.whatwg.org/#interface-formdata
// https://fetch.spec.whatwg.org/#headers-class
// https://fetch.spec.whatwg.org/#request-class
// https://fetch.spec.whatwg.org/#response-class
exposeLazyInterfaces(globalThis, 'internal/deps/undici/undici', [
  'FormData', 'Headers', 'Request', 'Response', 'MessageEvent', 'CloseEvent',
]);

// https://html.spec.whatwg.org/multipage/server-sent-events.html#server-sent-events.org/
// https://websockets.spec.whatwg.org/
exposeLazyInterfaces(globalThis, 'internal/deps/undici/undici', ['EventSource', 'WebSocket']);

// The WebAssembly Web API which relies on Response.
// https:// webassembly.github.io/spec/web-api/#streaming-modules
internalBinding('wasm_web_api').setImplementation((streamState, source) => {
  require('internal/wasm_web_api').wasmStreamingCallback(streamState, source);
});

// WebCryptoAPI
if (internalBinding('config').hasOpenSSL) {
  defineReplaceableLazyAttribute(
    globalThis,
    'internal/crypto/webcrypto',
    ['crypto'],
    false,
    function cryptoThisCheck() {
      if (this !== globalThis && this != null)
        throw new ERR_INVALID_THIS(
          'nullish or must be the global object');
    },
  );
  exposeLazyInterfaces(
    globalThis, 'internal/crypto/webcrypto',
    ['Crypto', 'CryptoKey', 'SubtleCrypto'],
  );
} else {
  ObjectDefineProperty(globalThis, 'crypto',
                       { __proto__: null, ...ObjectGetOwnPropertyDescriptor({
                         get crypto() {
                           throw new ERR_NO_CRYPTO();
                         },
                       }, 'crypto') });
}
