# Diagnostics Channel

<!-- YAML
added:
  - v15.1.0
  - v14.17.0
changes:
  - version:
      - v19.2.0
      - v18.13.0
    pr-url: https://github.com/nodejs/node/pull/45290
    description: diagnostics_channel is now Stable.
-->

<!--introduced_in=v15.1.0-->

> Stability: 2 - Stable

<!-- source_link=lib/diagnostics_channel.js -->

The `node:diagnostics_channel` module provides an API to create named channels
to report arbitrary message data for diagnostics purposes.

It can be accessed using:

```mjs
import diagnostics_channel from 'node:diagnostics_channel';
```

```cjs
const diagnostics_channel = require('node:diagnostics_channel');
```

It is intended that a module writer wanting to report diagnostics messages
will create one or many top-level channels to report messages through.
Channels may also be acquired at runtime but it is not encouraged
due to the additional overhead of doing so. Channels may be exported for
convenience, but as long as the name is known it can be acquired anywhere.

If you intend for your module to produce diagnostics data for others to
consume it is recommended that you include documentation of what named
channels are used along with the shape of the message data. Channel names
should generally include the module name to avoid collisions with data from
other modules.

## Public API

### Overview

Following is a simple overview of the public API.

```mjs
import diagnostics_channel from 'node:diagnostics_channel';

// Get a reusable channel object
const channel = diagnostics_channel.channel('my-channel');

function onMessage(message, name) {
  // Received data
}

// Subscribe to the channel
diagnostics_channel.subscribe('my-channel', onMessage);

// Check if the channel has an active subscriber
if (channel.hasSubscribers) {
  // Publish data to the channel
  channel.publish({
    some: 'data',
  });
}

// Unsubscribe from the channel
diagnostics_channel.unsubscribe('my-channel', onMessage);
```

```cjs
const diagnostics_channel = require('node:diagnostics_channel');

// Get a reusable channel object
const channel = diagnostics_channel.channel('my-channel');

function onMessage(message, name) {
  // Received data
}

// Subscribe to the channel
diagnostics_channel.subscribe('my-channel', onMessage);

// Check if the channel has an active subscriber
if (channel.hasSubscribers) {
  // Publish data to the channel
  channel.publish({
    some: 'data',
  });
}

// Unsubscribe from the channel
diagnostics_channel.unsubscribe('my-channel', onMessage);
```

#### `diagnostics_channel.hasSubscribers(name)`

<!-- YAML
added:
 - v15.1.0
 - v14.17.0
-->

* `name` {string|symbol} The channel name
* Returns: {boolean} If there are active subscribers

Check if there are active subscribers to the named channel. This is helpful if
the message you want to send might be expensive to prepare.

This API is optional but helpful when trying to publish messages from very
performance-sensitive code.

```mjs
import diagnostics_channel from 'node:diagnostics_channel';

if (diagnostics_channel.hasSubscribers('my-channel')) {
  // There are subscribers, prepare and publish message
}
```

```cjs
const diagnostics_channel = require('node:diagnostics_channel');

if (diagnostics_channel.hasSubscribers('my-channel')) {
  // There are subscribers, prepare and publish message
}
```

#### `diagnostics_channel.channel(name)`

<!-- YAML
added:
 - v15.1.0
 - v14.17.0
-->

* `name` {string|symbol} The channel name
* Returns: {Channel} The named channel object

This is the primary entry-point for anyone wanting to publish to a named
channel. It produces a channel object which is optimized to reduce overhead at
publish time as much as possible.

```mjs
import diagnostics_channel from 'node:diagnostics_channel';

const channel = diagnostics_channel.channel('my-channel');
```

```cjs
const diagnostics_channel = require('node:diagnostics_channel');

const channel = diagnostics_channel.channel('my-channel');
```

#### `diagnostics_channel.subscribe(name, onMessage)`

<!-- YAML
added:
 - v18.7.0
 - v16.17.0
-->

* `name` {string|symbol} The channel name
* `onMessage` {Function} The handler to receive channel messages
  * `message` {any} The message data
  * `name` {string|symbol} The name of the channel

Register a message handler to subscribe to this channel. This message handler
will be run synchronously whenever a message is published to the channel. Any
errors thrown in the message handler will trigger an [`'uncaughtException'`][].

```mjs
import diagnostics_channel from 'node:diagnostics_channel';

diagnostics_channel.subscribe('my-channel', (message, name) => {
  // Received data
});
```

