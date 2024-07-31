# Web Streams API

<!--introduced_in=v16.5.0-->

<!-- YAML
added: v16.5.0
changes:
  - version:
    - v21.0.0
    pr-url: https://github.com/nodejs/node/pull/45684
    description: No longer experimental.
  - version: v18.0.0
    pr-url: https://github.com/nodejs/node/pull/42225
    description: Use of this API no longer emit a runtime warning.
-->

> Stability: 2 - Stable

An implementation of the [WHATWG Streams Standard][].

## Overview

The [WHATWG Streams Standard][] (or "web streams") defines an API for handling
streaming data. It is similar to the Node.js [Streams][] API but emerged later
and has become the "standard" API for streaming data across many JavaScript
environments.

There are three primary types of objects:

* `ReadableStream` - Represents a source of streaming data.
* `WritableStream` - Represents a destination for streaming data.
* `TransformStream` - Represents an algorithm for transforming streaming data.

### Example `ReadableStream`

This example creates a simple `ReadableStream` that pushes the current
`performance.now()` timestamp once every second forever. An async iterable
is used to read the data from the stream.

```mjs
import {
  ReadableStream,
} from 'node:stream/web';

import {
  setInterval as every,
} from 'node:timers/promises';

import {
  performance,
} from 'node:perf_hooks';

const SECOND = 1000;

const stream = new ReadableStream({
  async start(controller) {
    for await (const _ of every(SECOND))
      controller.enqueue(performance.now());
  },
});

for await (const value of stream)
  console.log(value);
```

```cjs
const {
  ReadableStream,
} = require('node:stream/web');

const {
  setInterval: every,
} = require('node:timers/promises');

const {
  performance,
} = require('node:perf_hooks');

const SECOND = 1000;

const stream = new ReadableStream({
  async start(controller) {
    for await (const _ of every(SECOND))
      controller.enqueue(performance.now());
  },
});

(async () => {
  for await (const value of stream)
    console.log(value);
})();
```

## API

### Class: `ReadableStream`

<!-- YAML
added: v16.5.0
changes:
  - version: v18.0.0
    pr-url: https://github.com/nodejs/node/pull/42225
    description: This class is now exposed on the global object.
-->

#### `new ReadableStream([underlyingSource [, strategy]])`

<!-- YAML
added: v16.5.0
-->

<!--lint disable maximum-line-length remark-lint-->

* `underlyingSource` {Object}
  * `start` {Function} A user-defined function that is invoked immediately when
    the `ReadableStream` is created.
    * `controller` {ReadableStreamDefaultController|ReadableByteStreamController}
    * Returns: `undefined` or a promise fulfilled with `undefined`.
  * `pull` {Function} A user-defined function that is called repeatedly when the
    `ReadableStream` internal queue is not full. The operation may be sync or
    async. If async, the function will not be called again until the previously
    returned promise is fulfilled.
    * `controller` {ReadableStreamDefaultController|ReadableByteStreamController}
    * Returns: A promise fulfilled with `undefined`.
  * `cancel` {Function} A user-defined function that is called when the
    `ReadableStream` is canceled.
    * `reason` {any}
    * Returns: A promise fulfilled with `undefined`.
  * `type` {string} Must be `'bytes'` or `undefined`.
  * `autoAllocateChunkSize` {number} Used only when `type` is equal to
    `'bytes'`. When set to a non-zero value a view buffer is automatically
    allocated to `ReadableByteStreamController.byobRequest`. When not set
    one must use stream's internal queues to transfer data via the default
    reader `ReadableStreamDefaultReader`.
* `strategy` {Object}
  * `highWaterMark` {number} The maximum internal queue size before backpressure
    is applied.
  * `size` {Function} A user-defined function used to identify the size of each
    chunk of data.
    * `chunk` {any}
    * Returns: {number}

<!--lint enable maximum-line-length remark-lint-->

#### `readableStream.locked`

<!-- YAML
added: v16.5.0
-->

* Type: {boolean} Set to `true` if there is an active reader for this
  {ReadableStream}.

The `readableStream.locked` property is `false` by default, and is
switched to `true` while there is an active reader consuming the
stream's data.

#### `readableStream.cancel([reason])`

<!-- YAML
added: v16.5.0
-->

* `reason` {any}
* Returns: A promise fulfilled with `undefined` once cancelation has
  been completed.

#### `readableStream.getReader([options])`

<!-- YAML
added: v16.5.0
-->

* `options` {Object}
  * `mode` {string} `'byob'` or `undefined`
* Returns: {ReadableStreamDefaultReader|ReadableStreamBYOBReader}

```mjs
import { ReadableStream } from 'node:stream/web';

const stream = new ReadableStream();

const reader = stream.getReader();

console.log(await reader.read());
```

```cjs
const { ReadableStream } = require('node:stream/web');

const stream = new ReadableStream();

const reader = stream.getReader();

reader.read().then(console.log);
```

Causes the `readableStream.locked` to be `true`.

#### `readableStream.pipeThrough(transform[, options])`

<!-- YAML
added: v16.5.0
-->

