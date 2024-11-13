# Global objects

<!--introduced_in=v0.10.0-->

<!-- type=misc -->

These objects are available in all modules.

The following variables may appear to be global but are not. They exist only in
the scope of [CommonJS modules][]:

* [`__dirname`][]
* [`__filename`][]
* [`exports`][]
* [`module`][]
* [`require()`][]

The objects listed here are specific to Node.js. There are [built-in objects][]
that are part of the JavaScript language itself, which are also globally
accessible.

## Class: `AbortController`

<!-- YAML
added:
  - v15.0.0
  - v14.17.0
changes:
  - version: v15.4.0
    pr-url: https://github.com/nodejs/node/pull/35949
    description: No longer experimental.
-->

<!-- type=global -->

A utility class used to signal cancelation in selected `Promise`-based APIs.
The API is based on the Web API [`AbortController`][].

```js
const ac = new AbortController();

ac.signal.addEventListener('abort', () => console.log('Aborted!'),
                           { once: true });

ac.abort();

console.log(ac.signal.aborted);  // Prints true
```

### `abortController.abort([reason])`

<!-- YAML
added:
  - v15.0.0
  - v14.17.0
changes:
  - version:
      - v17.2.0
      - v16.14.0
    pr-url: https://github.com/nodejs/node/pull/40807
    description: Added the new optional reason argument.
-->

* `reason` {any} An optional reason, retrievable on the `AbortSignal`'s
  `reason` property.

Triggers the abort signal, causing the `abortController.signal` to emit
the `'abort'` event.

### `abortController.signal`

<!-- YAML
added:
  - v15.0.0
  - v14.17.0
-->

* Type: {AbortSignal}

### Class: `AbortSignal`

<!-- YAML
added:
  - v15.0.0
  - v14.17.0
-->

* Extends: {EventTarget}

The `AbortSignal` is used to notify observers when the
`abortController.abort()` method is called.

#### Static method: `AbortSignal.abort([reason])`

<!-- YAML
added:
  - v15.12.0
  - v14.17.0
changes:
  - version:
      - v17.2.0
      - v16.14.0
    pr-url: https://github.com/nodejs/node/pull/40807
    description: Added the new optional reason argument.
-->

* `reason`: {any}
* Returns: {AbortSignal}

Returns a new already aborted `AbortSignal`.

#### Static method: `AbortSignal.timeout(delay)`

<!-- YAML
added:
  - v17.3.0
  - v16.14.0
-->

* `delay` {number} The number of milliseconds to wait before triggering
  the AbortSignal.

Returns a new `AbortSignal` which will be aborted in `delay` milliseconds.

#### Static method: `AbortSignal.any(signals)`

<!-- YAML
added:
  - v20.3.0
  - v18.17.0
-->

* `signals` {AbortSignal\[]} The `AbortSignal`s of which to compose a new `AbortSignal`.

Returns a new `AbortSignal` which will be aborted if any of the provided
signals are aborted. Its [`abortSignal.reason`][] will be set to whichever
one of the `signals` caused it to be aborted.

#### Event: `'abort'`

<!-- YAML
added:
  - v15.0.0
  - v14.17.0
-->

The `'abort'` event is emitted when the `abortController.abort()` method
is called. The callback is invoked with a single object argument with a
single `type` property set to `'abort'`:

```js
const ac = new AbortController();

// Use either the onabort property...
ac.signal.onabort = () => console.log('aborted!');

// Or the EventTarget API...
ac.signal.addEventListener('abort', (event) => {
  console.log(event.type);  // Prints 'abort'
}, { once: true });

ac.abort();
```

The `AbortController` with which the `AbortSignal` is associated will only
ever trigger the `'abort'` event once. We recommended that code check
that the `abortSignal.aborted` attribute is `false` before adding an `'abort'`
event listener.

Any event listeners attached to the `AbortSignal` should use the
`{ once: true }` option (or, if using the `EventEmitter` APIs to attach a
listener, use the `once()` method) to ensure that the event listener is
removed as soon as the `'abort'` event is handled. Failure to do so may
result in memory leaks.