```cjs
const diagnostics_channel = require('node:diagnostics_channel');

diagnostics_channel.subscribe('my-channel', (message, name) => {
  // Received data
});
```

#### `diagnostics_channel.unsubscribe(name, onMessage)`

<!-- YAML
added:
 - v18.7.0
 - v16.17.0
-->

* `name` {string|symbol} The channel name
* `onMessage` {Function} The previous subscribed handler to remove
* Returns: {boolean} `true` if the handler was found, `false` otherwise.

Remove a message handler previously registered to this channel with
[`diagnostics_channel.subscribe(name, onMessage)`][].

```mjs
import diagnostics_channel from 'node:diagnostics_channel';

function onMessage(message, name) {
  // Received data
}

diagnostics_channel.subscribe('my-channel', onMessage);

diagnostics_channel.unsubscribe('my-channel', onMessage);
```

```cjs
const diagnostics_channel = require('node:diagnostics_channel');

function onMessage(message, name) {
  // Received data
}

diagnostics_channel.subscribe('my-channel', onMessage);

diagnostics_channel.unsubscribe('my-channel', onMessage);
```

#### `diagnostics_channel.tracingChannel(nameOrChannels)`

<!-- YAML
added:
 - v19.9.0
 - v18.19.0
-->

> Stability: 1 - Experimental

* `nameOrChannels` {string|TracingChannel} Channel name or
  object containing all the [TracingChannel Channels][]
* Returns: {TracingChannel} Collection of channels to trace with

Creates a [`TracingChannel`][] wrapper for the given
[TracingChannel Channels][]. If a name is given, the corresponding tracing
channels will be created in the form of `tracing:${name}:${eventType}` where
`eventType` corresponds to the types of [TracingChannel Channels][].

```mjs
import diagnostics_channel from 'node:diagnostics_channel';

const channelsByName = diagnostics_channel.tracingChannel('my-channel');

// or...

const channelsByCollection = diagnostics_channel.tracingChannel({
  start: diagnostics_channel.channel('tracing:my-channel:start'),
  end: diagnostics_channel.channel('tracing:my-channel:end'),
  asyncStart: diagnostics_channel.channel('tracing:my-channel:asyncStart'),
  asyncEnd: diagnostics_channel.channel('tracing:my-channel:asyncEnd'),
  error: diagnostics_channel.channel('tracing:my-channel:error'),
});
```

```cjs
const diagnostics_channel = require('node:diagnostics_channel');

const channelsByName = diagnostics_channel.tracingChannel('my-channel');

// or...

const channelsByCollection = diagnostics_channel.tracingChannel({
  start: diagnostics_channel.channel('tracing:my-channel:start'),
  end: diagnostics_channel.channel('tracing:my-channel:end'),
  asyncStart: diagnostics_channel.channel('tracing:my-channel:asyncStart'),
  asyncEnd: diagnostics_channel.channel('tracing:my-channel:asyncEnd'),
  error: diagnostics_channel.channel('tracing:my-channel:error'),
});
```

### Class: `Channel`

<!-- YAML
added:
 - v15.1.0
 - v14.17.0
-->

The class `Channel` represents an individual named channel within the data
pipeline. It is used to track subscribers and to publish messages when there
are subscribers present. It exists as a separate object to avoid channel
lookups at publish time, enabling very fast publish speeds and allowing
for heavy use while incurring very minimal cost. Channels are created with
[`diagnostics_channel.channel(name)`][], constructing a channel directly
with `new Channel(name)` is not supported.

#### `channel.hasSubscribers`

<!-- YAML
added:
 - v15.1.0
 - v14.17.0
-->

* Returns: {boolean} If there are active subscribers

Check if there are active subscribers to this channel. This is helpful if
the message you want to send might be expensive to prepare.

This API is optional but helpful when trying to publish messages from very
performance-sensitive code.

```mjs
import diagnostics_channel from 'node:diagnostics_channel';

const channel = diagnostics_channel.channel('my-channel');

if (channel.hasSubscribers) {
  // There are subscribers, prepare and publish message
}
```

```cjs
const diagnostics_channel = require('node:diagnostics_channel');

const channel = diagnostics_channel.channel('my-channel');

if (channel.hasSubscribers) {
  // There are subscribers, prepare and publish message
}
```

