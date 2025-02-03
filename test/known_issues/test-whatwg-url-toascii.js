'use strict';
const common = require('../common');
if (!common.hasIntl) {
  // A handful of the tests fail when ICU is not included.
  common.skip('missing Intl');
}

const fixtures = require('../common/fixtures');
const { test, assert_equals, assert_throws } = require('../common/wpt').harness;

const request = {
  response: require(
    fixtures.path('wpt', 'url', 'resources', 'toascii.json'),
  ),
};

// The following tests are copied from WPT. Modifications to them should be
// upstreamed first.
// Refs: https://github.com/w3c/web-platform-tests/blob/4839a0a804/url/toascii.window.js
// License: http://www.w3.org/Consortium/Legal/2008/04-testsuite-copyright.html

/* eslint-disable */
// async_test(t => {
//   const request = new XMLHttpRequest()
//   request.open("GET", "toascii.json")
//   request.send()
//   request.responseType = "json"
//   request.onload = t.step_func_done(() => {
    runTests(request.response)
//   })
// }, "Loading data…")

function makeURL(type, input) {
  input = "https://" + input + "/x"
  if(type === "url") {
    return new URL(input)
  } else {
    const url = document.createElement(type)
    url.href = input
    return url
  }
}

function runTests(tests) {
  for(var i = 0, l = tests.length; i < l; i++) {
    let hostTest = tests[i]
    if (typeof hostTest === "string") {
      continue // skip comments
    }
    const typeName = { "url": "URL", "a": "<a>", "area": "<area>" }
    // ;["url", "a", "area"].forEach((type) => {
    ;["url"].forEach((type) => {
      test(() => {
        if(hostTest.output !== null) {
          const url = makeURL("url", hostTest.input)
          assert_equals(url.host, hostTest.output)
          assert_equals(url.hostname, hostTest.output)
          assert_equals(url.pathname, "/x")
          assert_equals(url.href, "https://" + hostTest.output + "/x")
        } else {
          if(type === "url") {
            assert_throws(new TypeError, () => makeURL("url", hostTest.input))
          } else {
            const url = makeURL(type, hostTest.input)
            assert_equals(url.host, "")
            assert_equals(url.hostname, "")
            assert_equals(url.pathname, "")
            assert_equals(url.href, "https://" + hostTest.input + "/x")
          }
        }
      }, hostTest.input + " (using " + typeName[type] + ")")
      ;["host", "hostname"].forEach((val) => {
        test(() => {
          const url = makeURL(type, "x")
          url[val] = hostTest.input
          if(hostTest.output !== null) {
            assert_equals(url[val], hostTest.output)
          } else {
            assert_equals(url[val], "x")
          }
        }, hostTest.input + " (using " + typeName[type] + "." + val + ")")
      })
    })
  }
}
/* eslint-enable */