#### `abortSignal.aborted`

<!-- YAML
added:
  - v15.0.0
  - v14.17.0
-->

* Type: {boolean} True after the `AbortController` has been aborted.

#### `abortSignal.onabort`

<!-- YAML
added:
  - v15.0.0
  - v14.17.0
-->

* Type: {Function}

An optional callback function that may be set by user code to be notified
when the `abortController.abort()` function has been called.

#### `abortSignal.reason`

<!-- YAML
added:
  - v17.2.0
  - v16.14.0
-->

* Type: {any}

An optional reason specified when the `AbortSignal` was triggered.

```js
const ac = new AbortController();
ac.abort(new Error('boom!'));
console.log(ac.signal.reason);  // Error: boom!
```

#### `abortSignal.throwIfAborted()`

<!-- YAML
added:
  - v17.3.0
  - v16.17.0
-->

If `abortSignal.aborted` is `true`, throws `abortSignal.reason`.

## Class: `Blob`

<!-- YAML
added: v18.0.0
-->

<!-- type=global -->

See {Blob}.

## Class: `Buffer`

<!-- YAML
added: v0.1.103
-->

<!-- type=global -->

* {Function}

Used to handle binary data. See the [buffer section][].

## Class: `ByteLengthQueuingStrategy`

<!-- YAML
added: v18.0.0
-->

> Stability: 1 - Experimental.

A browser-compatible implementation of [`ByteLengthQueuingStrategy`][].

## `__dirname`

This variable may appear to be global but is not. See [`__dirname`][].

## `__filename`

This variable may appear to be global but is not. See [`__filename`][].

## `atob(data)`

<!-- YAML
added: v16.0.0
-->

> Stability: 3 - Legacy. Use `Buffer.from(data, 'base64')` instead.

Global alias for [`buffer.atob()`][].

## `BroadcastChannel`

<!-- YAML
added: v18.0.0
-->

See {BroadcastChannel}.

## `btoa(data)`

<!-- YAML
added: v16.0.0
-->

> Stability: 3 - Legacy. Use `buf.toString('base64')` instead.

Global alias for [`buffer.btoa()`][].

## `clearImmediate(immediateObject)`

<!-- YAML
added: v0.9.1
-->

<!--type=global-->

[`clearImmediate`][] is described in the [timers][] section.

## `clearInterval(intervalObject)`

<!-- YAML
added: v0.0.1
-->

<!--type=global-->

[`clearInterval`][] is described in the [timers][] section.

## `clearTimeout(timeoutObject)`

<!-- YAML
added: v0.0.1
-->

<!--type=global-->

[`clearTimeout`][] is described in the [timers][] section.

## `CloseEvent`

<!-- YAML
added: v23.0.0
-->

<!-- type=global -->

The `CloseEvent` class. See [`CloseEvent`][] for more details.

A browser-compatible implementation of [`CloseEvent`][]. Disable this API
with the [`--no-experimental-websocket`][] CLI flag.

## Class: `CompressionStream`

<!-- YAML
added: v18.0.0
-->

> Stability: 1 - Experimental.

A browser-compatible implementation of [`CompressionStream`][].

## `console`

<!-- YAML
added: v0.1.100
-->

<!-- type=global -->

* {Object}

Used to print to stdout and stderr. See the [`console`][] section.

## Class: `CountQueuingStrategy`

<!-- YAML
added: v18.0.0
-->

> Stability: 1 - Experimental.

A browser-compatible implementation of [`CountQueuingStrategy`][].

## `Crypto`

<!-- YAML
added:
  - v17.6.0
  - v16.15.0
changes:
  - version: v23.0.0
    pr-url: https://github.com/nodejs/node/pull/52564
    description: No longer experimental.
  - version: v19.0.0
    pr-url: https://github.com/nodejs/node/pull/42083
    description: No longer behind `--experimental-global-webcrypto` CLI flag.