#### `channel.publish(message)`

<!-- YAML
added:
 - v15.1.0
 - v14.17.0
-->

* `message` {any} The message to send to the channel subscribers

Publish a message to any subscribers to the channel. This will trigger
message handlers synchronously so they will execute within the same context.

```mjs
import diagnostics_channel from 'node:diagnostics_channel';

const channel = diagnostics_channel.channel('my-channel');

channel.publish({
  some: 'message',
});
```

```cjs
const diagnostics_channel = require('node:diagnostics_channel');

const channel = diagnostics_channel.channel('my-channel');

channel.publish({
  some: 'message',
});
```

#### `channel.subscribe(onMessage)`

<!-- YAML
added:
 - v15.1.0
 - v14.17.0
deprecated:
 - v18.7.0
 - v16.17.0
-->

> Stability: 0 - Deprecated: Use [`diagnostics_channel.subscribe(name, onMessage)`][]

* `onMessage` {Function} The handler to receive channel messages
  * `message` {any} The message data
  * `name` {string|symbol} The name of the channel

Register a message handler to subscribe to this channel. This message handler
will be run synchronously whenever a message is published to the channel. Any
errors thrown in the message handler will trigger an [`'uncaughtException'`][].

```mjs
import diagnostics_channel from 'node:diagnostics_channel';

const channel = diagnostics_channel.channel('my-channel');

channel.subscribe((message, name) => {
  // Received data
});
```

```cjs
const diagnostics_channel = require('node:diagnostics_channel');

const channel = diagnostics_channel.channel('my-channel');

channel.subscribe((message, name) => {
  // Received data
});
```

#### `channel.unsubscribe(onMessage)`

<!-- YAML
added:
 - v15.1.0
 - v14.17.0
deprecated:
 - v18.7.0
 - v16.17.0
changes:
  - version:
    - v17.1.0
    - v16.14.0
    - v14.19.0
    pr-url: https://github.com/nodejs/node/pull/40433
    description: Added return value. Added to channels without subscribers.
-->

> Stability: 0 - Deprecated: Use [`diagnostics_channel.unsubscribe(name, onMessage)`][]

* `onMessage` {Function} The previous subscribed handler to remove
* Returns: {boolean} `true` if the handler was found, `false` otherwise.

Remove a message handler previously registered to this channel with
[`channel.subscribe(onMessage)`][].

```mjs
import diagnostics_channel from 'node:diagnostics_channel';

const channel = diagnostics_channel.channel('my-channel');

function onMessage(message, name) {
  // Received data
}

channel.subscribe(onMessage);

channel.unsubscribe(onMessage);
```

```cjs
const diagnostics_channel = require('node:diagnostics_channel');

const channel = diagnostics_channel.channel('my-channel');

function onMessage(message, name) {
  // Received data
}

channel.subscribe(onMessage);

channel.unsubscribe(onMessage);
```

#### `channel.bindStore(store[, transform])`

<!-- YAML
added:
 - v19.9.0
 - v18.19.0
-->

> Stability: 1 - Experimental

* `store` {AsyncLocalStorage} The store to which to bind the context data
* `transform` {Function} Transform context data before setting the store context

When [`channel.runStores(context, ...)`][] is called, the given context data
will be applied to any store bound to the channel. If the store has already been
bound the previous `transform` function will be replaced with the new one.
The `transform` function may be omitted to set the given context data as the
context directly.

```mjs
import diagnostics_channel from 'node:diagnostics_channel';
import { AsyncLocalStorage } from 'node:async_hooks';

const store = new AsyncLocalStorage();

const channel = diagnostics_channel.channel('my-channel');

channel.bindStore(store, (data) => {
  return { data };
});
```

```cjs
const diagnostics_channel = require('node:diagnostics_channel');
const { AsyncLocalStorage } = require('node:async_hooks');

const store = new AsyncLocalStorage();

const channel = diagnostics_channel.channel('my-channel');

channel.bindStore(store, (data) => {
  return { data };
});
```

#### `channel.unbindStore(store)`

<!-- YAML
added:
 - v19.9.0
 - v18.19.0
-->

> Stability: 1 - Experimental

* `store` {AsyncLocalStorage} The store to unbind from the channel.
* Returns: {boolean} `true` if the store was found, `false` otherwise.

Remove a message handler previously registered to this channel with
[`channel.bindStore(store)`][].

