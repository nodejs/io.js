const kComponents = [
  'protocol',
  'username',
  'password',
  'hostname',
  'port',
  'password',
  'pathname',
  'search',
  'hash',
];

function runTests(data) {
  for (let entry of data) {
    test(function() {
      if (entry.expected_obj === 'error') {
        assert_throws_js(TypeError, _ => new URLPattern(...entry.pattern),
                         'URLPattern() constructor');
        return;
      }

      const pattern = new URLPattern(...entry.pattern);

      // If the expected_obj property is not present we will automatically
      // fill it with the most likely expected values.
      entry.expected_obj = entry.expected_obj || {};

      // The compiled URLPattern object should have a property for each
      // component exposing the compiled pattern string.
      for (let component of kComponents) {
        // If the test case explicitly provides an expected pattern string,
        // then use that.  This is necessary in cases where the original
        // construction pattern gets canonicalized, etc.
        let expected = entry.expected_obj[component];

        // If there is no explicit expected pattern string, then compute
        // the expected value based on the URLPattern constructor args.
        if (expected == undefined) {
          // First determine if there is a baseURL present in the pattern
          // input.  A baseURL can be the source for many component patterns.
          let baseURL = null;
          if (entry.pattern.length > 0 && entry.pattern[0].baseURL) {
            baseURL = new URL(entry.pattern[0].baseURL);
          } else if (entry.pattern.length > 1 &&
                     typeof entry.pattern[1] === 'string') {
            baseURL = new URL(entry.pattern[1]);
          }

          const EARLIER_COMPONENTS = {
            protocol: [],
            hostname: ["protocol"],
            port: ["protocol", "hostname"],
            username: [],
            password: [],
            pathname: ["protocol", "hostname", "port"],
            search: ["protocol", "hostname", "port", "pathname"],
            hash: ["protocol", "hostname", "port", "pathname", "search"],
          };

          // We automatically populate the expected pattern string using
          // the following options in priority order:
          //
          //  1. If the original input explicitly provided a pattern, then
          //     echo that back as the expected value.
          //  2. If an "earlier" component is specified, then a wildcard
          //     will be used rather than inheriting from the base URL.
          //  3. If the baseURL exists and provides a component value then
          //     use that for the expected pattern.
          //  4. Otherwise fall back on the default pattern of `*` for an
          //     empty component pattern.
          //
          // Note that username and password are never inherited, and will only
          // need to match if explicitly specified.
          if (entry.exactly_empty_components &&
              entry.exactly_empty_components.includes(component)) {
            expected = '';
          } else if (typeof entry.pattern[0] === 'object' &&
              entry.pattern[0][component]) {
            expected = entry.pattern[0][component];
          } else if (typeof entry.pattern[0] === 'object' &&
              EARLIER_COMPONENTS[component].some(c => c in entry.pattern[0])) {
            expected = '*';
          } else if (baseURL && component !== 'username' && component !== 'password') {
            let base_value = baseURL[component];
            // Unfortunately some URL() getters include separator chars; e.g.
            // the trailing `:` for the protocol.  Strip those off if necessary.
            if (component === 'protocol')
              base_value = base_value.substring(0, base_value.length - 1);
            else if (component === 'search' || component === 'hash')
              base_value = base_value.substring(1, base_value.length);
            expected = base_value;
          } else {
            expected = '*';
          }
        }

        // Finally, assert that the compiled object property matches the
        // expected property.
        assert_equals(pattern[component], expected,
                      `compiled pattern property '${component}'`);
      }

      if (entry.expected_match === 'error') {
        assert_throws_js(TypeError, _ => pattern.test(...entry.inputs),
                         'test() result');
        assert_throws_js(TypeError, _ => pattern.exec(...entry.inputs),
                         'exec() result');
        return;
      }

      // First, validate the test() method by converting the expected result to
      // a truthy value.
      assert_equals(pattern.test(...entry.inputs), !!entry.expected_match,
                    'test() result');

      // Next, start validating the exec() method.
      const exec_result = pattern.exec(...entry.inputs);

      // On a failed match exec() returns null.
      if (!entry.expected_match || typeof entry.expected_match !== "object") {
        assert_equals(exec_result, entry.expected_match, 'exec() failed match result');
        return;
      }

      if (!entry.expected_match.inputs)
        entry.expected_match.inputs = entry.inputs;

      // Next verify the result.input is correct.  This may be a structured
      // URLPatternInit dictionary object or a URL string.
      assert_equals(exec_result.inputs.length,
                    entry.expected_match.inputs.length,
                    'exec() result.inputs.length');
      for (let i = 0; i < exec_result.inputs.length; ++i) {
        const input = exec_result.inputs[i];
        const expected_input = entry.expected_match.inputs[i];
        if (typeof input === 'string') {
          assert_equals(input, expected_input, `exec() result.inputs[${i}]`);
          continue;
        }
        for (let component of kComponents) {
          assert_equals(input[component], expected_input[component],
                        `exec() result.inputs[${i}][${component}]`);
        }
      }

      // Next we will compare the URLPatternComponentResult for each of these
      // expected components.
      for (let component of kComponents) {
        let expected_obj = entry.expected_match[component];

        // If the test expectations don't include a component object, then
        // we auto-generate one.  This is convenient for the many cases
        // where the pattern has a default wildcard or empty string pattern
        // for a component and the input is essentially empty.
        if (!expected_obj) {
          expected_obj = { input: '', groups: {} };

          // Next, we must treat default wildcards differently than empty string
          // patterns.  The wildcard results in a capture group, but the empty
          // string pattern does not.  The expectation object must list which
          // components should be empty instead of wildcards in
          // |exactly_empty_components|.
          if (!entry.exactly_empty_components ||
              !entry.exactly_empty_components.includes(component)) {
            expected_obj.groups['0'] = '';
          }
        }
        // JSON does not allow us to use undefined directly, so the data file
        // contains null instead.  Translate to the expected undefined value
        // here.
        for (const key in expected_obj.groups) {
          if (expected_obj.groups[key] === null) {
            expected_obj.groups[key] = undefined;
          }
        }
        assert_object_equals(exec_result[component], expected_obj,
                             `exec() result for ${component}`);
      }
    }, `Pattern: ${JSON.stringify(entry.pattern)} ` +
       `Inputs: ${JSON.stringify(entry.inputs)}`);
  }
}

promise_test(async function() {
  const response = await fetch('resources/urlpatterntestdata.json');
  const data = await response.json();
  runTests(data);
}, 'Loading data...');