-->

> Stability: 2 - Stable.

A browser-compatible implementation of {Crypto}. This global is available
only if the Node.js binary was compiled with including support for the
`node:crypto` module.

## `crypto`

<!-- YAML
added:
  - v17.6.0
  - v16.15.0
changes:
  - version: v23.0.0
    pr-url: https://github.com/nodejs/node/pull/52564
    description: No longer experimental.
  - version: v19.0.0
    pr-url: https://github.com/nodejs/node/pull/42083
    description: No longer behind `--experimental-global-webcrypto` CLI flag.
-->

> Stability: 2 - Stable.

A browser-compatible implementation of the [Web Crypto API][].

## `CryptoKey`

<!-- YAML
added:
  - v17.6.0
  - v16.15.0
changes:
  - version: v23.0.0
    pr-url: https://github.com/nodejs/node/pull/52564
    description: No longer experimental.
  - version: v19.0.0
    pr-url: https://github.com/nodejs/node/pull/42083
    description: No longer behind `--experimental-global-webcrypto` CLI flag.
-->

> Stability: 2 - Stable.

A browser-compatible implementation of {CryptoKey}. This global is available
only if the Node.js binary was compiled with including support for the
`node:crypto` module.

## `CustomEvent`

<!-- YAML
added:
  - v18.7.0
  - v16.17.0
changes:
  - version: v23.0.0
    pr-url: https://github.com/nodejs/node/pull/52723
    description: No longer experimental.
  - version:
    - v22.1.0
    - v20.13.0
    pr-url: https://github.com/nodejs/node/pull/52618
    description: CustomEvent is now stable.
  - version: v19.0.0
    pr-url: https://github.com/nodejs/node/pull/44860
    description: No longer behind `--experimental-global-customevent` CLI flag.
-->

> Stability: 2 - Stable

<!-- type=global -->

A browser-compatible implementation of the [`CustomEvent` Web API][].

## Class: `DecompressionStream`

<!-- YAML
added: v18.0.0
-->

> Stability: 1 - Experimental.

A browser-compatible implementation of [`DecompressionStream`][].

## `Event`

<!-- YAML
added: v15.0.0
changes:
  - version: v15.4.0
    pr-url: https://github.com/nodejs/node/pull/35949
    description: No longer experimental.
-->

<!-- type=global -->

A browser-compatible implementation of the `Event` class. See
[`EventTarget` and `Event` API][] for more details.

## `EventSource`

<!-- YAML
added:
  - v22.3.0
  - v20.18.0
-->

> Stability: 1 - Experimental. Enable this API with the [`--experimental-eventsource`][]
> CLI flag.

A browser-compatible implementation of the [`EventSource`][] class.

## `EventTarget`

<!-- YAML
added: v15.0.0
changes:
  - version: v15.4.0
    pr-url: https://github.com/nodejs/node/pull/35949
    description: No longer experimental.
-->

<!-- type=global -->

A browser-compatible implementation of the `EventTarget` class. See
[`EventTarget` and `Event` API][] for more details.

## `exports`

This variable may appear to be global but is not. See [`exports`][].

## `fetch`

<!-- YAML
added:
  - v17.5.0
  - v16.15.0
changes:
  - version:
    - v21.0.0
    pr-url: https://github.com/nodejs/node/pull/45684
    description: No longer experimental.
  - version: v18.0.0
    pr-url: https://github.com/nodejs/node/pull/41811
    description: No longer behind `--experimental-fetch` CLI flag.
-->

> Stability: 2 - Stable

A browser-compatible implementation of the [`fetch()`][] function.

## Class: `File`

<!-- YAML
added: v20.0.0
-->

<!-- type=global -->

See {File}.

## Class `FormData`

<!-- YAML
added:
  - v17.6.0
  - v16.15.0