```mjs
import diagnostics_channel from 'node:diagnostics_channel';
import { AsyncLocalStorage } from 'node:async_hooks';

const store = new AsyncLocalStorage();

const channel = diagnostics_channel.channel('my-channel');

channel.bindStore(store);
channel.unbindStore(store);
```

```cjs
const diagnostics_channel = require('node:diagnostics_channel');
const { AsyncLocalStorage } = require('node:async_hooks');

const store = new AsyncLocalStorage();

const channel = diagnostics_channel.channel('my-channel');

channel.bindStore(store);
channel.unbindStore(store);
```

#### `channel.runStores(context, fn[, thisArg[, ...args]])`

<!-- YAML
added:
 - v19.9.0
 - v18.19.0
-->

> Stability: 1 - Experimental

* `context` {any} Message to send to subscribers and bind to stores
* `fn` {Function} Handler to run within the entered storage context
* `thisArg` {any} The receiver to be used for the function call.
* `...args` {any} Optional arguments to pass to the function.

Applies the given data to any AsyncLocalStorage instances bound to the channel
for the duration of the given function, then publishes to the channel within
the scope of that data is applied to the stores.

If a transform function was given to [`channel.bindStore(store)`][] it will be
applied to transform the message data before it becomes the context value for
the store. The prior storage context is accessible from within the transform
function in cases where context linking is required.

The context applied to the store should be accessible in any async code which
continues from execution which began during the given function, however
there are some situations in which [context loss][] may occur.

```mjs
import diagnostics_channel from 'node:diagnostics_channel';
import { AsyncLocalStorage } from 'node:async_hooks';

const store = new AsyncLocalStorage();

const channel = diagnostics_channel.channel('my-channel');

channel.bindStore(store, (message) => {
  const parent = store.getStore();
  return new Span(message, parent);
});
channel.runStores({ some: 'message' }, () => {
  store.getStore(); // Span({ some: 'message' })
});
```

```cjs
const diagnostics_channel = require('node:diagnostics_channel');
const { AsyncLocalStorage } = require('node:async_hooks');

const store = new AsyncLocalStorage();

const channel = diagnostics_channel.channel('my-channel');

channel.bindStore(store, (message) => {
  const parent = store.getStore();
  return new Span(message, parent);
});
channel.runStores({ some: 'message' }, () => {
  store.getStore(); // Span({ some: 'message' })
});
```

### Class: `TracingChannel`

<!-- YAML
added:
 - v19.9.0
 - v18.19.0
-->

> Stability: 1 - Experimental

The class `TracingChannel` is a collection of [TracingChannel Channels][] which
together express a single traceable action. It is used to formalize and
simplify the process of producing events for tracing application flow.
[`diagnostics_channel.tracingChannel()`][] is used to construct a
`TracingChannel`. As with `Channel` it is recommended to create and reuse a
single `TracingChannel` at the top-level of the file rather than creating them
dynamically.

#### `tracingChannel.subscribe(subscribers)`

<!-- YAML
added:
 - v19.9.0
 - v18.19.0
-->

> Stability: 1 - Experimental

* `subscribers` {Object} Set of [TracingChannel Channels][] subscribers
  * `start` {Function} The [`start` event][] subscriber
  * `end` {Function} The [`end` event][] subscriber
  * `asyncStart` {Function} The [`asyncStart` event][] subscriber
  * `asyncEnd` {Function} The [`asyncEnd` event][] subscriber
  * `error` {Function} The [`error` event][] subscriber

Helper to subscribe a collection of functions to the corresponding channels.
This is the same as calling [`channel.subscribe(onMessage)`][] on each channel
individually.

```mjs
import diagnostics_channel from 'node:diagnostics_channel';

const channels = diagnostics_channel.tracingChannel('my-channel');

channels.subscribe({
  start(message) {
    // Handle start message
  },
  end(message) {
    // Handle end message
  },
  asyncStart(message) {
    // Handle asyncStart message
  },
  asyncEnd(message) {
    // Handle asyncEnd message
  },
  error(message) {
    // Handle error message
  },
});
```

```cjs
const diagnostics_channel = require('node:diagnostics_channel');

const channels = diagnostics_channel.tracingChannel('my-channel');

channels.subscribe({
  start(message) {
    // Handle start message
  },
  end(message) {
    // Handle end message
  },
  asyncStart(message) {
    // Handle asyncStart message
  },
  asyncEnd(message) {
    // Handle asyncEnd message
  },
  error(message) {
    // Handle error message
  },
});
```