* `transform` {Object}
  * `readable` {ReadableStream} The `ReadableStream` to which
    `transform.writable` will push the potentially modified data
    it receives from this `ReadableStream`.
  * `writable` {WritableStream} The `WritableStream` to which this
    `ReadableStream`'s data will be written.
* `options` {Object}
  * `preventAbort` {boolean} When `true`, errors in this `ReadableStream`
    will not cause `transform.writable` to be aborted.
  * `preventCancel` {boolean} When `true`, errors in the destination
    `transform.writable` do not cause this `ReadableStream` to be
    canceled.
  * `preventClose` {boolean} When `true`, closing this `ReadableStream`
    does not cause `transform.writable` to be closed.
  * `signal` {AbortSignal} Allows the transfer of data to be canceled
    using an {AbortController}.
* Returns: {ReadableStream} From `transform.readable`.

Connects this {ReadableStream} to the pair of {ReadableStream} and
{WritableStream} provided in the `transform` argument such that the
data from this {ReadableStream} is written in to `transform.writable`,
possibly transformed, then pushed to `transform.readable`. Once the
pipeline is configured, `transform.readable` is returned.

Causes the `readableStream.locked` to be `true` while the pipe operation
is active.

```mjs
import {
  ReadableStream,
  TransformStream,
} from 'node:stream/web';

const stream = new ReadableStream({
  start(controller) {
    controller.enqueue('a');
  },
});

const transform = new TransformStream({
  transform(chunk, controller) {
    controller.enqueue(chunk.toUpperCase());
  },
});

const transformedStream = stream.pipeThrough(transform);

for await (const chunk of transformedStream)
  console.log(chunk);
  // Prints: A
```

```cjs
const {
  ReadableStream,
  TransformStream,
} = require('node:stream/web');

const stream = new ReadableStream({
  start(controller) {
    controller.enqueue('a');
  },
});

const transform = new TransformStream({
  transform(chunk, controller) {
    controller.enqueue(chunk.toUpperCase());
  },
});

const transformedStream = stream.pipeThrough(transform);

(async () => {
  for await (const chunk of transformedStream)
    console.log(chunk);
    // Prints: A
})();
```

#### `readableStream.pipeTo(destination[, options])`

<!-- YAML
added: v16.5.0
-->

* `destination` {WritableStream} A {WritableStream} to which this
  `ReadableStream`'s data will be written.
* `options` {Object}
  * `preventAbort` {boolean} When `true`, errors in this `ReadableStream`
    will not cause `destination` to be aborted.
  * `preventCancel` {boolean} When `true`, errors in the `destination`
    will not cause this `ReadableStream` to be canceled.
  * `preventClose` {boolean} When `true`, closing this `ReadableStream`
    does not cause `destination` to be closed.
  * `signal` {AbortSignal} Allows the transfer of data to be canceled
    using an {AbortController}.
* Returns: A promise fulfilled with `undefined`

Causes the `readableStream.locked` to be `true` while the pipe operation
is active.

#### `readableStream.tee()`

<!-- YAML
added: v16.5.0
changes:
  - version:
    - v18.10.0
    - v16.18.0
    pr-url: https://github.com/nodejs/node/pull/44505
    description: Support teeing a readable byte stream.
-->

* Returns: {ReadableStream\[]}

Returns a pair of new {ReadableStream} instances to which this
`ReadableStream`'s data will be forwarded. Each will receive the
same data.

Causes the `readableStream.locked` to be `true`.

#### `readableStream.values([options])`

<!-- YAML
added: v16.5.0
-->

* `options` {Object}
  * `preventCancel` {boolean} When `true`, prevents the {ReadableStream}
    from being closed when the async iterator abruptly terminates.
    **Default**: `false`.

Creates and returns an async iterator usable for consuming this
`ReadableStream`'s data.

Causes the `readableStream.locked` to be `true` while the async iterator
is active.

```mjs
import { Buffer } from 'node:buffer';

const stream = new ReadableStream(getSomeSource());

for await (const chunk of stream.values({ preventCancel: true }))
  console.log(Buffer.from(chunk).toString());
```

#### Async Iteration

The {ReadableStream} object supports the async iterator protocol using
`for await` syntax.

```mjs
import { Buffer } from 'node:buffer';

const stream = new ReadableStream(getSomeSource());

for await (const chunk of stream)
  console.log(Buffer.from(chunk).toString());
```

The async iterator will consume the {ReadableStream} until it terminates.

By default, if the async iterator exits early (via either a `break`,
`return`, or a `throw`), the {ReadableStream} will be closed. To prevent
automatic closing of the {ReadableStream}, use the `readableStream.values()`
method to acquire the async iterator and set the `preventCancel` option to
`true`.

The {ReadableStream} must not be locked (that is, it must not have an existing
active reader). During the async iteration, the {ReadableStream} will be locked.

#### Transferring with `postMessage()`

A {ReadableStream} instance can be transferred using a {MessagePort}.

```js
const stream = new ReadableStream(getReadableSourceSomehow());

const { port1, port2 } = new MessageChannel();

port1.onmessage = ({ data }) => {
  data.getReader().read().then((chunk) => {
    console.log(chunk);
  });
};

port2.postMessage(stream, [stream]);
```

### `ReadableStream.from(iterable)`