changes:
  - version:
    - v21.0.0
    pr-url: https://github.com/nodejs/node/pull/45684
    description: No longer experimental.
  - version: v18.0.0
    pr-url: https://github.com/nodejs/node/pull/41811
    description: No longer behind `--experimental-fetch` CLI flag.
-->

> Stability: 2 - Stable

A browser-compatible implementation of {FormData}.

## `global`

<!-- YAML
added: v0.1.27
-->

<!-- type=global -->

> Stability: 3 - Legacy. Use [`globalThis`][] instead.

* {Object} The global namespace object.

In browsers, the top-level scope has traditionally been the global scope. This
means that `var something` will define a new global variable, except within
ECMAScript modules. In Node.js, this is different. The top-level scope is not
the global scope; `var something` inside a Node.js module will be local to that
module, regardless of whether it is a [CommonJS module][] or an
[ECMAScript module][].

## Class `Headers`

<!-- YAML
added:
  - v17.5.0
  - v16.15.0
changes:
  - version:
    - v21.0.0
    pr-url: https://github.com/nodejs/node/pull/45684
    description: No longer experimental.
  - version: v18.0.0
    pr-url: https://github.com/nodejs/node/pull/41811
    description: No longer behind `--experimental-fetch` CLI flag.
-->

> Stability: 2 - Stable

A browser-compatible implementation of {Headers}.

## `localStorage`

<!-- YAML
added: v22.4.0
-->

> Stability: 1.0 - Early development.

A browser-compatible implementation of [`localStorage`][]. Data is stored
unencrypted in the file specified by the [`--localstorage-file`][] CLI flag.
The maximum amount of data that can be stored is 10 MB.
Any modification of this data outside of the Web Storage API is not supported.
Enable this API with the [`--experimental-webstorage`][] CLI flag.
`localStorage` data is not stored per user or per request when used in the context
of a server, it is shared across all users and requests.

## `MessageChannel`

<!-- YAML
added: v15.0.0
-->

<!-- type=global -->

The `MessageChannel` class. See [`MessageChannel`][] for more details.

## `MessageEvent`

<!-- YAML
added: v15.0.0
-->

<!-- type=global -->

The `MessageEvent` class. See [`MessageEvent`][] for more details.

## `MessagePort`

<!-- YAML
added: v15.0.0
-->

<!-- type=global -->

The `MessagePort` class. See [`MessagePort`][] for more details.

## `module`

This variable may appear to be global but is not. See [`module`][].

## `Navigator`

<!-- YAML
added: v21.0.0
-->

> Stability: 1.1 - Active development. Disable this API with the
> [`--no-experimental-global-navigator`][] CLI flag.

A partial implementation of the [Navigator API][].

## `navigator`

<!-- YAML
added: v21.0.0
-->

> Stability: 1.1 - Active development. Disable this API with the
> [`--no-experimental-global-navigator`][] CLI flag.

A partial implementation of [`window.navigator`][].

### `navigator.hardwareConcurrency`

<!-- YAML
added: v21.0.0
-->

* {number}

The `navigator.hardwareConcurrency` read-only property returns the number of
logical processors available to the current Node.js instance.

```js
console.log(`This process is running on ${navigator.hardwareConcurrency} logical processors`);
```

### `navigator.language`

<!-- YAML
added: v21.2.0
-->

* {string}

The `navigator.language` read-only property returns a string representing the
preferred language of the Node.js instance. The language will be determined by
the ICU library used by Node.js at runtime based on the
default language of the operating system.

The value is representing the language version as defined in [RFC 5646][].

The fallback value on builds without ICU is `'en-US'`.

```js
console.log(`The preferred language of the Node.js instance has the tag '${navigator.language}'`);
```

### `navigator.languages`

<!-- YAML
added: v21.2.0
-->

* {Array<string>}

The `navigator.languages` read-only property returns an array of strings
representing the preferred languages of the Node.js instance.
By default `navigator.languages` contains only the value of
`navigator.language`, which will be determined by the ICU library used by
Node.js at runtime based on the default language of the operating system.

The fallback value on builds without ICU is `['en-US']`.