#### `tracingChannel.unsubscribe(subscribers)`

<!-- YAML
added:
 - v19.9.0
 - v18.19.0
-->

> Stability: 1 - Experimental

* `subscribers` {Object} Set of [TracingChannel Channels][] subscribers
  * `start` {Function} The [`start` event][] subscriber
  * `end` {Function} The [`end` event][] subscriber
  * `asyncStart` {Function} The [`asyncStart` event][] subscriber
  * `asyncEnd` {Function} The [`asyncEnd` event][] subscriber
  * `error` {Function} The [`error` event][] subscriber
* Returns: {boolean} `true` if all handlers were successfully unsubscribed,
  and `false` otherwise.

Helper to unsubscribe a collection of functions from the corresponding channels.
This is the same as calling [`channel.unsubscribe(onMessage)`][] on each channel
individually.

```mjs
import diagnostics_channel from 'node:diagnostics_channel';

const channels = diagnostics_channel.tracingChannel('my-channel');

channels.unsubscribe({
  start(message) {
    // Handle start message
  },
  end(message) {
    // Handle end message
  },
  asyncStart(message) {
    // Handle asyncStart message
  },
  asyncEnd(message) {
    // Handle asyncEnd message
  },
  error(message) {
    // Handle error message
  },
});
```

```cjs
const diagnostics_channel = require('node:diagnostics_channel');

const channels = diagnostics_channel.tracingChannel('my-channel');

channels.unsubscribe({
  start(message) {
    // Handle start message
  },
  end(message) {
    // Handle end message
  },
  asyncStart(message) {
    // Handle asyncStart message
  },
  asyncEnd(message) {
    // Handle asyncEnd message
  },
  error(message) {
    // Handle error message
  },
});
```

#### `tracingChannel.traceSync(fn[, context[, thisArg[, ...args]]])`

<!-- YAML
added:
 - v19.9.0
 - v18.19.0
-->

> Stability: 1 - Experimental

* `fn` {Function} Function to wrap a trace around
* `context` {Object} Shared object to correlate events through
* `thisArg` {any} The receiver to be used for the function call
* `...args` {any} Optional arguments to pass to the function
* Returns: {any} The return value of the given function

Trace a synchronous function call. This will always produce a [`start` event][]
and [`end` event][] around the execution and may produce an [`error` event][]
if the given function throws an error. This will run the given function using
[`channel.runStores(context, ...)`][] on the `start` channel which ensures all
events should have any bound stores set to match this trace context.

To ensure only correct trace graphs are formed, events will only be published
if subscribers are present prior to starting the trace. Subscriptions which are
added after the trace begins will not receive future events from that trace,
only future traces will be seen.

```mjs
import diagnostics_channel from 'node:diagnostics_channel';

const channels = diagnostics_channel.tracingChannel('my-channel');

channels.traceSync(() => {
  // Do something
}, {
  some: 'thing',
});
```

```cjs
const diagnostics_channel = require('node:diagnostics_channel');

const channels = diagnostics_channel.tracingChannel('my-channel');

channels.traceSync(() => {
  // Do something
}, {
  some: 'thing',
});
```

#### `tracingChannel.tracePromise(fn[, context[, thisArg[, ...args]]])`

<!-- YAML
added:
 - v19.9.0
 - v18.19.0
-->

> Stability: 1 - Experimental

* `fn` {Function} Promise-returning function to wrap a trace around
* `context` {Object} Shared object to correlate trace events through
* `thisArg` {any} The receiver to be used for the function call
* `...args` {any} Optional arguments to pass to the function
* Returns: {Promise} Chained from promise returned by the given function

Trace a promise-returning function call. This will always produce a
[`start` event][] and [`end` event][] around the synchronous portion of the
function execution, and will produce an [`asyncStart` event][] and
[`asyncEnd` event][] when a promise continuation is reached. It may also
produce an [`error` event][] if the given function throws an error or the
returned promise rejects. This will run the given function using
[`channel.runStores(context, ...)`][] on the `start` channel which ensures all
events should have any bound stores set to match this trace context.

To ensure only correct trace graphs are formed, events will only be published
if subscribers are present prior to starting the trace. Subscriptions which are
added after the trace begins will not receive future events from that trace,
only future traces will be seen.