<!-- YAML
added: v20.6.0
-->

* `iterable` {Iterable} Object implementing the `Symbol.asyncIterator` or
  `Symbol.iterator` iterable protocol.

A utility method that creates a new {ReadableStream} from an iterable.

```mjs
import { ReadableStream } from 'node:stream/web';

async function* asyncIterableGenerator() {
  yield 'a';
  yield 'b';
  yield 'c';
}

const stream = ReadableStream.from(asyncIterableGenerator());

for await (const chunk of stream)
  console.log(chunk); // Prints: 'a', 'b', 'c'
```

```cjs
const { ReadableStream } = require('node:stream/web');

async function* asyncIterableGenerator() {
  yield 'a';
  yield 'b';
  yield 'c';
}

(async () => {
  const stream = ReadableStream.from(asyncIterableGenerator());

  for await (const chunk of stream)
    console.log(chunk); // Prints: 'a', 'b', 'c'
})();
```

### Class: `ReadableStreamDefaultReader`

<!-- YAML
added: v16.5.0
changes:
  - version: v18.0.0
    pr-url: https://github.com/nodejs/node/pull/42225
    description: This class is now exposed on the global object.
-->

By default, calling `readableStream.getReader()` with no arguments
will return an instance of `ReadableStreamDefaultReader`. The default
reader treats the chunks of data passed through the stream as opaque
values, which allows the {ReadableStream} to work with generally any
JavaScript value.

#### `new ReadableStreamDefaultReader(stream)`

<!-- YAML
added: v16.5.0
-->

* `stream` {ReadableStream}

Creates a new {ReadableStreamDefaultReader} that is locked to the
given {ReadableStream}.

#### `readableStreamDefaultReader.cancel([reason])`

<!-- YAML
added: v16.5.0
-->

* `reason` {any}
* Returns: A promise fulfilled with `undefined`.

Cancels the {ReadableStream} and returns a promise that is fulfilled
when the underlying stream has been canceled.

#### `readableStreamDefaultReader.closed`

<!-- YAML
added: v16.5.0
-->

* Type: {Promise} Fulfilled with `undefined` when the associated
  {ReadableStream} is closed or rejected if the stream errors or the reader's
  lock is released before the stream finishes closing.

#### `readableStreamDefaultReader.read()`

<!-- YAML
added: v16.5.0
-->

* Returns: A promise fulfilled with an object:
  * `value` {any}
  * `done` {boolean}

Requests the next chunk of data from the underlying {ReadableStream}
and returns a promise that is fulfilled with the data once it is
available.

#### `readableStreamDefaultReader.releaseLock()`

<!-- YAML
added: v16.5.0
-->

Releases this reader's lock on the underlying {ReadableStream}.

### Class: `ReadableStreamBYOBReader`

<!-- YAML
added: v16.5.0
changes:
  - version: v18.0.0
    pr-url: https://github.com/nodejs/node/pull/42225
    description: This class is now exposed on the global object.
-->

The `ReadableStreamBYOBReader` is an alternative consumer for
byte-oriented {ReadableStream}s (those that are created with
`underlyingSource.type` set equal to `'bytes'` when the
`ReadableStream` was created).

The `BYOB` is short for "bring your own buffer". This is a
pattern that allows for more efficient reading of byte-oriented
data that avoids extraneous copying.

```mjs
import {
  open,
} from 'node:fs/promises';

import {
  ReadableStream,
} from 'node:stream/web';

import { Buffer } from 'node:buffer';

class Source {
  type = 'bytes';
  autoAllocateChunkSize = 1024;

  async start(controller) {
    this.file = await open(new URL(import.meta.url));
    this.controller = controller;
  }

  async pull(controller) {
    const view = controller.byobRequest?.view;
    const {
      bytesRead,
    } = await this.file.read({
      buffer: view,
      offset: view.byteOffset,
      length: view.byteLength,
    });

    if (bytesRead === 0) {
      await this.file.close();
      this.controller.close();
    }
    controller.byobRequest.respond(bytesRead);
  }
}

const stream = new ReadableStream(new Source());

async function read(stream) {
  const reader = stream.getReader({ mode: 'byob' });

  const chunks = [];
  let result;
  do {
    result = await reader.read(Buffer.alloc(100));
    if (result.value !== undefined)
      chunks.push(Buffer.from(result.value));
  } while (!result.done);

  return Buffer.concat(chunks);
}

const data = await read(stream);
console.log(Buffer.from(data).toString());
```

#### `new ReadableStreamBYOBReader(stream)`

<!-- YAML
added: v16.5.0
-->

* `stream` {ReadableStream}

Creates a new `ReadableStreamBYOBReader` that is locked to the
given {ReadableStream}.

#### `readableStreamBYOBReader.cancel([reason])`

<!-- YAML
added: v16.5.0
-->

* `reason` {any}
* Returns: A promise fulfilled with `undefined`.

Cancels the {ReadableStream} and returns a promise that is fulfilled
when the underlying stream has been canceled.

#### `readableStreamBYOBReader.closed`

<!-- YAML
added: v16.5.0
-->