```js
console.log(`The preferred languages are '${navigator.languages}'`);
```

### `navigator.platform`

<!-- YAML
added: v21.2.0
-->

* {string}

The `navigator.platform` read-only property returns a string identifying the
platform on which the Node.js instance is running.

```js
console.log(`This process is running on ${navigator.platform}`);
```

### `navigator.userAgent`

<!-- YAML
added: v21.1.0
-->

* {string}

The `navigator.userAgent` read-only property returns user agent
consisting of the runtime name and major version number.

```js
console.log(`The user-agent is ${navigator.userAgent}`); // Prints "Node.js/21"
```

## `PerformanceEntry`

<!-- YAML
added: v19.0.0
-->

<!-- type=global -->

The `PerformanceEntry` class. See [`PerformanceEntry`][] for more details.

## `PerformanceMark`

<!-- YAML
added: v19.0.0
-->

<!-- type=global -->

The `PerformanceMark` class. See [`PerformanceMark`][] for more details.

## `PerformanceMeasure`

<!-- YAML
added: v19.0.0
-->

<!-- type=global -->

The `PerformanceMeasure` class. See [`PerformanceMeasure`][] for more details.

## `PerformanceObserver`

<!-- YAML
added: v19.0.0
-->

<!-- type=global -->

The `PerformanceObserver` class. See [`PerformanceObserver`][] for more details.

## `PerformanceObserverEntryList`

<!-- YAML
added: v19.0.0
-->

<!-- type=global -->

The `PerformanceObserverEntryList` class. See
[`PerformanceObserverEntryList`][] for more details.

## `PerformanceResourceTiming`

<!-- YAML
added: v19.0.0
-->

<!-- type=global -->

The `PerformanceResourceTiming` class. See [`PerformanceResourceTiming`][] for
more details.

## `performance`

<!-- YAML
added: v16.0.0
-->

The [`perf_hooks.performance`][] object.

## `process`

<!-- YAML
added: v0.1.7
-->

<!-- type=global -->

* {Object}

The process object. See the [`process` object][] section.

## `queueMicrotask(callback)`

<!-- YAML
added: v11.0.0
-->

<!-- type=global -->

* `callback` {Function} Function to be queued.

The `queueMicrotask()` method queues a microtask to invoke `callback`. If
`callback` throws an exception, the [`process` object][] `'uncaughtException'`
event will be emitted.

The microtask queue is managed by V8 and may be used in a similar manner to
the [`process.nextTick()`][] queue, which is managed by Node.js. The
`process.nextTick()` queue is always processed before the microtask queue
within each turn of the Node.js event loop.

```js
// Here, `queueMicrotask()` is used to ensure the 'load' event is always
// emitted asynchronously, and therefore consistently. Using
// `process.nextTick()` here would result in the 'load' event always emitting
// before any other promise jobs.

DataHandler.prototype.load = async function load(key) {
  const hit = this._cache.get(key);
  if (hit !== undefined) {
    queueMicrotask(() => {
      this.emit('load', hit);
    });
    return;
  }

  const data = await fetchData(key);
  this._cache.set(key, data);
  this.emit('load', data);
};
```

## Class: `ReadableByteStreamController`

<!-- YAML
added: v18.0.0
-->

> Stability: 1 - Experimental.

A browser-compatible implementation of [`ReadableByteStreamController`][].

## Class: `ReadableStream`

<!-- YAML
added: v18.0.0
-->

> Stability: 1 - Experimental.

A browser-compatible implementation of [`ReadableStream`][].

## Class: `ReadableStreamBYOBReader`

<!-- YAML
added: v18.0.0
-->

> Stability: 1 - Experimental.

A browser-compatible implementation of [`ReadableStreamBYOBReader`][].

## Class: `ReadableStreamBYOBRequest`

<!-- YAML
added: v18.0.0
-->

> Stability: 1 - Experimental.

A browser-compatible implementation of [`ReadableStreamBYOBRequest`][].

