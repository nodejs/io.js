/* eslint-disable @stylistic/js/max-len */

import {
  noRestrictedSyntaxCommonAll,
  noRestrictedSyntaxCommonLib,
} from '../tools/eslint/eslint.config_utils.mjs';

const noRestrictedSyntax = [
  'error',
  ...noRestrictedSyntaxCommonAll,
  ...noRestrictedSyntaxCommonLib,
  {
    selector: "CallExpression[callee.object.name='assert']:not([callee.property.name='ok']):not([callee.property.name='fail']):not([callee.property.name='ifError'])",
    message: 'Only use simple assertions',
  },
  {
    // Forbids usages of `btoa` that are not caught by no-restricted-globals, like:
    // ```
    // const { btoa } = internalBinding('buffer');
    // btoa('...');
    // ```
    selector: "CallExpression[callee.property.name='btoa'], CallExpression[callee.name='btoa']",
    message: "`btoa` supports only latin-1 charset, use Buffer.from(str).toString('base64') instead",
  },
  {
    selector: 'NewExpression[callee.name=/Error$/]:not([callee.name=/^(AssertionError|NghttpError|AbortError|NodeAggregateError)$/])',
    message: "Use an error exported by 'internal/errors' instead.",
  },
  {
    selector: "CallExpression[callee.object.name='Error'][callee.property.name='captureStackTrace']",
    message: "Use 'hideStackFrames' from 'internal/errors' instead.",
  },
  {
    selector: "AssignmentExpression:matches([left.object.name='Error']):matches([left.name='prepareStackTrace'], [left.property.name='prepareStackTrace'])",
    message: "Use 'overrideStackTrace' from 'internal/errors' instead.",
  },
  {
    selector: "ThrowStatement > NewExpression[callee.name=/^ERR_[A-Z_]+$/] > ObjectExpression:first-child:not(:has([key.name='message']):has([key.name='code']):has([key.name='syscall']))",
    message: 'The context passed into the SystemError constructor must include .code, .syscall, and .message properties.',
  },
];

