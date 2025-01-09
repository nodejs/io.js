// META: script=performanceobservers.js

test(function () {
  const obs = new PerformanceObserver(() => {});
  assert_throws_js(TypeError, function () {
    obs.observe({});
  });
  assert_throws_js(TypeError, function () {
    obs.observe({entryType: ['mark', 'measure']});
  });
}, "Calling observe() without 'type' or 'entryTypes' throws a TypeError");

test(() => {
  const obs = new PerformanceObserver(() =>{});
  obs.observe({entryTypes: ["mark"]});
  assert_throws_dom('InvalidModificationError', function () {
    obs.observe({type: "measure"});
  });
}, "Calling observe() with entryTypes and then type should throw an InvalidModificationError");

test(() => {
  const obs = new PerformanceObserver(() =>{});
  obs.observe({type: "mark"});
  assert_throws_dom('InvalidModificationError', function () {
    obs.observe({entryTypes: ["measure"]});
  });
}, "Calling observe() with type and then entryTypes should throw an InvalidModificationError");

test(() => {
  const obs = new PerformanceObserver(() =>{});
  assert_throws_js(TypeError, function () {
    obs.observe({type: "mark", entryTypes: ["measure"]});
  });
}, "Calling observe() with type and entryTypes should throw a TypeError");

test(() => {
  const obs = new PerformanceObserver(() =>{});
  assert_throws_js(TypeError, function () {
    obs.observe({buffered: true, entryTypes: ["measure"]});
  });
}, "Calling observe() with buffered and entryTypes should throw a TypeError");

test(function () {
  const obs = new PerformanceObserver(() =>{});
  // Definitely not an entry type.
  obs.observe({type: "this-cannot-match-an-entryType"});
  // Close to an entry type, but not quite.
  obs.observe({type: "marks"});
}, "Passing in unknown values to type does throw an exception.");

async_test(function (t) {
  let observedMark = false;
  let observedMeasure = false;
  const observer = new PerformanceObserver(
    t.step_func(function (entryList, obs) {
      observedMark |= entryList.getEntries().filter(
        entry => entry.entryType === 'mark').length;
      observedMeasure |= entryList.getEntries().filter(
        entry => entry.entryType === 'measure').length
      // Only conclude the test once we receive both entries!
      if (observedMark && observedMeasure) {
        observer.disconnect();
        t.done();
      }
    })
  );
  observer.observe({type: "mark"});
  observer.observe({type: "measure"});
  self.performance.mark("mark1");
  self.performance.measure("measure1");
}, "observe() with different type values stacks.");
