// META: global=window,worker,shadowrealm
'use strict';

promise_test(t => {
  let isDone = false;
  const ws = new WritableStream(
    {
      write() {
        return new Promise(resolve => {
          t.step_timeout(() => {
            isDone = true;
            resolve();
          }, 200);
        });
      },

      close() {
        assert_true(isDone, 'close is only called once the promise has been resolved');
      }
    },
    new ByteLengthQueuingStrategy({ highWaterMark: 1024 * 16 })
  );

  const writer = ws.getWriter();
  writer.write({ byteLength: 1024 });

  return writer.close();
}, 'Closing a writable stream with in-flight writes below the high water mark delays the close call properly');
