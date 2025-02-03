async function waitForEvent(eventName, test, target, timeoutMs = 500) {
  return new Promise((resolve, reject) => {
    const timeoutCallback = test.step_timeout(() => {
      reject(`No ${eventName} event received for target ${target}`);
    }, timeoutMs);
    target.addEventListener(eventName, (evt) => {
      clearTimeout(timeoutCallback);
      resolve(evt);
    }, { once: true });
  });
}

async function waitForScrollendEvent(test, target, timeoutMs = 500) {
  return waitForEvent("scrollend", test, target, timeoutMs);
}

async function waitForScrollendEventNoTimeout(target) {
  return new Promise((resolve) => {
    target.addEventListener("scrollend", resolve);
  });
}

async function waitForPointercancelEvent(test, target, timeoutMs = 500) {
  return waitForEvent("pointercancel", test, target, timeoutMs);
}

// Resets the scroll position to (0,0).  If a scroll is required, then the
// promise is not resolved until the scrollend event is received.
async function waitForScrollReset(test, scroller, x = 0, y = 0) {
  return new Promise(resolve => {
    if (scroller.scrollTop == x && scroller.scrollLeft == y) {
      resolve();
    } else {
      const eventTarget =
        scroller == document.scrollingElement ? document : scroller;
      scroller.scrollTo(x, y);
      waitForScrollendEventNoTimeout(eventTarget).then(resolve);
    }
  });
}

async function createScrollendPromiseForTarget(test,
                                               target_div,
                                               timeoutMs = 500) {
  return waitForScrollendEvent(test, target_div, timeoutMs).then(evt => {
    assert_false(evt.cancelable, 'Event is not cancelable');
    assert_false(evt.bubbles, 'Event targeting element does not bubble');
  });
}

function verifyNoScrollendOnDocument(test) {
  const callback =
      test.unreached_func("window got unexpected scrollend event.");
  window.addEventListener('scrollend', callback);
  test.add_cleanup(() => {
    window.removeEventListener('scrollend', callback);
  });
}

async function verifyScrollStopped(test, target_div) {
  const unscaled_pause_time_in_ms = 100;
  const x = target_div.scrollLeft;
  const y = target_div.scrollTop;
  return new Promise(resolve => {
    test.step_timeout(() => {
      assert_equals(target_div.scrollLeft, x);
      assert_equals(target_div.scrollTop, y);
      resolve();
    }, unscaled_pause_time_in_ms);
  });
}

async function resetTargetScrollState(test, target_div) {
  if (target_div.scrollTop != 0 || target_div.scrollLeft != 0) {
    target_div.scrollTop = 0;
    target_div.scrollLeft = 0;
    return waitForScrollendEvent(test, target_div);
  }
}

const MAX_FRAME = 700;
const MAX_UNCHANGED_FRAMES = 20;

// Returns a promise that resolves when the given condition is met or rejects
// after MAX_FRAME animation frames.
// TODO(crbug.com/1400399): deprecate. We should not use frame based waits in
// WPT as frame rates may vary greatly in different testing environments.
function waitFor(condition, error_message = 'Reaches the maximum frames.') {
  return new Promise((resolve, reject) => {
    function tick(frames) {
      // We requestAnimationFrame either for MAX_FRAME frames or until condition
      // is met.
      if (frames >= MAX_FRAME)
        reject(error_message);
      else if (condition())
        resolve();
      else
        requestAnimationFrame(tick.bind(this, frames + 1));
    }
    tick(0);
  });
}

// TODO(crbug.com/1400446): Test driver should defer sending events until the
// browser is ready. Also the term compositor-commit is misleading as not all
// user-agents use a compositor process.
function waitForCompositorCommit() {
  return new Promise((resolve) => {
    // rAF twice.
    window.requestAnimationFrame(() => {
      window.requestAnimationFrame(resolve);
    });
  });
}

// Please don't remove this. This is necessary for chromium-based browsers. It
// can be a no-op on user-agents that do not have a separate compositor thread.
// TODO(crbug.com/1509054): This shouldn't be necessary if the test harness
// deferred running the tests until after paint holding.
async function waitForCompositorReady() {
  const animation =
      document.body.animate({ opacity: [ 0, 1 ] }, {duration: 1 });
  return animation.finished;
}

function waitForNextFrame() {
  const startTime = performance.now();
  return new Promise(resolve => {
    window.requestAnimationFrame((frameTime) => {
      if (frameTime < startTime) {
        window.requestAnimationFrame(resolve);
      } else {
        resolve();
      }
    });
  });
}