## Class: `ReadableStreamDefaultController`

<!-- YAML
added: v18.0.0
-->

> Stability: 1 - Experimental.

A browser-compatible implementation of [`ReadableStreamDefaultController`][].

## Class: `ReadableStreamDefaultReader`

<!-- YAML
added: v18.0.0
-->

> Stability: 1 - Experimental.

A browser-compatible implementation of [`ReadableStreamDefaultReader`][].

## `require()`

This variable may appear to be global but is not. See [`require()`][].

## `Response`

<!-- YAML
added:
  - v17.5.0
  - v16.15.0
changes:
  - version:
    - v21.0.0
    pr-url: https://github.com/nodejs/node/pull/45684
    description: No longer experimental.
  - version: v18.0.0
    pr-url: https://github.com/nodejs/node/pull/41811
    description: No longer behind `--experimental-fetch` CLI flag.
-->

> Stability: 2 - Stable

A browser-compatible implementation of {Response}.

## `Request`

<!-- YAML
added:
  - v17.5.0
  - v16.15.0
changes:
  - version:
    - v21.0.0
    pr-url: https://github.com/nodejs/node/pull/45684
    description: No longer experimental.
  - version: v18.0.0
    pr-url: https://github.com/nodejs/node/pull/41811
    description: No longer behind `--experimental-fetch` CLI flag.
-->

> Stability: 2 - Stable

A browser-compatible implementation of {Request}.

## `sessionStorage`

<!-- YAML
added: v22.4.0
-->

> Stability: 1.0 - Early development.

A browser-compatible implementation of [`sessionStorage`][]. Data is stored in
memory, with a storage quota of 10 MB. `sessionStorage` data persists only within
the currently running process, and is not shared between workers.

## `setImmediate(callback[, ...args])`

<!-- YAML
added: v0.9.1
-->

<!-- type=global -->

[`setImmediate`][] is described in the [timers][] section.

## `setInterval(callback, delay[, ...args])`

<!-- YAML
added: v0.0.1
-->

<!-- type=global -->

[`setInterval`][] is described in the [timers][] section.

## `setTimeout(callback, delay[, ...args])`

<!-- YAML
added: v0.0.1
-->

<!-- type=global -->

[`setTimeout`][] is described in the [timers][] section.

## Class: `Storage`

<!-- YAML
added: v22.4.0
-->

> Stability: 1.0 - Early development.

A browser-compatible implementation of [`Storage`][]. Enable this API with the
[`--experimental-webstorage`][] CLI flag.

## `structuredClone(value[, options])`

<!-- YAML
added: v17.0.0
-->

<!-- type=global -->

The WHATWG [`structuredClone`][] method.

## `SubtleCrypto`

<!-- YAML
added:
  - v17.6.0
  - v16.15.0
changes:
  - version: v19.0.0
    pr-url: https://github.com/nodejs/node/pull/42083
    description: No longer behind `--experimental-global-webcrypto` CLI flag.
-->

> Stability: 2 - Stable.

A browser-compatible implementation of {SubtleCrypto}. This global is available
only if the Node.js binary was compiled with including support for the
`node:crypto` module.

## `DOMException`

<!-- YAML
added: v17.0.0
-->

<!-- type=global -->

The WHATWG `DOMException` class. See [`DOMException`][] for more details.

## `TextDecoder`

<!-- YAML
added: v11.0.0
-->

<!-- type=global -->

The WHATWG `TextDecoder` class. See the [`TextDecoder`][] section.

## Class: `TextDecoderStream`

<!-- YAML
added: v18.0.0
-->

> Stability: 1 - Experimental.

A browser-compatible implementation of [`TextDecoderStream`][].

## `TextEncoder`

<!-- YAML
added: v11.0.0
-->

<!-- type=global -->

The WHATWG `TextEncoder` class. See the [`TextEncoder`][] section.

## Class: `TextEncoderStream`

<!-- YAML
added: v18.0.0
-->

> Stability: 1 - Experimental.