* Type: {Promise} Fulfilled with `undefined` when the associated
  {ReadableStream} is closed or rejected if the stream errors or the reader's
  lock is released before the stream finishes closing.

#### `readableStreamBYOBReader.read(view[, options])`

<!-- YAML
added: v16.5.0
changes:
  - version: v21.7.0
    pr-url: https://github.com/nodejs/node/pull/50888
    description: Added `min` option.
-->

* `view` {Buffer|TypedArray|DataView}
* `options` {Object}
  * `min` {number} When set, the returned promise will only be
    fulfilled as soon as `min` number of elements are available.
    When not set, the promise fulfills when at least one element
    is available.
* Returns: A promise fulfilled with an object:
  * `value` {TypedArray|DataView}
  * `done` {boolean}

Requests the next chunk of data from the underlying {ReadableStream}
and returns a promise that is fulfilled with the data once it is
available.

Do not pass a pooled {Buffer} object instance in to this method.
Pooled `Buffer` objects are created using `Buffer.allocUnsafe()`,
or `Buffer.from()`, or are often returned by various `node:fs` module
callbacks. These types of `Buffer`s use a shared underlying
{ArrayBuffer} object that contains all of the data from all of
the pooled `Buffer` instances. When a `Buffer`, {TypedArray},
or {DataView} is passed in to `readableStreamBYOBReader.read()`,
the view's underlying `ArrayBuffer` is _detached_, invalidating
all existing views that may exist on that `ArrayBuffer`. This
can have disastrous consequences for your application.

#### `readableStreamBYOBReader.releaseLock()`

<!-- YAML
added: v16.5.0
-->

Releases this reader's lock on the underlying {ReadableStream}.

### Class: `ReadableStreamDefaultController`

<!-- YAML
added: v16.5.0
-->

Every {ReadableStream} has a controller that is responsible for
the internal state and management of the stream's queue. The
`ReadableStreamDefaultController` is the default controller
implementation for `ReadableStream`s that are not byte-oriented.

#### `readableStreamDefaultController.close()`

<!-- YAML
added: v16.5.0
-->

Closes the {ReadableStream} to which this controller is associated.

#### `readableStreamDefaultController.desiredSize`

<!-- YAML
added: v16.5.0
-->

* Type: {number}

Returns the amount of data remaining to fill the {ReadableStream}'s
queue.

#### `readableStreamDefaultController.enqueue([chunk])`

<!-- YAML
added: v16.5.0
-->

* `chunk` {any}

Appends a new chunk of data to the {ReadableStream}'s queue.

#### `readableStreamDefaultController.error([error])`

<!-- YAML
added: v16.5.0
-->

* `error` {any}

Signals an error that causes the {ReadableStream} to error and close.

### Class: `ReadableByteStreamController`

<!-- YAML
added: v16.5.0
changes:
  - version: v18.10.0
    pr-url: https://github.com/nodejs/node/pull/44702
    description: Support handling a BYOB pull request from a released reader.
-->

Every {ReadableStream} has a controller that is responsible for
the internal state and management of the stream's queue. The
`ReadableByteStreamController` is for byte-oriented `ReadableStream`s.

#### `readableByteStreamController.byobRequest`

<!-- YAML
added: v16.5.0
-->

* Type: {ReadableStreamBYOBRequest}

#### `readableByteStreamController.close()`

<!-- YAML
added: v16.5.0
-->

Closes the {ReadableStream} to which this controller is associated.

#### `readableByteStreamController.desiredSize`

<!-- YAML
added: v16.5.0
-->

* Type: {number}

Returns the amount of data remaining to fill the {ReadableStream}'s
queue.

#### `readableByteStreamController.enqueue(chunk)`

<!-- YAML
added: v16.5.0
-->

* `chunk`: {Buffer|TypedArray|DataView}

Appends a new chunk of data to the {ReadableStream}'s queue.

#### `readableByteStreamController.error([error])`

<!-- YAML
added: v16.5.0
-->

* `error` {any}

Signals an error that causes the {ReadableStream} to error and close.

### Class: `ReadableStreamBYOBRequest`

<!-- YAML
added: v16.5.0
changes:
  - version: v18.0.0
    pr-url: https://github.com/nodejs/node/pull/42225
    description: This class is now exposed on the global object.
-->

When using `ReadableByteStreamController` in byte-oriented
streams, and when using the `ReadableStreamBYOBReader`,
the `readableByteStreamController.byobRequest` property
provides access to a `ReadableStreamBYOBRequest` instance
that represents the current read request. The object
is used to gain access to the `ArrayBuffer`/`TypedArray`
that has been provided for the read request to fill,
and provides methods for signaling that the data has
been provided.

#### `readableStreamBYOBRequest.respond(bytesWritten)`

<!-- YAML
added: v16.5.0
-->

* `bytesWritten` {number}

Signals that a `bytesWritten` number of bytes have been written
to `readableStreamBYOBRequest.view`.

#### `readableStreamBYOBRequest.respondWithNewView(view)`

<!-- YAML
added: v16.5.0
-->

* `view` {Buffer|TypedArray|DataView}

Signals that the request has been fulfilled with bytes written
to a new `Buffer`, `TypedArray`, or `DataView`.

#### `readableStreamBYOBRequest.view`

