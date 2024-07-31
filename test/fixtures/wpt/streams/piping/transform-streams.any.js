// META: global=window,worker,shadowrealm
'use strict';

promise_test(() => {
  const rs = new ReadableStream({
    start(c) {
      c.enqueue('a');
      c.enqueue('b');
      c.enqueue('c');
      c.close();
    }
  });

  const ts = new TransformStream();

  const ws = new WritableStream();

  return rs.pipeThrough(ts).pipeTo(ws).then(() => {
    const writer = ws.getWriter();
    return writer.closed;
  });
}, 'Piping through an identity transform stream should close the destination when the source closes');
