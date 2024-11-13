// The test functions called in the navigation-counter test. They rely on
// artifacts defined in
// '/html/browsers/browsing-the-web/back-forward-cache/resources/helper.sub.js'
// which should be included before this file to use these functions.

// This function is to obtain navigation ids of all performance entries to
// verify.
let testInitial = () => {
  return window.performance.getEntries().map(e => e.navigationId);
}

let testMarkMeasure = (markId, markName, MeasureName) => {
  const markName1 = 'test-mark';
  const markName2 = 'test-mark' + markId;
  const measureName = 'test-measure' + markId;

  window.performance.mark(markName1);
  window.performance.mark(markName2);
  window.performance.measure(measureName, markName1, markName2);
  return window.performance.getEntriesByName(markName2).concat(
    window.performance.getEntriesByName(measureName)).map(e => e.navigationId);
}

let testResourceTiming = async (resourceTimingEntryId) => {
  let navigationId;

  let p = new Promise(resolve => {
    new PerformanceObserver((list) => {
      const entry = list.getEntries().find(
        e => e.name.includes('json_resource' + resourceTimingEntryId));
      if (entry) {
        navigationId = entry.navigationId;
        resolve();
      }
    }).observe({ type: 'resource' });
  });

  const resp = await fetch(
    '/performance-timeline/resources/json_resource' + resourceTimingEntryId + '.json');
  await p;
  return [navigationId];
}

let testElementTiming = async (elementTimingEntryId) => {
  let navigationId;
  let p = new Promise(resolve => {
    new PerformanceObserver((list) => {
      const entry = list.getEntries().find(
        e => e.entryType === 'element' && e.identifier === 'test-element-timing' + elementTimingEntryId);
      if (entry) {
        navigationId = entry.navigationId;
        resolve();
      }
    }).observe({ type: 'element' });
  });

  let el = document.createElement('p');
  el.setAttribute('elementtiming', 'test-element-timing' + elementTimingEntryId);
  el.textContent = 'test element timing text';
  document.body.appendChild(el);
  await p;
  return [navigationId];
}

let testLongTask = async () => {
  let navigationIds = [];

  let p = new Promise(resolve => {
    new PerformanceObserver((list) => {
      const entry = list.getEntries().find(e => e.entryType === 'longtask')
      if (entry) {
        navigationIds.push(entry.navigationId);
        navigationIds = navigationIds.concat(
          entry.attribution.map(a => a.navigationId));
        resolve();
      }
    }).observe({ type: 'longtask' });
  });

  const script = document.createElement('script');
  script.src = '/performance-timeline/resources/make_long_task.js';
  document.body.appendChild(script);
  await p;
  document.body.removeChild(script);
  return navigationIds;
}

const testFunctionMap = {
  'mark_measure': testMarkMeasure,
  'resource_timing': testResourceTiming,
  'element_timing': testElementTiming,
  'long_task_task_attribution': testLongTask,
};

function runNavigationIdTest(params, description) {
  const defaultParams = {
    openFunc: url => window.open(url, '_blank', 'noopener'),
    scripts: [],
    funcBeforeNavigation: () => { },
    targetOrigin: originCrossSite,
    navigationTimes: 4,
    funcAfterAssertion: () => { },
  }  // Apply defaults.
  params = { ...defaultParams, ...params };

  promise_test(async t => {
    const pageA = new RemoteContext(token());
    const pageB = new RemoteContext(token());

    const urlA = executorPath + pageA.context_id;
    const urlB = params.targetOrigin + executorPath + pageB.context_id;
    // Open url A.
    params.openFunc(urlA);
    await pageA.execute_script(waitForPageShow);

    // Assert navigation ids of all performance entries are the same.
    let navigationIds = await pageA.execute_script(testInitial);
    assert_true(
      navigationIds.every(t => t === navigationIds[0]),
      'Navigation Ids should be the same as the initial load.');

    for (i = 1; i <= params.navigationTimes; i++) {
      // Navigate away to url B and back.
      await navigateAndThenBack(pageA, pageB, urlB);

      // Assert new navigation ids are generated when the document is load from bfcache.
      let nextNavigationIds = await pageA.execute_script(
        testFunctionMap[params.testName], [i + 1]);

      // Assert navigation ids of all performance entries are the same.
      assert_true(
        nextNavigationIds.every(t => t === nextNavigationIds[0]),
        'All Navigation Ids should be same after bfcache navigation.');

      // Assert navigation ids after bfcache navigation are different from those before.
      assert_true(
        navigationIds[0] !== nextNavigationIds[0],
        params.testName +
        ' Navigation Ids should be re-generated and different from the previous ones.');

      navigationIds = nextNavigationIds;
    }
  }, description);
}