<!-- YAML
added: v16.5.0
-->

* Type: {Buffer|TypedArray|DataView}

### Class: `WritableStream`

<!-- YAML
added: v16.5.0
changes:
  - version: v18.0.0
    pr-url: https://github.com/nodejs/node/pull/42225
    description: This class is now exposed on the global object.
-->

The `WritableStream` is a destination to which stream data is sent.

```mjs
import {
  WritableStream,
} from 'node:stream/web';

const stream = new WritableStream({
  write(chunk) {
    console.log(chunk);
  },
});

await stream.getWriter().write('Hello World');
```

#### `new WritableStream([underlyingSink[, strategy]])`

<!-- YAML
added: v16.5.0
-->

* `underlyingSink` {Object}
  * `start` {Function} A user-defined function that is invoked immediately when
    the `WritableStream` is created.
    * `controller` {WritableStreamDefaultController}
    * Returns: `undefined` or a promise fulfilled with `undefined`.
  * `write` {Function} A user-defined function that is invoked when a chunk of
    data has been written to the `WritableStream`.
    * `chunk` {any}
    * `controller` {WritableStreamDefaultController}
    * Returns: A promise fulfilled with `undefined`.
  * `close` {Function} A user-defined function that is called when the
    `WritableStream` is closed.
    * Returns: A promise fulfilled with `undefined`.
  * `abort` {Function} A user-defined function that is called to abruptly close
    the `WritableStream`.
    * `reason` {any}
    * Returns: A promise fulfilled with `undefined`.
  * `type` {any} The `type` option is reserved for future use and _must_ be
    undefined.
* `strategy` {Object}
  * `highWaterMark` {number} The maximum internal queue size before backpressure
    is applied.
  * `size` {Function} A user-defined function used to identify the size of each
    chunk of data.
    * `chunk` {any}
    * Returns: {number}

#### `writableStream.abort([reason])`

<!-- YAML
added: v16.5.0
-->

* `reason` {any}
* Returns: A promise fulfilled with `undefined`.

Abruptly terminates the `WritableStream`. All queued writes will be
canceled with their associated promises rejected.

#### `writableStream.close()`

<!-- YAML
added: v16.5.0
-->

* Returns: A promise fulfilled with `undefined`.

Closes the `WritableStream` when no additional writes are expected.

#### `writableStream.getWriter()`

<!-- YAML
added: v16.5.0
-->

* Returns: {WritableStreamDefaultWriter}

Creates and returns a new writer instance that can be used to write
data into the `WritableStream`.

#### `writableStream.locked`

<!-- YAML
added: v16.5.0
-->

* Type: {boolean}

The `writableStream.locked` property is `false` by default, and is
switched to `true` while there is an active writer attached to this
`WritableStream`.

#### Transferring with postMessage()

A {WritableStream} instance can be transferred using a {MessagePort}.

```js
const stream = new WritableStream(getWritableSinkSomehow());

const { port1, port2 } = new MessageChannel();

port1.onmessage = ({ data }) => {
  data.getWriter().write('hello');
};

port2.postMessage(stream, [stream]);
```

### Class: `WritableStreamDefaultWriter`

<!-- YAML
added: v16.5.0
changes:
  - version: v18.0.0
    pr-url: https://github.com/nodejs/node/pull/42225
    description: This class is now exposed on the global object.
-->

#### `new WritableStreamDefaultWriter(stream)`

<!-- YAML
added: v16.5.0
-->

* `stream` {WritableStream}

Creates a new `WritableStreamDefaultWriter` that is locked to the given
`WritableStream`.

#### `writableStreamDefaultWriter.abort([reason])`

<!-- YAML
added: v16.5.0
-->

* `reason` {any}
* Returns: A promise fulfilled with `undefined`.

Abruptly terminates the `WritableStream`. All queued writes will be
canceled with their associated promises rejected.

#### `writableStreamDefaultWriter.close()`

<!-- YAML
added: v16.5.0
-->

* Returns: A promise fulfilled with `undefined`.

Closes the `WritableStream` when no additional writes are expected.

#### `writableStreamDefaultWriter.closed`

<!-- YAML
added: v16.5.0
-->

* Type: {Promise} Fulfilled with `undefined` when the associated
  {WritableStream} is closed or rejected if the stream errors or the writer's
  lock is released before the stream finishes closing.

#### `writableStreamDefaultWriter.desiredSize`

<!-- YAML
added: v16.5.0
-->

* Type: {number}

The amount of data required to fill the {WritableStream}'s queue.

#### `writableStreamDefaultWriter.ready`

<!-- YAML
added: v16.5.0
-->

* Type: {Promise} Fulfilled with `undefined` when the writer is ready
  to be used.

#### `writableStreamDefaultWriter.releaseLock()`

<!-- YAML
added: v16.5.0
-->

Releases this writer's lock on the underlying {ReadableStream}.

#### `writableStreamDefaultWriter.write([chunk])`

<!-- YAML
added: v16.5.0
-->

* `chunk`: {any}
* Returns: A promise fulfilled with `undefined`.

Appends a new chunk of data to the {WritableStream}'s queue.

### Class: `WritableStreamDefaultController`

