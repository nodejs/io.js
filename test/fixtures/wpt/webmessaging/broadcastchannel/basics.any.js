test(function() {
  assert_throws_js(
      TypeError,
      () => BroadcastChannel(""),
      "Calling BroadcastChannel constructor without 'new' must throw"
    );
}, "BroadcastChannel constructor called as normal function");

async_test(t => {
    let c1 = new BroadcastChannel('eventType');
    let c2 = new BroadcastChannel('eventType');

    c2.onmessage = t.step_func(e => {
        assert_true(e instanceof MessageEvent);
        assert_equals(e.target, c2);
        assert_equals(e.type, 'message');
        assert_equals(e.origin, location.origin, 'origin');
        assert_equals(e.data, 'hello world');
        assert_equals(e.source, null, 'source');
        t.done();
      });
    c1.postMessage('hello world');
  }, 'postMessage results in correct event');

async_test(t => {
    let c1 = new BroadcastChannel('order');
    let c2 = new BroadcastChannel('order');
    let c3 = new BroadcastChannel('order');

    let events = [];
    let doneCount = 0;
    let handler = t.step_func(e => {
      events.push(e);
      if (e.data == 'done') {
        doneCount++;
        if (doneCount == 2) {
          assert_equals(events.length, 6);
          assert_equals(events[0].target, c2, 'target for event 0');
          assert_equals(events[0].data, 'from c1');
          assert_equals(events[1].target, c3, 'target for event 1');
          assert_equals(events[1].data, 'from c1');
          assert_equals(events[2].target, c1, 'target for event 2');
          assert_equals(events[2].data, 'from c3');
          assert_equals(events[3].target, c2, 'target for event 3');
          assert_equals(events[3].data, 'from c3');
          assert_equals(events[4].target, c1, 'target for event 4');
          assert_equals(events[4].data, 'done');
          assert_equals(events[5].target, c3, 'target for event 5');
          assert_equals(events[5].data, 'done');
          t.done();
        }
      }
    });
    c1.onmessage = handler;
    c2.onmessage = handler;
    c3.onmessage = handler;

    c1.postMessage('from c1');
    c3.postMessage('from c3');
    c2.postMessage('done');
  }, 'messages are delivered in port creation order');

async_test(t => {
    let c1 = new BroadcastChannel('closed');
    let c2 = new BroadcastChannel('closed');
    let c3 = new BroadcastChannel('closed');

    c2.onmessage = t.unreached_func();
    c2.close();
    c3.onmessage = t.step_func(() => t.done());
    c1.postMessage('test');
  }, 'messages aren\'t delivered to a closed port');

 async_test(t => {
    let c1 = new BroadcastChannel('closed');
    let c2 = new BroadcastChannel('closed');
    let c3 = new BroadcastChannel('closed');

    c2.onmessage = t.unreached_func();
    c3.onmessage = t.step_func(() => t.done());
    c1.postMessage('test');
    c2.close();
}, 'messages aren\'t delivered to a port closed after calling postMessage.');

async_test(t => {
    let c1 = new BroadcastChannel('create-in-onmessage');
    let c2 = new BroadcastChannel('create-in-onmessage');

    c2.onmessage = t.step_func(e => {
        assert_equals(e.data, 'first');
        c2.close();
        let c3 = new BroadcastChannel('create-in-onmessage');
        c3.onmessage = t.step_func(event => {
            assert_equals(event.data, 'done');
            t.done();
          });
        c1.postMessage('done');
      });
    c1.postMessage('first');
    c2.postMessage('second');
  }, 'closing and creating channels during message delivery works correctly');

async_test(t => {
    let c1 = new BroadcastChannel('close-in-onmessage');
    let c2 = new BroadcastChannel('close-in-onmessage');
    let c3 = new BroadcastChannel('close-in-onmessage');
    let events = [];
    c1.onmessage = e => events.push('c1: ' + e.data);
    c2.onmessage = e => events.push('c2: ' + e.data);
    c3.onmessage = e => events.push('c3: ' + e.data);

    // c2 closes itself when it receives the first message
    c2.addEventListener('message', e => {
        c2.close();
      });

    c3.addEventListener('message', t.step_func(e => {
        if (e.data == 'done') {
          assert_array_equals(events, [
              'c2: first',
              'c3: first',
              'c3: done']);
          t.done();
        }
      }));
    c1.postMessage('first');
    c1.postMessage('done');
  }, 'Closing a channel in onmessage prevents already queued tasks from firing onmessage events');