A browser-compatible implementation of [`TextEncoderStream`][].

## Class: `TransformStream`

<!-- YAML
added: v18.0.0
-->

> Stability: 1 - Experimental.

A browser-compatible implementation of [`TransformStream`][].

## Class: `TransformStreamDefaultController`

<!-- YAML
added: v18.0.0
-->

> Stability: 1 - Experimental.

A browser-compatible implementation of [`TransformStreamDefaultController`][].

## `URL`

<!-- YAML
added: v10.0.0
-->

<!-- type=global -->

The WHATWG `URL` class. See the [`URL`][] section.

## `URLSearchParams`

<!-- YAML
added: v10.0.0
-->

<!-- type=global -->

The WHATWG `URLSearchParams` class. See the [`URLSearchParams`][] section.

## `WebAssembly`

<!-- YAML
added: v8.0.0
-->

<!-- type=global -->

* {Object}

The object that acts as the namespace for all W3C
[WebAssembly][webassembly-org] related functionality. See the
[Mozilla Developer Network][webassembly-mdn] for usage and compatibility.

## `WebSocket`

<!-- YAML
added:
  - v21.0.0
  - v20.10.0
changes:
  - version: v22.4.0
    pr-url: https://github.com/nodejs/node/pull/53352
    description: No longer experimental.
  - version: v22.0.0
    pr-url: https://github.com/nodejs/node/pull/51594
    description: No longer behind `--experimental-websocket` CLI flag.
-->

> Stability: 2 - Stable.

A browser-compatible implementation of [`WebSocket`][]. Disable this API
with the [`--no-experimental-websocket`][] CLI flag.

## Class: `WritableStream`

<!-- YAML
added: v18.0.0
-->

> Stability: 1 - Experimental.

A browser-compatible implementation of [`WritableStream`][].

## Class: `WritableStreamDefaultController`

<!-- YAML
added: v18.0.0
-->

> Stability: 1 - Experimental.

A browser-compatible implementation of [`WritableStreamDefaultController`][].

## Class: `WritableStreamDefaultWriter`

<!-- YAML
added: v18.0.0
-->

> Stability: 1 - Experimental.

A browser-compatible implementation of [`WritableStreamDefaultWriter`][].