<!-- YAML
added: v16.5.0
changes:
  - version: v18.0.0
    pr-url: https://github.com/nodejs/node/pull/42225
    description: This class is now exposed on the global object.
-->

The `WritableStreamDefaultController` manages the {WritableStream}'s
internal state.

#### `writableStreamDefaultController.error([error])`

<!-- YAML
added: v16.5.0
-->

* `error` {any}

Called by user-code to signal that an error has occurred while processing
the `WritableStream` data. When called, the {WritableStream} will be aborted,
with currently pending writes canceled.

#### `writableStreamDefaultController.signal`

* Type: {AbortSignal} An `AbortSignal` that can be used to cancel pending
  write or close operations when a {WritableStream} is aborted.

### Class: `TransformStream`

<!-- YAML
added: v16.5.0
changes:
  - version: v18.0.0
    pr-url: https://github.com/nodejs/node/pull/42225
    description: This class is now exposed on the global object.
-->

A `TransformStream` consists of a {ReadableStream} and a {WritableStream} that
are connected such that the data written to the `WritableStream` is received,
and potentially transformed, before being pushed into the `ReadableStream`'s
queue.

```mjs
import {
  TransformStream,
} from 'node:stream/web';

const transform = new TransformStream({
  transform(chunk, controller) {
    controller.enqueue(chunk.toUpperCase());
  },
});

await Promise.all([
  transform.writable.getWriter().write('A'),
  transform.readable.getReader().read(),
]);
```

#### `new TransformStream([transformer[, writableStrategy[, readableStrategy]]])`

<!-- YAML
added: v16.5.0
-->

* `transformer` {Object}
  * `start` {Function} A user-defined function that is invoked immediately when
    the `TransformStream` is created.
    * `controller` {TransformStreamDefaultController}
    * Returns: `undefined` or a promise fulfilled with `undefined`
  * `transform` {Function} A user-defined function that receives, and
    potentially modifies, a chunk of data written to `transformStream.writable`,
    before forwarding that on to `transformStream.readable`.
    * `chunk` {any}
    * `controller` {TransformStreamDefaultController}
    * Returns: A promise fulfilled with `undefined`.
  * `flush` {Function} A user-defined function that is called immediately before
    the writable side of the `TransformStream` is closed, signaling the end of
    the transformation process.
    * `controller` {TransformStreamDefaultController}
    * Returns: A promise fulfilled with `undefined`.
  * `readableType` {any} the `readableType` option is reserved for future use
    and _must_ be `undefined`.
  * `writableType` {any} the `writableType` option is reserved for future use
    and _must_ be `undefined`.
* `writableStrategy` {Object}
  * `highWaterMark` {number} The maximum internal queue size before backpressure
    is applied.
  * `size` {Function} A user-defined function used to identify the size of each
    chunk of data.
    * `chunk` {any}
    * Returns: {number}
* `readableStrategy` {Object}
  * `highWaterMark` {number} The maximum internal queue size before backpressure
    is applied.
  * `size` {Function} A user-defined function used to identify the size of each
    chunk of data.
    * `chunk` {any}
    * Returns: {number}

#### `transformStream.readable`

<!-- YAML
added: v16.5.0
-->

* Type: {ReadableStream}

#### `transformStream.writable`

<!-- YAML
added: v16.5.0
-->

* Type: {WritableStream}

#### Transferring with postMessage()

A {TransformStream} instance can be transferred using a {MessagePort}.

```js
const stream = new TransformStream();

const { port1, port2 } = new MessageChannel();

port1.onmessage = ({ data }) => {
  const { writable, readable } = data;
  // ...
};

port2.postMessage(stream, [stream]);
```

### Class: `TransformStreamDefaultController`

<!-- YAML
added: v16.5.0
changes:
  - version: v18.0.0
    pr-url: https://github.com/nodejs/node/pull/42225
    description: This class is now exposed on the global object.
-->

The `TransformStreamDefaultController` manages the internal state
of the `TransformStream`.

#### `transformStreamDefaultController.desiredSize`

<!-- YAML
added: v16.5.0
-->

* Type: {number}

The amount of data required to fill the readable side's queue.

#### `transformStreamDefaultController.enqueue([chunk])`

<!-- YAML
added: v16.5.0
-->

* `chunk` {any}

Appends a chunk of data to the readable side's queue.

#### `transformStreamDefaultController.error([reason])`

<!-- YAML
added: v16.5.0
-->

* `reason` {any}

Signals to both the readable and writable side that an error has occurred
while processing the transform data, causing both sides to be abruptly
closed.

#### `transformStreamDefaultController.terminate()`

<!-- YAML
added: v16.5.0
-->

Closes the readable side of the transport and causes the writable side
to be abruptly closed with an error.

### Class: `ByteLengthQueuingStrategy`

<!-- YAML
added: v16.5.0
changes:
  - version: v18.0.0
    pr-url: https://github.com/nodejs/node/pull/42225
    description: This class is now exposed on the global object.
-->

#### `new ByteLengthQueuingStrategy(init)`

<!-- YAML
added: v16.5.0
-->

* `init` {Object}
  * `highWaterMark` {number}

#### `byteLengthQueuingStrategy.highWaterMark`