```mjs
import diagnostics_channel from 'node:diagnostics_channel';

const channels = diagnostics_channel.tracingChannel('my-channel');

channels.tracePromise(async () => {
  // Do something
}, {
  some: 'thing',
});
```

```cjs
const diagnostics_channel = require('node:diagnostics_channel');

const channels = diagnostics_channel.tracingChannel('my-channel');

channels.tracePromise(async () => {
  // Do something
}, {
  some: 'thing',
});
```

#### `tracingChannel.traceCallback(fn, position, context, thisArg, ...args)`

<!-- YAML
added:
 - v19.9.0
 - v18.19.0
-->

> Stability: 1 - Experimental

* `fn` {Function} callback using function to wrap a trace around
* `position` {number} Zero-indexed argument position of expected callback
  (defaults to last argument if `undefined` is passed)
* `context` {Object} Shared object to correlate trace events through (defaults
  to `{}` if `undefined` is passed)
* `thisArg` {any} The receiver to be used for the function call
* `...args` {any} arguments to pass to the function (must include the callback)
* Returns: {any} The return value of the given function

Trace a callback-receiving function call. The callback is expected to follow
the error as first arg convention typically used. This will always produce a
[`start` event][] and [`end` event][] around the synchronous portion of the
function execution, and will produce a [`asyncStart` event][] and
[`asyncEnd` event][] around the callback execution. It may also produce an
[`error` event][] if the given function throws or the first argument passed to
the callback is set. This will run the given function using
[`channel.runStores(context, ...)`][] on the `start` channel which ensures all
events should have any bound stores set to match this trace context.

To ensure only correct trace graphs are formed, events will only be published
if subscribers are present prior to starting the trace. Subscriptions which are
added after the trace begins will not receive future events from that trace,
only future traces will be seen.

```mjs
import diagnostics_channel from 'node:diagnostics_channel';

const channels = diagnostics_channel.tracingChannel('my-channel');

channels.traceCallback((arg1, callback) => {
  // Do something
  callback(null, 'result');
}, 1, {
  some: 'thing',
}, thisArg, arg1, callback);
```

```cjs
const diagnostics_channel = require('node:diagnostics_channel');

const channels = diagnostics_channel.tracingChannel('my-channel');

channels.traceCallback((arg1, callback) => {
  // Do something
  callback(null, 'result');
}, {
  some: 'thing',
}, thisArg, arg1, callback);
```

The callback will also be run with [`channel.runStores(context, ...)`][] which
enables context loss recovery in some cases.

```mjs
import diagnostics_channel from 'node:diagnostics_channel';
import { AsyncLocalStorage } from 'node:async_hooks';

const channels = diagnostics_channel.tracingChannel('my-channel');
const myStore = new AsyncLocalStorage();

// The start channel sets the initial store data to something
// and stores that store data value on the trace context object
channels.start.bindStore(myStore, (data) => {
  const span = new Span(data);
  data.span = span;
  return span;
});

// Then asyncStart can restore from that data it stored previously
channels.asyncStart.bindStore(myStore, (data) => {
  return data.span;
});
```

```cjs
const diagnostics_channel = require('node:diagnostics_channel');
const { AsyncLocalStorage } = require('node:async_hooks');

const channels = diagnostics_channel.tracingChannel('my-channel');
const myStore = new AsyncLocalStorage();

// The start channel sets the initial store data to something
// and stores that store data value on the trace context object
channels.start.bindStore(myStore, (data) => {
  const span = new Span(data);
  data.span = span;
  return span;
});

// Then asyncStart can restore from that data it stored previously
channels.asyncStart.bindStore(myStore, (data) => {
  return data.span;
});
```

#### `tracingChannel.hasSubscribers`

<!-- YAML
added:
 - v22.0.0
 - v20.13.0
-->

> Stability: 1 - Experimental

* Returns: {boolean} `true` if any of the individual channels has a subscriber,
  `false` if not.

This is a helper method available on a [`TracingChannel`][] instance to check if
any of the [TracingChannel Channels][] have subscribers. A `true` is returned if
any of them have at least one subscriber, a `false` is returned otherwise.

```mjs
import diagnostics_channel from 'node:diagnostics_channel';

const channels = diagnostics_channel.tracingChannel('my-channel');

if (channels.hasSubscribers) {
  // Do something
}
```