[CommonJS module]: modules.md
[CommonJS modules]: modules.md
[ECMAScript module]: esm.md
[Navigator API]: https://html.spec.whatwg.org/multipage/system-state.html#the-navigator-object
[RFC 5646]: https://www.rfc-editor.org/rfc/rfc5646.txt
[Web Crypto API]: webcrypto.md
[`--experimental-eventsource`]: cli.md#--experimental-eventsource
[`--experimental-webstorage`]: cli.md#--experimental-webstorage
[`--localstorage-file`]: cli.md#--localstorage-filefile
[`--no-experimental-global-navigator`]: cli.md#--no-experimental-global-navigator
[`--no-experimental-websocket`]: cli.md#--no-experimental-websocket
[`AbortController`]: https://developer.mozilla.org/en-US/docs/Web/API/AbortController
[`ByteLengthQueuingStrategy`]: webstreams.md#class-bytelengthqueuingstrategy
[`CloseEvent`]: https://developer.mozilla.org/en-US/docs/Web/API/CloseEvent/CloseEvent
[`CompressionStream`]: webstreams.md#class-compressionstream
[`CountQueuingStrategy`]: webstreams.md#class-countqueuingstrategy
[`CustomEvent` Web API]: https://dom.spec.whatwg.org/#customevent
[`DOMException`]: https://developer.mozilla.org/en-US/docs/Web/API/DOMException
[`DecompressionStream`]: webstreams.md#class-decompressionstream
[`EventSource`]: https://developer.mozilla.org/en-US/docs/Web/API/EventSource
[`EventTarget` and `Event` API]: events.md#eventtarget-and-event-api
[`MessageChannel`]: worker_threads.md#class-messagechannel
[`MessageEvent`]: https://developer.mozilla.org/en-US/docs/Web/API/MessageEvent/MessageEvent
[`MessagePort`]: worker_threads.md#class-messageport
[`PerformanceEntry`]: perf_hooks.md#class-performanceentry
[`PerformanceMark`]: perf_hooks.md#class-performancemark
[`PerformanceMeasure`]: perf_hooks.md#class-performancemeasure
[`PerformanceObserverEntryList`]: perf_hooks.md#class-performanceobserverentrylist
[`PerformanceObserver`]: perf_hooks.md#class-performanceobserver
[`PerformanceResourceTiming`]: perf_hooks.md#class-performanceresourcetiming
[`ReadableByteStreamController`]: webstreams.md#class-readablebytestreamcontroller
[`ReadableStreamBYOBReader`]: webstreams.md#class-readablestreambyobreader
[`ReadableStreamBYOBRequest`]: webstreams.md#class-readablestreambyobrequest
[`ReadableStreamDefaultController`]: webstreams.md#class-readablestreamdefaultcontroller
[`ReadableStreamDefaultReader`]: webstreams.md#class-readablestreamdefaultreader
[`ReadableStream`]: webstreams.md#class-readablestream
[`Storage`]: https://developer.mozilla.org/en-US/docs/Web/API/Storage
[`TextDecoderStream`]: webstreams.md#class-textdecoderstream
[`TextDecoder`]: util.md#class-utiltextdecoder
[`TextEncoderStream`]: webstreams.md#class-textencoderstream
[`TextEncoder`]: util.md#class-utiltextencoder
[`TransformStreamDefaultController`]: webstreams.md#class-transformstreamdefaultcontroller
[`TransformStream`]: webstreams.md#class-transformstream
[`URLSearchParams`]: url.md#class-urlsearchparams
[`URL`]: url.md#class-url
[`WebSocket`]: https://developer.mozilla.org/en-US/docs/Web/API/WebSocket
[`WritableStreamDefaultController`]: webstreams.md#class-writablestreamdefaultcontroller
[`WritableStreamDefaultWriter`]: webstreams.md#class-writablestreamdefaultwriter
[`WritableStream`]: webstreams.md#class-writablestream
[`__dirname`]: modules.md#__dirname
[`__filename`]: modules.md#__filename
[`abortSignal.reason`]: #abortsignalreason
[`buffer.atob()`]: buffer.md#bufferatobdata
[`buffer.btoa()`]: buffer.md#bufferbtoadata
[`clearImmediate`]: timers.md#clearimmediateimmediate
[`clearInterval`]: timers.md#clearintervaltimeout
[`clearTimeout`]: timers.md#cleartimeouttimeout
[`console`]: console.md
[`exports`]: modules.md#exports
[`fetch()`]: https://developer.mozilla.org/en-US/docs/Web/API/fetch
[`globalThis`]: https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/globalThis
[`localStorage`]: https://developer.mozilla.org/en-US/docs/Web/API/Window/localStorage
[`module`]: modules.md#module
[`perf_hooks.performance`]: perf_hooks.md#perf_hooksperformance
[`process.nextTick()`]: process.md#processnexttickcallback-args
[`process` object]: process.md#process
[`require()`]: modules.md#requireid
[`sessionStorage`]: https://developer.mozilla.org/en-US/docs/Web/API/Window/sessionStorage
[`setImmediate`]: timers.md#setimmediatecallback-args
[`setInterval`]: timers.md#setintervalcallback-delay-args
[`setTimeout`]: timers.md#settimeoutcallback-delay-args
[`structuredClone`]: https://developer.mozilla.org/en-US/docs/Web/API/structuredClone
[`window.navigator`]: https://developer.mozilla.org/en-US/docs/Web/API/Window/navigator
[buffer section]: buffer.md
[built-in objects]: https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects
[timers]: timers.md
[webassembly-mdn]: https://developer.mozilla.org/en-US/docs/WebAssembly
[webassembly-org]: https://webassembly.org