<!-- YAML
added: v16.5.0
-->

* Type: {number}

#### `byteLengthQueuingStrategy.size`

<!-- YAML
added: v16.5.0
-->

* Type: {Function}
  * `chunk` {any}
  * Returns: {number}

### Class: `CountQueuingStrategy`

<!-- YAML
added: v16.5.0
changes:
  - version: v18.0.0
    pr-url: https://github.com/nodejs/node/pull/42225
    description: This class is now exposed on the global object.
-->

#### `new CountQueuingStrategy(init)`

<!-- YAML
added: v16.5.0
-->

* `init` {Object}
  * `highWaterMark` {number}

#### `countQueuingStrategy.highWaterMark`

<!-- YAML
added: v16.5.0
-->

* Type: {number}

#### `countQueuingStrategy.size`

<!-- YAML
added: v16.5.0
-->

* Type: {Function}
  * `chunk` {any}
  * Returns: {number}

### Class: `TextEncoderStream`

<!-- YAML
added: v16.6.0
changes:
  - version: v18.0.0
    pr-url: https://github.com/nodejs/node/pull/42225
    description: This class is now exposed on the global object.
-->

#### `new TextEncoderStream()`

<!-- YAML
added: v16.6.0
-->

Creates a new `TextEncoderStream` instance.

#### `textEncoderStream.encoding`

<!-- YAML
added: v16.6.0
-->

* Type: {string}

The encoding supported by the `TextEncoderStream` instance.

#### `textEncoderStream.readable`

<!-- YAML
added: v16.6.0
-->

* Type: {ReadableStream}

#### `textEncoderStream.writable`

<!-- YAML
added: v16.6.0
-->

* Type: {WritableStream}

### Class: `TextDecoderStream`

<!-- YAML
added: v16.6.0
changes:
  - version: v18.0.0
    pr-url: https://github.com/nodejs/node/pull/42225
    description: This class is now exposed on the global object.
-->

#### `new TextDecoderStream([encoding[, options]])`

<!-- YAML
added: v16.6.0
-->

* `encoding` {string} Identifies the `encoding` that this `TextDecoder` instance
  supports. **Default:** `'utf-8'`.
* `options` {Object}
  * `fatal` {boolean} `true` if decoding failures are fatal.
  * `ignoreBOM` {boolean} When `true`, the `TextDecoderStream` will include the
    byte order mark in the decoded result. When `false`, the byte order mark
    will be removed from the output. This option is only used when `encoding` is
    `'utf-8'`, `'utf-16be'`, or `'utf-16le'`. **Default:** `false`.

Creates a new `TextDecoderStream` instance.

#### `textDecoderStream.encoding`

<!-- YAML
added: v16.6.0
-->

* Type: {string}

The encoding supported by the `TextDecoderStream` instance.

#### `textDecoderStream.fatal`

<!-- YAML
added: v16.6.0
-->

* Type: {boolean}

The value will be `true` if decoding errors result in a `TypeError` being
thrown.

#### `textDecoderStream.ignoreBOM`

<!-- YAML
added: v16.6.0
-->

* Type: {boolean}

The value will be `true` if the decoding result will include the byte order
mark.

#### `textDecoderStream.readable`

<!-- YAML
added: v16.6.0
-->

* Type: {ReadableStream}

#### `textDecoderStream.writable`

<!-- YAML
added: v16.6.0
-->

* Type: {WritableStream}

### Class: `CompressionStream`

<!-- YAML
added: v17.0.0
changes:
  - version: v18.0.0
    pr-url: https://github.com/nodejs/node/pull/42225
    description: This class is now exposed on the global object.
-->

#### `new CompressionStream(format)`

<!-- YAML
added: v17.0.0
changes:
  - version:
    - v21.2.0
    - v20.12.0
    pr-url: https://github.com/nodejs/node/pull/50097
    description: format now accepts `deflate-raw` value.
-->

* `format` {string} One of `'deflate'`, `'deflate-raw'`, or `'gzip'`.

#### `compressionStream.readable`

<!-- YAML
added: v17.0.0
-->

* Type: {ReadableStream}

#### `compressionStream.writable`

<!-- YAML
added: v17.0.0
-->

* Type: {WritableStream}

### Class: `DecompressionStream`

<!-- YAML
added: v17.0.0
changes:
  - version: v18.0.0
    pr-url: https://github.com/nodejs/node/pull/42225
    description: This class is now exposed on the global object.
-->

#### `new DecompressionStream(format)`

<!-- YAML
added: v17.0.0
changes:
  - version:
    - v21.2.0
    - v20.12.0
    pr-url: https://github.com/nodejs/node/pull/50097
    description: format now accepts `deflate-raw` value.
-->

* `format` {string} One of `'deflate'`, `'deflate-raw'`, or `'gzip'`.

#### `decompressionStream.readable`

<!-- YAML
added: v17.0.0
-->

* Type: {ReadableStream}

#### `decompressionStream.writable`

<!-- YAML
added: v17.0.0
-->

* Type: {WritableStream}

### Utility Consumers

<!-- YAML
added: v16.7.0
-->

The utility consumer functions provide common options for consuming
streams.