```cjs
const diagnostics_channel = require('node:diagnostics_channel');

const channels = diagnostics_channel.tracingChannel('my-channel');

if (channels.hasSubscribers) {
  // Do something
}
```

### TracingChannel Channels

A TracingChannel is a collection of several diagnostics\_channels representing
specific points in the execution lifecycle of a single traceable action. The
behavior is split into five diagnostics\_channels consisting of `start`,
`end`, `asyncStart`, `asyncEnd`, and `error`. A single traceable action will
share the same event object between all events, this can be helpful for
managing correlation through a weakmap.

These event objects will be extended with `result` or `error` values when
the task "completes". In the case of a synchronous task the `result` will be
the return value and the `error` will be anything thrown from the function.
With callback-based async functions the `result` will be the second argument
of the callback while the `error` will either be a thrown error visible in the
`end` event or the first callback argument in either of the `asyncStart` or
`asyncEnd` events.

To ensure only correct trace graphs are formed, events should only be published
if subscribers are present prior to starting the trace. Subscriptions which are
added after the trace begins should not receive future events from that trace,
only future traces will be seen.

Tracing channels should follow a naming pattern of:

* `tracing:module.class.method:start` or `tracing:module.function:start`
* `tracing:module.class.method:end` or `tracing:module.function:end`
* `tracing:module.class.method:asyncStart` or `tracing:module.function:asyncStart`
* `tracing:module.class.method:asyncEnd` or `tracing:module.function:asyncEnd`
* `tracing:module.class.method:error` or `tracing:module.function:error`

#### `start(event)`

* Name: `tracing:${name}:start`

The `start` event represents the point at which a function is called. At this
point the event data may contain function arguments or anything else available
at the very start of the execution of the function.

#### `end(event)`

* Name: `tracing:${name}:end`

The `end` event represents the point at which a function call returns a value.
In the case of an async function this is when the promise returned not when the
function itself makes a return statement internally. At this point, if the
traced function was synchronous the `result` field will be set to the return
value of the function. Alternatively, the `error` field may be present to
represent any thrown errors.

It is recommended to listen specifically to the `error` event to track errors
as it may be possible for a traceable action to produce multiple errors. For
example, an async task which fails may be started internally before the sync
part of the task then throws an error.

#### `asyncStart(event)`

* Name: `tracing:${name}:asyncStart`

The `asyncStart` event represents the callback or continuation of a traceable
function being reached. At this point things like callback arguments may be
available, or anything else expressing the "result" of the action.

For callbacks-based functions, the first argument of the callback will be
assigned to the `error` field, if not `undefined` or `null`, and the second
argument will be assigned to the `result` field.

For promises, the argument to the `resolve` path will be assigned to `result`
or the argument to the `reject` path will be assign to `error`.

It is recommended to listen specifically to the `error` event to track errors
as it may be possible for a traceable action to produce multiple errors. For
example, an async task which fails may be started internally before the sync
part of the task then throws an error.

#### `asyncEnd(event)`

* Name: `tracing:${name}:asyncEnd`

The `asyncEnd` event represents the callback of an asynchronous function
returning. It's not likely event data will change after the `asyncStart` event,
however it may be useful to see the point where the callback completes.

#### `error(event)`

* Name: `tracing:${name}:error`

The `error` event represents any error produced by the traceable function
either synchronously or asynchronously. If an error is thrown in the
synchronous portion of the traced function the error will be assigned to the
`error` field of the event and the `error` event will be triggered. If an error
is received asynchronously through a callback or promise rejection it will also
be assigned to the `error` field of the event and trigger the `error` event.

It is possible for a single traceable function call to produce errors multiple
times so this should be considered when consuming this event. For example, if
another async task is triggered internally which fails and then the sync part
of the function then throws and error two `error` events will be emitted, one
for the sync error and one for the async error.

### Built-in Channels

> Stability: 1 - Experimental

While the diagnostics\_channel API is now considered stable, the built-in
channels currently available are not. Each channel must be declared stable
independently.

#### HTTP

`http.client.request.start`

* `request` {http.ClientRequest}

Emitted when client starts a request.

`http.client.request.error`

* `request` {http.ClientRequest}
* `error` {Error}

Emitted when an error occurs during a client request.

`http.client.response.finish`

* `request` {http.ClientRequest}
* `response` {http.IncomingMessage}

Emitted when client receives a response.

