
async function test_scrollend_on_touch_drag(t, target_div) {
  // Skip the test on a Mac as they do not support touch screens.
  const isMac = navigator.platform.toUpperCase().indexOf('MAC') >= 0;
  if (isMac)
    return;

  await resetTargetScrollState(t, target_div);
  await waitForCompositorReady();

  const targetScrollendPromise = waitForScrollendEventNoTimeout(target_div);
  verifyNoScrollendOnDocument(t);

  let scrollend_count = 0;
  const scrollend_listener = () => {
    scrollend_count += 1;
  };
  target_div.addEventListener("scrollend", scrollend_listener);
  t.add_cleanup(() => {
    target_div.removeEventListener('scrollend', scrollend_listener);
  });

  // Perform a touch drag on target div and wait for target_div to get
  // a scrollend event.
  await new test_driver.Actions()
    .addPointer('TestPointer', 'touch')
    .pointerMove(0, 0, { origin: target_div }) // 0, 0 is center of element.
    .pointerDown()
    .addTick()
    .pointerMove(0, -40, { origin: target_div }) //  Drag up to move down.
    .addTick()
    .pause(200) //  Prevent inertial scroll.
    .pointerMove(0, -60, { origin: target_div })
    .addTick()
    .pause(200) //  Prevent inertial scroll.
    .pointerUp()
    .send();

  await targetScrollendPromise;

  assert_true(target_div.scrollTop > 0);
  await verifyScrollStopped(t, target_div);
  assert_equals(scrollend_count, 1);
}

async function test_scrollend_on_scrollbar_gutter_click(t, target_div) {
  // Skip test on platforms that do not have a visible scrollbar (e.g.
  // overlay scrollbar).
  const scrollbar_width = target_div.offsetWidth - target_div.clientWidth;
  if (scrollbar_width == 0)
    return;

  await resetTargetScrollState(t, target_div);
  await waitForCompositorReady();

  const targetScrollendPromise = waitForScrollendEventNoTimeout(target_div);
  verifyNoScrollendOnDocument(t);

  const bounds = target_div.getBoundingClientRect();
  // Some versions of webdriver have been known to frown at non-int arguments
  // to pointerMove.
  const x = Math.round(bounds.right - scrollbar_width / 2);
  const y = Math.round(bounds.bottom - 20);
  await new test_driver.Actions()
    .addPointer('TestPointer', 'mouse')
    .pointerMove(x, y, { origin: 'viewport' })
    .pointerDown()
    .addTick()
    .pointerUp()
    .send();

  await targetScrollendPromise;
  assert_true(target_div.scrollTop > 0);
  await verifyScrollStopped(t, target_div);
}

// Same issue as previous test.
async function test_scrollend_on_scrollbar_thumb_drag(t, target_div) {
  // Skip test on platforms that do not have a visible scrollbar (e.g.
  // overlay scrollbar).
  const scrollbar_width = target_div.offsetWidth - target_div.clientWidth;
  if (scrollbar_width == 0)
    return;

  await resetTargetScrollState(t, target_div);
  await waitForCompositorReady();

  const targetScrollendPromise = waitForScrollendEventNoTimeout(target_div);
  verifyNoScrollendOnDocument(t);

  const bounds = target_div.getBoundingClientRect();
  // Some versions of webdriver have been known to frown at non-int arguments
  // to pointerMove.
  const x = Math.round(bounds.right - scrollbar_width / 2);
  const y = Math.round(bounds.top + 30);
  const dy = 30;
  await new test_driver.Actions()
    .addPointer('TestPointer', 'mouse')
    .pointerMove(x, y, { origin: 'viewport' })
    .pointerDown()
    .pointerMove(x, y + dy, { origin: 'viewport' })
    .addTick()
    .pointerUp()
    .send();

  await targetScrollendPromise;
  assert_true(target_div.scrollTop > 0);
  await verifyScrollStopped(t, target_div);
}

async function test_scrollend_on_mousewheel_scroll(t, target_div, frame) {
  await resetTargetScrollState(t, target_div);
  await waitForCompositorReady();

  const targetScrollendPromise = waitForScrollendEventNoTimeout(target_div);
  verifyNoScrollendOnDocument(t);

  let scroll_origin = target_div;
  if (frame) {
    // chromedriver doesn't support passing { origin: element }
    // for an element within a subframe. Use the frame element itself.
    scroll_origin = frame;
  }
  const x = 0;
  const y = 0;
  const dx = 0;
  const dy = 40;
  await new test_driver.Actions()
  .scroll(x, y, dx, dy, { origin: scroll_origin })
  .send();

  await targetScrollendPromise;
  assert_true(target_div.scrollTop > 0);
  await verifyScrollStopped(t, target_div);
}

async function test_scrollend_on_keyboard_scroll(t, target_div) {
  await resetTargetScrollState(t, target_div);
  await waitForCompositorReady();

  verifyNoScrollendOnDocument(t);
  const targetScrollendPromise = waitForScrollendEventNoTimeout(target_div);

  target_div.focus();
  window.test_driver.send_keys(target_div, '\ue015');

  await targetScrollendPromise;
  assert_true(target_div.scrollTop > 0);
  await verifyScrollStopped(t, target_div);
}