They are accessed using:

```mjs
import {
  arrayBuffer,
  blob,
  buffer,
  json,
  text,
} from 'node:stream/consumers';
```

```cjs
const {
  arrayBuffer,
  blob,
  buffer,
  json,
  text,
} = require('node:stream/consumers');
```

#### `streamConsumers.arrayBuffer(stream)`

<!-- YAML
added: v16.7.0
-->

* `stream` {ReadableStream|stream.Readable|AsyncIterator}
* Returns: {Promise} Fulfills with an `ArrayBuffer` containing the full
  contents of the stream.

```mjs
import { arrayBuffer } from 'node:stream/consumers';
import { Readable } from 'node:stream';
import { TextEncoder } from 'node:util';

const encoder = new TextEncoder();
const dataArray = encoder.encode('hello world from consumers!');

const readable = Readable.from(dataArray);
const data = await arrayBuffer(readable);
console.log(`from readable: ${data.byteLength}`);
// Prints: from readable: 76
```

```cjs
const { arrayBuffer } = require('node:stream/consumers');
const { Readable } = require('node:stream');
const { TextEncoder } = require('node:util');

const encoder = new TextEncoder();
const dataArray = encoder.encode('hello world from consumers!');
const readable = Readable.from(dataArray);
arrayBuffer(readable).then((data) => {
  console.log(`from readable: ${data.byteLength}`);
  // Prints: from readable: 76
});
```

#### `streamConsumers.blob(stream)`

<!-- YAML
added: v16.7.0
-->

* `stream` {ReadableStream|stream.Readable|AsyncIterator}
* Returns: {Promise} Fulfills with a {Blob} containing the full contents
  of the stream.

```mjs
import { blob } from 'node:stream/consumers';

const dataBlob = new Blob(['hello world from consumers!']);

const readable = dataBlob.stream();
const data = await blob(readable);
console.log(`from readable: ${data.size}`);
// Prints: from readable: 27
```

```cjs
const { blob } = require('node:stream/consumers');

const dataBlob = new Blob(['hello world from consumers!']);

const readable = dataBlob.stream();
blob(readable).then((data) => {
  console.log(`from readable: ${data.size}`);
  // Prints: from readable: 27
});
```

#### `streamConsumers.buffer(stream)`

<!-- YAML
added: v16.7.0
-->

* `stream` {ReadableStream|stream.Readable|AsyncIterator}
* Returns: {Promise} Fulfills with a {Buffer} containing the full
  contents of the stream.

```mjs
import { buffer } from 'node:stream/consumers';
import { Readable } from 'node:stream';
import { Buffer } from 'node:buffer';

const dataBuffer = Buffer.from('hello world from consumers!');

const readable = Readable.from(dataBuffer);
const data = await buffer(readable);
console.log(`from readable: ${data.length}`);
// Prints: from readable: 27
```

```cjs
const { buffer } = require('node:stream/consumers');
const { Readable } = require('node:stream');
const { Buffer } = require('node:buffer');

const dataBuffer = Buffer.from('hello world from consumers!');

const readable = Readable.from(dataBuffer);
buffer(readable).then((data) => {
  console.log(`from readable: ${data.length}`);
  // Prints: from readable: 27
});
```

#### `streamConsumers.json(stream)`

<!-- YAML
added: v16.7.0
-->

* `stream` {ReadableStream|stream.Readable|AsyncIterator}
* Returns: {Promise} Fulfills with the contents of the stream parsed as a
  UTF-8 encoded string that is then passed through `JSON.parse()`.

```mjs
import { json } from 'node:stream/consumers';
import { Readable } from 'node:stream';

const items = Array.from(
  {
    length: 100,
  },
  () => ({
    message: 'hello world from consumers!',
  }),
);

const readable = Readable.from(JSON.stringify(items));
const data = await json(readable);
console.log(`from readable: ${data.length}`);
// Prints: from readable: 100
```

```cjs
const { json } = require('node:stream/consumers');
const { Readable } = require('node:stream');

const items = Array.from(
  {
    length: 100,
  },
  () => ({
    message: 'hello world from consumers!',
  }),
);

const readable = Readable.from(JSON.stringify(items));
json(readable).then((data) => {
  console.log(`from readable: ${data.length}`);
  // Prints: from readable: 100
});
```

#### `streamConsumers.text(stream)`

<!-- YAML
added: v16.7.0
-->

* `stream` {ReadableStream|stream.Readable|AsyncIterator}
* Returns: {Promise} Fulfills with the contents of the stream parsed as a
  UTF-8 encoded string.

```mjs
import { text } from 'node:stream/consumers';
import { Readable } from 'node:stream';

const readable = Readable.from('Hello world from consumers!');
const data = await text(readable);
console.log(`from readable: ${data.length}`);
// Prints: from readable: 27
```

```cjs
const { text } = require('node:stream/consumers');
const { Readable } = require('node:stream');

const readable = Readable.from('Hello world from consumers!');
text(readable).then((data) => {
  console.log(`from readable: ${data.length}`);
  // Prints: from readable: 27
});
```

[Streams]: stream.md
[WHATWG Streams Standard]: https://streams.spec.whatwg.org/
