// Tests for AbortSignal.any() and subclasses that don't use a controller.
function abortSignalAnySignalOnlyTests(signalInterface) {
  const desc = `${signalInterface.name}.any()`

  test(t => {
    const signal = signalInterface.any([]);
    assert_false(signal.aborted);
  }, `${desc} works with an empty array of signals`);
}

// Tests for AbortSignal.any() and subclasses that use a controller.
function abortSignalAnyTests(signalInterface, controllerInterface) {
  const suffix = `(using ${controllerInterface.name})`;
  const desc = `${signalInterface.name}.any()`;

  test(t => {
    const controller = new controllerInterface();
    const signal = controller.signal;
    const cloneSignal = signalInterface.any([signal]);
    assert_false(cloneSignal.aborted);
    assert_true("reason" in cloneSignal, "cloneSignal has reason property");
    assert_equals(cloneSignal.reason, undefined,
        "cloneSignal.reason is initially undefined");
    assert_not_equals(signal, cloneSignal,
        `${desc} returns a new signal.`);

    let eventFired = false;
    cloneSignal.onabort = t.step_func((e) => {
      assert_equals(e.target, cloneSignal,
          `The event target is the signal returned by ${desc}`);
      eventFired = true;
    });

    controller.abort("reason string");
    assert_true(signal.aborted);
    assert_true(cloneSignal.aborted);
    assert_true(eventFired);
    assert_equals(cloneSignal.reason, "reason string",
        `${desc} propagates the abort reason`);
  }, `${desc} follows a single signal ${suffix}`);

  test(t => {
    for (let i = 0; i < 3; ++i) {
      const controllers = [];
      for (let j = 0; j < 3; ++j) {
        controllers.push(new controllerInterface());
      }
      const combinedSignal = signalInterface.any(controllers.map(c => c.signal));

      let eventFired = false;
      combinedSignal.onabort = t.step_func((e) => {
        assert_equals(e.target, combinedSignal,
            `The event target is the signal returned by ${desc}`);
        eventFired = true;
      });

      controllers[i].abort();
      assert_true(eventFired);
      assert_true(combinedSignal.aborted);
      assert_true(combinedSignal.reason instanceof DOMException,
          "signal.reason is a DOMException");
      assert_equals(combinedSignal.reason.name, "AbortError",
          "signal.reason is a AbortError");
    }
  }, `${desc} follows multiple signals ${suffix}`);

  test(t => {
    const controllers = [];
    for (let i = 0; i < 3; ++i) {
      controllers.push(new controllerInterface());
    }
    controllers[1].abort("reason 1");
    controllers[2].abort("reason 2");

    const signal = signalInterface.any(controllers.map(c => c.signal));
    assert_true(signal.aborted);
    assert_equals(signal.reason, "reason 1",
        "The signal should be aborted with the first reason");
  }, `${desc} returns an aborted signal if passed an aborted signal ${suffix}`);

  test(t => {
    const controller = new controllerInterface();
    const signal = signalInterface.any([controller.signal, controller.signal]);
    assert_false(signal.aborted);
    controller.abort("reason");
    assert_true(signal.aborted);
    assert_equals(signal.reason, "reason");
  }, `${desc} can be passed the same signal more than once ${suffix}`);

  test(t => {
    const controller1 = new controllerInterface();
    controller1.abort("reason 1");
    const controller2 = new controllerInterface();
    controller2.abort("reason 2");

    const signal = signalInterface.any([controller1.signal, controller2.signal, controller1.signal]);
    assert_true(signal.aborted);
    assert_equals(signal.reason, "reason 1");
  }, `${desc} uses the first instance of a duplicate signal ${suffix}`);

  test(t => {
    for (let i = 0; i < 3; ++i) {
      const controllers = [];
      for (let j = 0; j < 3; ++j) {
        controllers.push(new controllerInterface());
      }
      const combinedSignal1 =
          signalInterface.any([controllers[0].signal, controllers[1].signal]);
      const combinedSignal2 =
          signalInterface.any([combinedSignal1, controllers[2].signal]);

      let eventFired = false;
      combinedSignal2.onabort = t.step_func((e) => {
        eventFired = true;
      });

      controllers[i].abort();
      assert_true(eventFired);
      assert_true(combinedSignal2.aborted);
      assert_true(combinedSignal2.reason instanceof DOMException,
          "signal.reason is a DOMException");
      assert_equals(combinedSignal2.reason.name, "AbortError",
          "signal.reason is a AbortError");
    }
  }, `${desc} signals are composable ${suffix}`);

  async_test(t => {
    const controller = new controllerInterface();
    const timeoutSignal = AbortSignal.timeout(5);

    const combinedSignal = signalInterface.any([controller.signal, timeoutSignal]);

    combinedSignal.onabort = t.step_func_done(() => {
      assert_true(combinedSignal.aborted);
      assert_true(combinedSignal.reason instanceof DOMException,
          "combinedSignal.reason is a DOMException");
      assert_equals(combinedSignal.reason.name, "TimeoutError",
          "combinedSignal.reason is a TimeoutError");
    });
  }, `${desc} works with signals returned by AbortSignal.timeout() ${suffix}`);

  test(t => {
    const controller = new controllerInterface();
    let combined = signalInterface.any([controller.signal]);
    combined = signalInterface.any([combined]);
    combined = signalInterface.any([combined]);
    combined = signalInterface.any([combined]);

    let eventFired = false;
    combined.onabort = () => {
      eventFired = true;
    }

    assert_false(eventFired);
    assert_false(combined.aborted);

    controller.abort("the reason");

    assert_true(eventFired);
    assert_true(combined.aborted);
    assert_equals(combined.reason, "the reason");
  }, `${desc} works with intermediate signals ${suffix}`);

  test(t => {
    const controller = new controllerInterface();
    const signals = [];
    // The first event should be dispatched on the originating signal.
    signals.push(controller.signal);
    // All dependents are linked to `controller.signal` (never to another
    // composite signal), so this is the order events should fire.
    signals.push(signalInterface.any([controller.signal]));
    signals.push(signalInterface.any([controller.signal]));
    signals.push(signalInterface.any([signals[0]]));
    signals.push(signalInterface.any([signals[1]]));

    let result = "";
    for (let i = 0; i < signals.length; i++) {
      signals[i].addEventListener('abort', () => {
        result += i;
      });
    }
    controller.abort();
    assert_equals(result, "01234");
  }, `Abort events for ${desc} signals fire in the right order ${suffix}`);

  test(t => {
    const controller = new controllerInterface();
    const signal1 = signalInterface.any([controller.signal]);
    const signal2 = signalInterface.any([signal1]);
    let eventFired = false;

    controller.signal.addEventListener('abort', () => {
      const signal3 = signalInterface.any([signal2]);
      assert_true(controller.signal.aborted);
      assert_true(signal1.aborted);
      assert_true(signal2.aborted);
      assert_true(signal3.aborted);
      eventFired = true;
    });

    controller.abort();
    assert_true(eventFired, "event fired");
  }, `Dependent signals for ${desc} are marked aborted before abort events fire ${suffix}`);

  test(t => {
    const controller1 = new controllerInterface();
    const controller2 = new controllerInterface();
    const signal = signalInterface.any([controller1.signal, controller2.signal]);
    let count = 0;

    controller1.signal.addEventListener('abort', () => {
      controller2.abort("reason 2");
    });

    signal.addEventListener('abort', () => {
      count++;
    });

    controller1.abort("reason 1");
    assert_equals(count, 1);
    assert_true(signal.aborted);
    assert_equals(signal.reason, "reason 1");
  }, `Dependent signals for ${desc} are aborted correctly for reentrant aborts ${suffix}`);

  test(t => {
    const source = signalInterface.abort();
    const dependent = signalInterface.any([source]);
    assert_true(source.reason instanceof DOMException);
    assert_equals(source.reason, dependent.reason);
  }, `Dependent signals for ${desc} should use the same DOMException instance from the already aborted source signal ${suffix}`);

  test(t => {
    const controller = new controllerInterface();
    const source = controller.signal;
    const dependent = signalInterface.any([source]);
    controller.abort();
    assert_true(source.reason instanceof DOMException);
    assert_equals(source.reason, dependent.reason);
  }, `Dependent signals for ${desc} should use the same DOMException instance from the source signal being aborted later ${suffix}`);
}