export default [
  {
    files: ['lib/**/*.js'],
    languageOptions: {
      globals: {
        // Parameters passed to internal modules.
        require: 'readonly',
        process: 'readonly',
        exports: 'readonly',
        module: 'readonly',
        internalBinding: 'readonly',
        primordials: 'readonly',
      },
    },
    rules: {
      'prefer-object-spread': 'error',
      'no-buffer-constructor': 'error',
      'no-restricted-syntax': noRestrictedSyntax,
      'no-restricted-globals': [
        'error',
        {
          name: 'AbortController',
          message: "Use `const { AbortController } = require('internal/abort_controller');` instead of the global.",
        },
        {
          name: 'AbortSignal',
          message: "Use `const { AbortSignal } = require('internal/abort_controller');` instead of the global.",
        },
        {
          name: 'Blob',
          message: "Use `const { Blob } = require('buffer');` instead of the global.",
        },
        {
          name: 'BroadcastChannel',
          message: "Use `const { BroadcastChannel } = require('internal/worker/io');` instead of the global.",
        },
        {
          name: 'Buffer',
          message: "Use `const { Buffer } = require('buffer');` instead of the global.",
        },
        {
          name: 'ByteLengthQueuingStrategy',
          message: "Use `const { ByteLengthQueuingStrategy } = require('internal/webstreams/queuingstrategies')` instead of the global.",
        },
        {
          name: 'CloseEvent',
          message: "Use `const { CloseEvent } = require('internal/deps/undici/undici');` instead of the global.",
        },
        {
          name: 'CompressionStream',
          message: "Use `const { CompressionStream } = require('internal/webstreams/compression')` instead of the global.",
        },
        {
          name: 'CountQueuingStrategy',
          message: "Use `const { CountQueuingStrategy } = require('internal/webstreams/queuingstrategies')` instead of the global.",
        },
        {
          name: 'CustomEvent',
          message: "Use `const { CustomEvent } = require('internal/event_target');` instead of the global.",
        },
        {
          name: 'DecompressionStream',
          message: "Use `const { DecompressionStream } = require('internal/webstreams/compression')` instead of the global.",
        },
        {
          name: 'DOMException',
          message: "Use lazy function `const { lazyDOMExceptionClass } = require('internal/util');` instead of the global.",
        },
        {
          name: 'Event',
          message: "Use `const { Event } = require('internal/event_target');` instead of the global.",
        },
        {
          name: 'EventTarget',
          message: "Use `const { EventTarget } = require('internal/event_target');` instead of the global.",
        },
        {
          name: 'File',
          message: "Use `const { File } = require('buffer');` instead of the global.",
        },
        {
          name: 'FormData',
          message: "Use `const { FormData } = require('internal/deps/undici/undici');` instead of the global.",
        },
        {
          name: 'Headers',
          message: "Use `const { Headers } = require('internal/deps/undici/undici');` instead of the global.",
        },
        // Intl is not available in primordials because it can be
        // disabled with --without-intl build flag.
        {
          name: 'Intl',
          message: 'Use `const { Intl } = globalThis;` instead of the global.',
        },
        {
          name: 'Iterator',
          message: 'Use `const { Iterator } = globalThis;` instead of the global.',
        },
        {
          name: 'MessageChannel',
          message: "Use `const { MessageChannel } = require('internal/worker/io');` instead of the global.",
        },
        {
          name: 'MessageEvent',
          message: "Use `const { MessageEvent } = require('internal/deps/undici/undici');` instead of the global.",
        },
        {
          name: 'MessagePort',
          message: "Use `const { MessagePort } = require('internal/worker/io');` instead of the global.",
        },
        {
          name: 'Navigator',
          message: "Use `const { Navigator } = require('internal/navigator');` instead of the global.",
        },
        {
          name: 'navigator',
          message: "Use `const { navigator } = require('internal/navigator');` instead of the global.",
        },
        {
          name: 'PerformanceEntry',
          message: "Use `const { PerformanceEntry } = require('perf_hooks');` instead of the global.",
        },
        {
          name: 'PerformanceMark',
          message: "Use `const { PerformanceMark } = require('perf_hooks');` instead of the global.",
        },
        {
          name: 'PerformanceMeasure',
          message: "Use `const { PerformanceMeasure } = require('perf_hooks');` instead of the global.",
        },
        {
          name: 'PerformanceObserverEntryList',
          message: "Use `const { PerformanceObserverEntryList } = require('perf_hooks');` instead of the global.",
        },
        {
          name: 'PerformanceObserver',
          message: "Use `const { PerformanceObserver } = require('perf_hooks');` instead of the global.",
        },
        {
          name: 'PerformanceResourceTiming',
          message: "Use `const { PerformanceResourceTiming } = require('perf_hooks');` instead of the global.",
        },
        {
          name: 'ReadableStream',
          message: "Use `const { ReadableStream } = require('internal/webstreams/readablestream')` instead of the global.",
        },
        {
          name: 'ReadableStreamDefaultReader',
          message: "Use `const { ReadableStreamDefaultReader } = require('internal/webstreams/readablestream')` instead of the global.",
        },
        {
          name: 'ReadableStreamBYOBReader',
          message: "Use `const { ReadableStreamBYOBReader } = require('internal/webstreams/readablestream')` instead of the global.",
        },
        {
          name: 'ReadableStreamBYOBRequest',
          message: "Use `const { ReadableStreamBYOBRequest } = require('internal/webstreams/readablestream')` instead of the global.",
        },
        {
          name: 'ReadableByteStreamController',
          message: "Use `const { ReadableByteStreamController } = require('internal/webstreams/readablestream')` instead of the global.",
        },
        {
          name: 'ReadableStreamDefaultController',
          message: "Use `const { ReadableStreamDefaultController } = require('internal/webstreams/readablestream')` instead of the global.",
        },
        {
          name: 'Request',
          message: "Use `const { Request } = require('internal/deps/undici/undici');` instead of the global.",
        },
        {
          name: 'Response',
          message: "Use `const { Response } = require('internal/deps/undici/undici');` instead of the global.",
        },
        // ShadowRealm is not available in primordials because it can be
        // disabled with --no-harmony-shadow-realm CLI flag.
        {
          name: 'ShadowRealm',
          message: 'Use `const { ShadowRealm } = globalThis;` instead of the global.',
        },
        // SharedArrayBuffer is not available in primordials because it can be
        // disabled with --no-harmony-sharedarraybuffer CLI flag.
        {
          name: 'SharedArrayBuffer',
          message: 'Use `const { SharedArrayBuffer } = globalThis;` instead of the global.',
        },
        {
          name: 'TextDecoder',
          message: "Use `const { TextDecoder } = require('internal/encoding');` instead of the global.",
        },
        {
          name: 'TextDecoderStream',
          message: "Use `const { TextDecoderStream } = require('internal/webstreams/encoding')` instead of the global.",
        },
        {
          name: 'TextEncoder',
          message: "Use `const { TextEncoder } = require('internal/encoding');` instead of the global.",
        },
        {
          name: 'TextEncoderStream',
          message: "Use `const { TextEncoderStream } = require('internal/webstreams/encoding')` instead of the global.",
        },
        {
          name: 'TransformStream',
          message: "Use `const { TransformStream } = require('internal/webstreams/transformstream')` instead of the global.",
        },
        {
          name: 'TransformStreamDefaultController',
          message: "Use `const { TransformStreamDefaultController } = require('internal/webstreams/transformstream')` instead of the global.",
        },
        {
          name: 'URL',
          message: "Use `const { URL } = require('internal/url');` instead of the global.",
        },
        {
          name: 'URLSearchParams',
          message: "Use `const { URLSearchParams } = require('internal/url');` instead of the global.",
        },
        // WebAssembly is not available in primordials because it can be
        // disabled with --jitless CLI flag.
        {
          name: 'WebAssembly',
          message: 'Use `const { WebAssembly } = globalThis;` instead of the global.',
        },
        {
          name: 'WritableStream',
          message: "Use `const { WritableStream } = require('internal/webstreams/writablestream')` instead of the global.",
        },
        {
          name: 'WritableStreamDefaultWriter',
          message: "Use `const { WritableStreamDefaultWriter } = require('internal/webstreams/writablestream')` instead of the global.",
        },
        {
          name: 'WritableStreamDefaultController',
          message: "Use `const { WritableStreamDefaultController } = require('internal/webstreams/writablestream')` instead of the global.",
        },
        {
          name: 'atob',
          message: "Use `const { atob } = require('buffer');` instead of the global.",
        },
        {
          name: 'btoa',
          message: "Use `const { btoa } = require('buffer');` instead of the global.",
        },
        {
          name: 'clearImmediate',
          message: "Use `const { clearImmediate } = require('timers');` instead of the global.",
        },
        {
          name: 'clearInterval',
          message: "Use `const { clearInterval } = require('timers');` instead of the global.",
        },
        {
          name: 'clearTimeout',
          message: "Use `const { clearTimeout } = require('timers');` instead of the global.",
        },
        {
          name: 'console',
          message: "Use `const console = require('internal/console/global');` instead of the global.",
        },
        {
          name: 'crypto',
          message: "Use `const { crypto } = require('internal/crypto/webcrypto');` instead of the global.",
        },
        {
          name: 'Crypto',
          message: "Use `const { Crypto } = require('internal/crypto/webcrypto');` instead of the global.",
        },
        {
          name: 'CryptoKey',
          message: "Use `const { CryptoKey } = require('internal/crypto/webcrypto');` instead of the global.",
        },
        {
          name: 'EventSource',
          message: "Use `const { EventSource } = require('internal/deps/undici/undici');` instead of the global.",
        },
        {
          name: 'fetch',
          message: "Use `const { fetch } = require('internal/deps/undici/undici');` instead of the global.",
        },
        {
          name: 'global',
          message: 'Use `const { globalThis } = primordials;` instead of `global`.',
        },
        {
          name: 'globalThis',
          message: 'Use `const { globalThis } = primordials;` instead of the global.',
        },
        {
          name: 'performance',
          message: "Use `const { performance } = require('perf_hooks');` instead of the global.",
        },
        {
          name: 'queueMicrotask',
          message: "Use `const { queueMicrotask } = require('internal/process/task_queues');` instead of the global.",
        },
        {
          name: 'setImmediate',
          message: "Use `const { setImmediate } = require('timers');` instead of the global.",
        },
        {
          name: 'setInterval',
          message: "Use `const { setInterval } = require('timers');` instead of the global.",
        },
        {
          name: 'setTimeout',
          message: "Use `const { setTimeout } = require('timers');` instead of the global.",
        },
        {
          name: 'structuredClone',
          message: "Use `const { structuredClone } = internalBinding('messaging');` instead of the global.",
        },
        {
          name: 'SubtleCrypto',
          message: "Use `const { SubtleCrypto } = require('internal/crypto/webcrypto');` instead of the global.",
        },
        // Float16Array is not available in primordials because it's only available with --js-float16array CLI flag.
        {
          name: 'Float16Array',
          message: 'Use `const { Float16Array } = globalThis;` instead of the global.',
        },
      ],
      'no-restricted-modules': [
        'error',
        {
          name: 'url',
          message: 'Require `internal/url` instead of `url`.',
        },
      ],

      // Stylistic rules.
      '@stylistic/js/no-mixed-operators': [
        'error',
        {
          groups: [
            ['&&', '||'],
          ],
        },
      ],

      // Custom rules in tools/eslint-rules.
      'node-core/alphabetize-errors': 'error',
      'node-core/alphabetize-primordials': 'error',
      'node-core/avoid-prototype-pollution': 'error',
      'node-core/lowercase-name-for-primitive': 'error',
      'node-core/non-ascii-character': 'error',
      'node-core/no-array-destructuring': 'error',
      'node-core/prefer-primordials': [
        'error',
        { name: 'AggregateError' },
        { name: 'Array' },
        { name: 'ArrayBuffer' },
        { name: 'Atomics' },
        { name: 'BigInt' },
        { name: 'BigInt64Array' },
        { name: 'BigUint64Array' },
        { name: 'Boolean' },
        { name: 'DataView' },
        { name: 'Date' },
        { name: 'decodeURI' },
        { name: 'decodeURIComponent' },
        { name: 'encodeURI' },
        { name: 'encodeURIComponent' },
        { name: 'escape' },
        { name: 'eval' },
        {
          name: 'Error',
          ignore: [
            'prepareStackTrace',
            'stackTraceLimit',
          ],
        },
        { name: 'EvalError' },
        {
          name: 'FinalizationRegistry',
          into: 'Safe',
        },
        { name: 'Float32Array' },
        { name: 'Float64Array' },
        { name: 'Function' },
        { name: 'Int16Array' },
        { name: 'Int32Array' },
        { name: 'Int8Array' },
        {
          name: 'isFinite',
          into: 'Number',
        },
        {
          name: 'isNaN',
          into: 'Number',
        },
        { name: 'JSON' },
        {
          name: 'Map',
          into: 'Safe',
        },
        { name: 'Math' },
        { name: 'Number' },
        { name: 'Object' },
        {
          name: 'parseFloat',
          into: 'Number',
        },
        {
          name: 'parseInt',
          into: 'Number',
        },
        { name: 'Proxy' },
        { name: 'Promise' },
        { name: 'RangeError' },
        { name: 'ReferenceError' },
        { name: 'Reflect' },
        { name: 'RegExp' },
        {
          name: 'Set',
          into: 'Safe',
        },
        { name: 'String' },
        { name: 'Symbol' },
        { name: 'SyntaxError' },
        { name: 'TypeError' },
        { name: 'Uint16Array' },
        { name: 'Uint32Array' },
        { name: 'Uint8Array' },
        { name: 'Uint8ClampedArray' },
        { name: 'unescape' },
        { name: 'URIError' },
        {
          name: 'WeakMap',
          into: 'Safe',
        },
        {
          name: 'WeakRef',
          into: 'Safe',
        },
        {
          name: 'WeakSet',
          into: 'Safe',
        },
      ],
    },
  },
  {
    files: ['lib/internal/modules/**/*.js'],
    rules: {
      'curly': 'error',
    },
  },
  {
    files: ['lib/internal/per_context/primordials.js'],
    rules: {
      'node-core/alphabetize-primordials': [
        'error',
        { enforceTopPosition: false },
      ],
    },
  },
  {
    files: ['lib/internal/test_runner/**/*.js'],
    rules: {
      'node-core/set-proto-to-null-in-object': 'error',
    },
  },
  {
    files: [
      'lib/_http_*.js',
      'lib/_tls_*.js',
      'lib/http.js',
      'lib/http2.js',
      'lib/internal/http.js',
      'lib/internal/http2/*.js',
      'lib/tls.js',
      'lib/zlib.js',
    ],
    rules: {
      'no-restricted-syntax': [
        ...noRestrictedSyntax,
        {
          selector: 'VariableDeclarator:has(.init[name="primordials"]) Identifier[name=/Prototype[A-Z]/]:not([name=/^(Object|Reflect)(Get|Set)PrototypeOf$/])',
          message: 'Do not use prototype primordials in this file.',
        },
      ],
    },
  },
];