`http.server.request.start`

* `request` {http.IncomingMessage}
* `response` {http.ServerResponse}
* `socket` {net.Socket}
* `server` {http.Server}

Emitted when server receives a request.

`http.server.response.finish`

* `request` {http.IncomingMessage}
* `response` {http.ServerResponse}
* `socket` {net.Socket}
* `server` {http.Server}

Emitted when server sends a response.

#### Modules

`module.require.start`

* `event` {Object} containing the following properties
  * `id` - Argument passed to `require()`. Module name.
  * `parentFilename` - Name of the module that attempted to require(id).

Emitted when `require()` is executed. See [`start` event][].

`module.require.end`

* `event` {Object} containing the following properties
  * `id` - Argument passed to `require()`. Module name.
  * `parentFilename` - Name of the module that attempted to require(id).

Emitted when a `require()` call returns. See [`end` event][].

`module.require.error`

* `event` {Object} containing the following properties
  * `id` - Argument passed to `require()`. Module name.
  * `parentFilename` - Name of the module that attempted to require(id).
* `error` {Error}

Emitted when a `require()` throws an error. See [`error` event][].

`module.import.asyncStart`

* `event` {Object} containing the following properties
  * `id` - Argument passed to `import()`. Module name.
  * `parentURL` - URL object of the module that attempted to import(id).

Emitted when `import()` is invoked. See [`asyncStart` event][].

`module.import.asyncEnd`

* `event` {Object} containing the following properties
  * `id` - Argument passed to `import()`. Module name.
  * `parentURL` - URL object of the module that attempted to import(id).

Emitted when `import()` has completed. See [`asyncEnd` event][].

`module.import.error`

* `event` {Object} containing the following properties
  * `id` - Argument passed to `import()`. Module name.
  * `parentURL` - URL object of the module that attempted to import(id).
* `error` {Error}

Emitted when a `import()` throws an error. See [`error` event][].

#### NET

`net.client.socket`

* `socket` {net.Socket}

Emitted when a new TCP or pipe client socket is created.

`net.server.socket`

* `socket` {net.Socket}

Emitted when a new TCP or pipe connection is received.

`tracing:net.server.listen:asyncStart`

* `server` {net.Server}
* `options` {Object}

Emitted when [`net.Server.listen()`][] is invoked, before the port or pipe is actually setup.

`tracing:net.server.listen:asyncEnd`

* `server` {net.Server}

Emitted when [`net.Server.listen()`][] has completed and thus the server is ready to accept connection.

`tracing:net.server.listen:error`

* `server` {net.Server}
* `error` {Error}

Emitted when [`net.Server.listen()`][] is returning an error.

#### UDP

`udp.socket`

* `socket` {dgram.Socket}

Emitted when a new UDP socket is created.

#### Process

<!-- YAML
added: v16.18.0
-->

`child_process`

* `process` {ChildProcess}

Emitted when a new process is created.

#### Worker Thread

<!-- YAML
added: v16.18.0
-->

`worker_threads`

* `worker` [`Worker`][]

Emitted when a new thread is created.

[TracingChannel Channels]: #tracingchannel-channels
[`'uncaughtException'`]: process.md#event-uncaughtexception
[`TracingChannel`]: #class-tracingchannel
[`Worker`]: worker_threads.md#class-worker
[`asyncEnd` event]: #asyncendevent
[`asyncStart` event]: #asyncstartevent
[`channel.bindStore(store)`]: #channelbindstorestore-transform
[`channel.runStores(context, ...)`]: #channelrunstorescontext-fn-thisarg-args
[`channel.subscribe(onMessage)`]: #channelsubscribeonmessage
[`channel.unsubscribe(onMessage)`]: #channelunsubscribeonmessage
[`diagnostics_channel.channel(name)`]: #diagnostics_channelchannelname
[`diagnostics_channel.subscribe(name, onMessage)`]: #diagnostics_channelsubscribename-onmessage
[`diagnostics_channel.tracingChannel()`]: #diagnostics_channeltracingchannelnameorchannels
[`diagnostics_channel.unsubscribe(name, onMessage)`]: #diagnostics_channelunsubscribename-onmessage
[`end` event]: #endevent
[`error` event]: #errorevent
[`net.Server.listen()`]: net.md#serverlisten
[`start` event]: #startevent
[context loss]: async_context.md#troubleshooting-context-loss
