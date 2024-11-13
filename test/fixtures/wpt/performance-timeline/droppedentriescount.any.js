promise_test(t => {
  // This setup is required for later tests as well.
  // Await for a dropped entry.
  return new Promise(res => {
    // Set a buffer size of 0 so that new resource entries count as dropped.
    performance.setResourceTimingBufferSize(0);
    // Use an observer to make sure the promise is resolved only when the
    // new entry has been created.
    new PerformanceObserver(res).observe({type: 'resource'});
    fetch('resources/square.png?id=1');
  }).then(() => {
  return new Promise(resolve => {
    new PerformanceObserver(t.step_func((entries, obs, options) => {
      assert_equals(options['droppedEntriesCount'], 0);
      resolve();
    })).observe({type: 'mark'});
    performance.mark('test');
  })});
}, 'Dropped entries count is 0 when there are no dropped entries of relevant type.');

promise_test(async t => {
  return new Promise(resolve => {
    new PerformanceObserver(t.step_func((entries, obs, options) => {
      assert_equals(options['droppedEntriesCount'], 1);
      resolve();
    })).observe({entryTypes: ['mark', 'resource']});
    performance.mark('meow');
  });
}, 'Dropped entries correctly counted with multiple types.');

promise_test(t => {
  return new Promise(resolve => {
    new PerformanceObserver(t.step_func((entries, obs, options) => {
      assert_equals(options['droppedEntriesCount'], 1,
          'There should have been some dropped resource timing entries at this point');
      resolve();
    })).observe({type: 'resource', buffered: true});
  });
}, 'Dropped entries counted even if observer was not registered at the time.');

promise_test(t => {
  return new Promise(resolve => {
    let callback_ran = false;
    new PerformanceObserver(t.step_func((entries, obs, options) => {
      if (!callback_ran) {
        assert_equals(options['droppedEntriesCount'], 2,
            'There should be two dropped entries right now.');
        fetch('resources/square.png?id=3');
        callback_ran = true;
      } else {
        assert_equals(options['droppedEntriesCount'], undefined,
            'droppedEntriesCount should be unset after the first callback!');
        resolve();
      }
    })).observe({type: 'resource'});
    fetch('resources/square.png?id=2');
  });
}, 'Dropped entries only surfaced on the first callback.');


promise_test(t => {
  return new Promise(resolve => {
    let callback_ran = false;
    let droppedEntriesCount = -1;
    new PerformanceObserver(t.step_func((entries, obs, options) => {
      if (!callback_ran) {
        assert_greater_than(options['droppedEntriesCount'], 0,
            'There should be several dropped entries right now.');
        droppedEntriesCount = options['droppedEntriesCount'];
        callback_ran = true;
        obs.observe({type: 'mark'});
        performance.mark('woof');
      } else {
        assert_equals(options['droppedEntriesCount'], droppedEntriesCount,
            'There should be droppedEntriesCount due to the new observe().');
        resolve();
      }
    })).observe({type: 'resource'});
    fetch('resources/square.png?id=4');
  });
}, 'Dropped entries surfaced after an observe() call!');
