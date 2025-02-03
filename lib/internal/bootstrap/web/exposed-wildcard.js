'use strict';

/**
 * This file exposes web interfaces that is defined with the WebIDL
 * [Exposed=*] extended attribute.
 * See more details at https://webidl.spec.whatwg.org/#Exposed.
 */

const {
  globalThis,
} = primordials;

const {
  exposeInterface,
  lazyDOMExceptionClass,
  exposeLazyInterfaces,
  exposeGetterAndSetter,
  exposeNamespace,
} = require('internal/util');
const config = internalBinding('config');

// https://console.spec.whatwg.org/#console-namespace
exposeNamespace(globalThis, 'console',
                createGlobalConsole());

const { URL, URLSearchParams } = require('internal/url');
// https://url.spec.whatwg.org/#url
exposeInterface(globalThis, 'URL', URL);
// https://url.spec.whatwg.org/#urlsearchparams
exposeInterface(globalThis, 'URLSearchParams', URLSearchParams);
exposeGetterAndSetter(globalThis,
                      'DOMException',
                      () => {
                        const DOMException = lazyDOMExceptionClass();
                        exposeInterface(globalThis, 'DOMException', DOMException);
                        return DOMException;
                      },
                      (value) => {
                        exposeInterface(globalThis, 'DOMException', value);
                      });

// https://dom.spec.whatwg.org/#interface-abortcontroller
// Lazy ones.
exposeLazyInterfaces(globalThis, 'internal/abort_controller', [
  'AbortController', 'AbortSignal',
]);
// https://dom.spec.whatwg.org/#interface-eventtarget
const {
  EventTarget, Event,
} = require('internal/event_target');
exposeInterface(globalThis, 'Event', Event);
exposeInterface(globalThis, 'EventTarget', EventTarget);
exposeLazyInterfaces(globalThis, 'internal/event_target', ['CustomEvent']);

// https://encoding.spec.whatwg.org/#textencoder
// https://encoding.spec.whatwg.org/#textdecoder
exposeLazyInterfaces(globalThis,
                     'internal/encoding',
                     ['TextEncoder', 'TextDecoder']);

function createGlobalConsole() {
  const consoleFromNode =
    require('internal/console/global');
  if (config.hasInspector) {
    const inspector = require('internal/util/inspector');
    // TODO(joyeecheung): postpone this until the first time inspector
    // is activated.
    inspector.wrapConsole(consoleFromNode);
    const { setConsoleExtensionInstaller } = internalBinding('inspector');
    // Setup inspector command line API.
    setConsoleExtensionInstaller(inspector.installConsoleExtensions);
  }
  return consoleFromNode;
}

// Web Streams API
exposeLazyInterfaces(
  globalThis,
  'internal/webstreams/transformstream',
  ['TransformStream', 'TransformStreamDefaultController']);

exposeLazyInterfaces(
  globalThis,
  'internal/webstreams/writablestream',
  ['WritableStream', 'WritableStreamDefaultController', 'WritableStreamDefaultWriter']);

exposeLazyInterfaces(
  globalThis,
  'internal/webstreams/readablestream',
  [
    'ReadableStream', 'ReadableStreamDefaultReader',
    'ReadableStreamBYOBReader', 'ReadableStreamBYOBRequest',
    'ReadableByteStreamController', 'ReadableStreamDefaultController',
  ]);

exposeLazyInterfaces(
  globalThis,
  'internal/webstreams/queuingstrategies',
  [
    'ByteLengthQueuingStrategy', 'CountQueuingStrategy',
  ]);

exposeLazyInterfaces(
  globalThis,
  'internal/webstreams/encoding',
  [
    'TextEncoderStream', 'TextDecoderStream',
  ]);

exposeLazyInterfaces(
  globalThis,
  'internal/webstreams/compression',
  [
    'CompressionStream', 'DecompressionStream',
  ]);
