// META: global=window,worker,shadowrealm
// META: script=../resources/test-utils.js
// META: script=../resources/rs-test-templates.js
'use strict';

// Run the readable stream test templates against readable streams created directly using the constructor

const theError = { name: 'boo!' };
const chunks = ['a', 'b'];

templatedRSEmpty('ReadableStream (empty)', () => {
  return new ReadableStream();
});

templatedRSEmptyReader('ReadableStream (empty) reader', () => {
  return streamAndDefaultReader(new ReadableStream());
});

templatedRSClosed('ReadableStream (closed via call in start)', () => {
  return new ReadableStream({
    start(c) {
      c.close();
    }
  });
});

templatedRSClosedReader('ReadableStream reader (closed before getting reader)', () => {
  let controller;
  const stream = new ReadableStream({
    start(c) {
      controller = c;
    }
  });
  controller.close();
  const result = streamAndDefaultReader(stream);
  return result;
});

templatedRSClosedReader('ReadableStream reader (closed after getting reader)', () => {
  let controller;
  const stream = new ReadableStream({
    start(c) {
      controller = c;
    }
  });
  const result = streamAndDefaultReader(stream);
  controller.close();
  return result;
});

templatedRSClosed('ReadableStream (closed via cancel)', () => {
  const stream = new ReadableStream();
  stream.cancel();
  return stream;
});

templatedRSClosedReader('ReadableStream reader (closed via cancel after getting reader)', () => {
  const stream = new ReadableStream();
  const result = streamAndDefaultReader(stream);
  result.reader.cancel();
  return result;
});

templatedRSErrored('ReadableStream (errored via call in start)', () => {
  return new ReadableStream({
    start(c) {
      c.error(theError);
    }
  });
}, theError);

templatedRSErroredSyncOnly('ReadableStream (errored via call in start)', () => {
  return new ReadableStream({
    start(c) {
      c.error(theError);
    }
  });
}, theError);

templatedRSErrored('ReadableStream (errored via returning a rejected promise in start)', () => {
  return new ReadableStream({
    start() {
      return Promise.reject(theError);
    }
  });
}, theError);

templatedRSErroredReader('ReadableStream (errored via returning a rejected promise in start) reader', () => {
  return streamAndDefaultReader(new ReadableStream({
    start() {
      return Promise.reject(theError);
    }
  }));
}, theError);

templatedRSErroredReader('ReadableStream reader (errored before getting reader)', () => {
  let controller;
  const stream = new ReadableStream({
    start(c) {
      controller = c;
    }
  });
  controller.error(theError);
  return streamAndDefaultReader(stream);
}, theError);

templatedRSErroredReader('ReadableStream reader (errored after getting reader)', () => {
  let controller;
  const result = streamAndDefaultReader(new ReadableStream({
    start(c) {
      controller = c;
    }
  }));
  controller.error(theError);
  return result;
}, theError);

templatedRSTwoChunksOpenReader('ReadableStream (two chunks enqueued, still open) reader', () => {
  return streamAndDefaultReader(new ReadableStream({
    start(c) {
      c.enqueue(chunks[0]);
      c.enqueue(chunks[1]);
    }
  }));
}, chunks);

templatedRSTwoChunksClosedReader('ReadableStream (two chunks enqueued, then closed) reader', () => {
  let doClose;
  const stream = new ReadableStream({
    start(c) {
      c.enqueue(chunks[0]);
      c.enqueue(chunks[1]);
      doClose = c.close.bind(c);
    }
  });
  const result = streamAndDefaultReader(stream);
  doClose();
  return result;
}, chunks);

function streamAndDefaultReader(stream) {
  return { stream, reader: stream.getReader() };
}