// TODO(crbug.com/1400399): Deprecate as frame rates may vary greatly in
// different test environments.
function waitForAnimationEnd(getValue) {
  var last_changed_frame = 0;
  var last_position = getValue();
  return new Promise((resolve, reject) => {
    function tick(frames) {
    // We requestAnimationFrame either for MAX_FRAME or until
    // MAX_UNCHANGED_FRAMES with no change have been observed.
      if (frames >= MAX_FRAME || frames - last_changed_frame > MAX_UNCHANGED_FRAMES) {
        resolve();
      } else {
        current_value = getValue();
        if (last_position != current_value) {
          last_changed_frame = frames;
          last_position = current_value;
        }
        requestAnimationFrame(tick.bind(this, frames + 1));
      }
    }
    tick(0);
  })
}

// Scrolls in target according to move_path with pauses in between
// The move_path should contains coordinates that are within target boundaries.
// Keep in mind that 0,0 is the center of the target element and is also
// the pointerDown position.
// pointerUp() is fired after sequence of moves.
function touchScrollInTargetSequentiallyWithPause(target, move_path, pause_time_in_ms = 100) {
  const test_driver_actions = new test_driver.Actions()
    .addPointer("pointer1", "touch")
    .pointerMove(0, 0, {origin: target})
    .pointerDown();

  const substeps = 5;
  let x = 0;
  let y = 0;
  // Do each move in 5 steps
  for(let move of move_path) {
    let step_x = (move.x - x) / substeps;
    let step_y = (move.y - y) / substeps;
    for(let step = 0; step < substeps; step++) {
      x += step_x;
      y += step_y;
      test_driver_actions.pointerMove(x, y, {origin: target});
    }
    test_driver_actions.pause(pause_time_in_ms); // To prevent inertial scroll
  }

  return test_driver_actions.pointerUp().send();
}

function touchScrollInTarget(pixels_to_scroll, target, direction, pause_time_in_ms = 100) {
  var x_delta = 0;
  var y_delta = 0;
  const num_movs = 5;
  if (direction == "down") {
    y_delta = -1 * pixels_to_scroll / num_movs;
  } else if (direction == "up") {
    y_delta = pixels_to_scroll / num_movs;
  } else if (direction == "right") {
    x_delta = -1 * pixels_to_scroll / num_movs;
  } else if (direction == "left") {
    x_delta = pixels_to_scroll / num_movs;
  } else {
    throw("scroll direction '" + direction + "' is not expected, direction should be 'down', 'up', 'left' or 'right'");
  }
  return new test_driver.Actions()
      .addPointer("pointer1", "touch")
      .pointerMove(0, 0, {origin: target})
      .pointerDown()
      .pointerMove(x_delta, y_delta, {origin: target})
      .pointerMove(2 * x_delta, 2 * y_delta, {origin: target})
      .pointerMove(3 * x_delta, 3 * y_delta, {origin: target})
      .pointerMove(4 * x_delta, 4 * y_delta, {origin: target})
      .pointerMove(5 * x_delta, 5 * y_delta, {origin: target})
      .pause(pause_time_in_ms)
      .pointerUp()
      .send();
}

// Trigger fling by doing pointerUp right after pointerMoves.
function touchFlingInTarget(pixels_to_scroll, target, direction) {
  return touchScrollInTarget(pixels_to_scroll, target, direction, 0 /* pause_time */);
}

function mouseActionsInTarget(target, origin, delta, pause_time_in_ms = 100) {
  return new test_driver.Actions()
    .addPointer("pointer1", "mouse")
    .pointerMove(origin.x, origin.y, { origin: target })
    .pointerDown()
    .pointerMove(origin.x + delta.x, origin.y + delta.y, { origin: target })
    .pointerMove(origin.x + delta.x * 2, origin.y + delta.y * 2, { origin: target })
    .pause(pause_time_in_ms)
    .pointerUp()
    .send();
}

// Returns a promise that resolves when the given condition holds for 10
// animation frames or rejects if the condition changes to false within 10
// animation frames.
// TODO(crbug.com/1400399): Deprecate as frame rates may very greatly in
// different test environments.
function conditionHolds(condition, error_message = 'Condition is not true anymore.') {
  const MAX_FRAME = 10;
  return new Promise((resolve, reject) => {
    function tick(frames) {
      // We requestAnimationFrame either for 10 frames or until condition is
      // violated.
      if (frames >= MAX_FRAME)
        resolve();
      else if (!condition())
        reject(error_message);
      else
        requestAnimationFrame(tick.bind(this, frames + 1));
    }
    tick(0);
  });
}

function scrollElementDown(element, scroll_amount) {
  let x = 0;
  let y = 0;
  let delta_x = 0;
  let delta_y = scroll_amount;
  let actions = new test_driver.Actions()
  .scroll(x, y, delta_x, delta_y, {origin: element});
  return  actions.send();
}

function scrollElementLeft(element, scroll_amount) {
  let x = 0;
  let y = 0;
  let delta_x = scroll_amount;
  let delta_y = 0;
  let actions = new test_driver.Actions()
  .scroll(x, y, delta_x, delta_y, {origin: element});
  return  actions.send();
}
