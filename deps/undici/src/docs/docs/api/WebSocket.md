# Class: WebSocket

Extends: [`EventTarget`](https://developer.mozilla.org/en-US/docs/Web/API/EventTarget)

The WebSocket object provides a way to manage a WebSocket connection to a server, allowing bidirectional communication. The API follows the [WebSocket spec](https://developer.mozilla.org/en-US/docs/Web/API/WebSocket) and [RFC 6455](https://datatracker.ietf.org/doc/html/rfc6455).

## `new WebSocket(url[, protocol])`

Arguments:

* **url** `URL | string`
* **protocol** `string | string[] | WebSocketInit` (optional) - Subprotocol(s) to request the server use, or a [`Dispatcher`](/docs/docs/api/Dispatcher.md).

### Example:

This example will not work in browsers or other platforms that don't allow passing an object.

```mjs
import { WebSocket, ProxyAgent } from 'undici'

const proxyAgent = new ProxyAgent('my.proxy.server')

const ws = new WebSocket('wss://echo.websocket.events', {
  dispatcher: proxyAgent,
  protocols: ['echo', 'chat']
})
```

If you do not need a custom Dispatcher, it's recommended to use the following pattern:

```mjs
import { WebSocket } from 'undici'

const ws = new WebSocket('wss://echo.websocket.events', ['echo', 'chat'])
```

# Class: WebSocketStream

> ⚠️ Warning: the WebSocketStream API has not been finalized and is likely to change.

See [MDN](https://developer.mozilla.org/en-US/docs/Web/API/WebSocketStream) for more information.

## `new WebSocketStream(url[, protocol])`

Arguments:

* **url** `URL | string`
* **options** `WebSocketStreamOptions` (optional)

### WebSocketStream Example

```js
const stream = new WebSocketStream('https://echo.websocket.org/')
const { readable, writable } = await stream.opened

async function read () {
  /** @type {ReadableStreamReader} */
  const reader = readable.getReader()

  while (true) {
    const { done, value } = await reader.read()
    if (done) break

    // do something with value
  }
}

async function write () {
  /** @type {WritableStreamDefaultWriter} */
  const writer = writable.getWriter()
  writer.write('Hello, world!')
  writer.releaseLock()
}

read()

setInterval(() => write(), 5000)

```

## Read More

- [MDN - WebSocket](https://developer.mozilla.org/en-US/docs/Web/API/WebSocket)
- [The WebSocket Specification](https://www.rfc-editor.org/rfc/rfc6455)
- [The WHATWG WebSocket Specification](https://websockets.spec.whatwg.org/)
