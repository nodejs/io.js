/*global self*/
/*jshint latedef: nofunc*/

/* Documentation: https://web-platform-tests.org/writing-tests/testharness-api.html
 * (../docs/_writing-tests/testharness-api.md) */

(function (global_scope)
{
    // default timeout is 10 seconds, test can override if needed
    var settings = {
        output:true,
        harness_timeout:{
            "normal":10000,
            "long":60000
        },
        test_timeout:null,
        message_events: ["start", "test_state", "result", "completion"],
        debug: false,
    };

    var xhtml_ns = "http://www.w3.org/1999/xhtml";

    /*
     * TestEnvironment is an abstraction for the environment in which the test
     * harness is used. Each implementation of a test environment has to provide
     * the following interface:
     *
     * interface TestEnvironment {
     *   // Invoked after the global 'tests' object has been created and it's
     *   // safe to call add_*_callback() to register event handlers.
     *   void on_tests_ready();
     *
     *   // Invoked after setup() has been called to notify the test environment
     *   // of changes to the test harness properties.
     *   void on_new_harness_properties(object properties);
     *
     *   // Should return a new unique default test name.
     *   DOMString next_default_test_name();
     *
     *   // Should return the test harness timeout duration in milliseconds.
     *   float test_timeout();
     * };
     */

    /*
     * A test environment with a DOM. The global object is 'window'. By default
     * test results are displayed in a table. Any parent windows receive
     * callbacks or messages via postMessage() when test events occur. See
     * apisample11.html and apisample12.html.
     */
    function WindowTestEnvironment() {
        this.name_counter = 0;
        this.window_cache = null;
        this.output_handler = null;
        this.all_loaded = false;
        var this_obj = this;
        this.message_events = [];
        this.dispatched_messages = [];

        this.message_functions = {
            start: [add_start_callback, remove_start_callback,
                    function (properties) {
                        this_obj._dispatch("start_callback", [properties],
                                           {type: "start", properties: properties});
                    }],

            test_state: [add_test_state_callback, remove_test_state_callback,
                         function(test) {
                             this_obj._dispatch("test_state_callback", [test],
                                                {type: "test_state",
                                                 test: test.structured_clone()});
                         }],
            result: [add_result_callback, remove_result_callback,
                     function (test) {
                         this_obj.output_handler.show_status();
                         this_obj._dispatch("result_callback", [test],
                                            {type: "result",
                                             test: test.structured_clone()});
                     }],
            completion: [add_completion_callback, remove_completion_callback,
                         function (tests, harness_status, asserts) {
                             var cloned_tests = map(tests, function(test) {
                                 return test.structured_clone();
                             });
                             this_obj._dispatch("completion_callback", [tests, harness_status],
                                                {type: "complete",
                                                 tests: cloned_tests,
                                                 status: harness_status.structured_clone(),
                                                 asserts: asserts.map(assert => assert.structured_clone())});
                         }]
        }

        on_event(window, 'load', function() {
            this_obj.all_loaded = true;
        });

        on_event(window, 'message', function(event) {
            if (event.data && event.data.type === "getmessages" && event.source) {
                // A window can post "getmessages" to receive a duplicate of every
                // message posted by this environment so far. This allows subscribers
                // from fetch_tests_from_window to 'catch up' to the current state of
                // this environment.
                for (var i = 0; i < this_obj.dispatched_messages.length; ++i)
                {
                    event.source.postMessage(this_obj.dispatched_messages[i], "*");
                }
            }
        });
    }

    WindowTestEnvironment.prototype._dispatch = function(selector, callback_args, message_arg) {
        this.dispatched_messages.push(message_arg);
        this._forEach_windows(
                function(w, same_origin) {
                    if (same_origin) {
                        try {
                            var has_selector = selector in w;
                        } catch(e) {
                            // If document.domain was set at some point same_origin can be
                            // wrong and the above will fail.
                            has_selector = false;
                        }
                        if (has_selector) {
                            try {
                                w[selector].apply(undefined, callback_args);
                            } catch (e) {}
                        }
                    }
                    if (w !== self) {
                        w.postMessage(message_arg, "*");
                    }
                });
    };

    WindowTestEnvironment.prototype._forEach_windows = function(callback) {
        // Iterate over the windows [self ... top, opener]. The callback is passed
        // two objects, the first one is the window object itself, the second one
        // is a boolean indicating whether or not it's on the same origin as the
        // current window.
        var cache = this.window_cache;
        if (!cache) {
            cache = [[self, true]];
            var w = self;
            var i = 0;
            var so;
            while (w != w.parent) {
                w = w.parent;
                so = is_same_origin(w);
                cache.push([w, so]);
                i++;
            }
            w = window.opener;
            if (w) {
                cache.push([w, is_same_origin(w)]);
            }
            this.window_cache = cache;
        }

        forEach(cache,
                function(a) {
                    callback.apply(null, a);
                });
    };

    WindowTestEnvironment.prototype.on_tests_ready = function() {
        var output = new Output();
        this.output_handler = output;

        var this_obj = this;

        add_start_callback(function (properties) {
            this_obj.output_handler.init(properties);
        });

        add_test_state_callback(function(test) {
            this_obj.output_handler.show_status();
        });

        add_result_callback(function (test) {
            this_obj.output_handler.show_status();
        });

        add_completion_callback(function (tests, harness_status, asserts_run) {
            this_obj.output_handler.show_results(tests, harness_status, asserts_run);
        });
        this.setup_messages(settings.message_events);
    };

    WindowTestEnvironment.prototype.setup_messages = function(new_events) {
        var this_obj = this;
        forEach(settings.message_events, function(x) {
            var current_dispatch = this_obj.message_events.indexOf(x) !== -1;
            var new_dispatch = new_events.indexOf(x) !== -1;
            if (!current_dispatch && new_dispatch) {
                this_obj.message_functions[x][0](this_obj.message_functions[x][2]);
            } else if (current_dispatch && !new_dispatch) {
                this_obj.message_functions[x][1](this_obj.message_functions[x][2]);
            }
        });
        this.message_events = new_events;
    }

    WindowTestEnvironment.prototype.next_default_test_name = function() {
        var suffix = this.name_counter > 0 ? " " + this.name_counter : "";
        this.name_counter++;
        return get_title() + suffix;
    };

    WindowTestEnvironment.prototype.on_new_harness_properties = function(properties) {
        this.output_handler.setup(properties);
        if (properties.hasOwnProperty("message_events")) {
            this.setup_messages(properties.message_events);
        }
    };

    WindowTestEnvironment.prototype.add_on_loaded_callback = function(callback) {
        on_event(window, 'load', callback);
    };

    WindowTestEnvironment.prototype.test_timeout = function() {
        var metas = document.getElementsByTagName("meta");
        for (var i = 0; i < metas.length; i++) {
            if (metas[i].name == "timeout") {
                if (metas[i].content == "long") {
                    return settings.harness_timeout.long;
                }
                break;
            }
        }
        return settings.harness_timeout.normal;
    };

    /*
     * Base TestEnvironment implementation for a generic web worker.
     *
     * Workers accumulate test results. One or more clients can connect and
     * retrieve results from a worker at any time.
     *
     * WorkerTestEnvironment supports communicating with a client via a
     * MessagePort.  The mechanism for determining the appropriate MessagePort
     * for communicating with a client depends on the type of worker and is
     * implemented by the various specializations of WorkerTestEnvironment
     * below.
     *
     * A client document using testharness can use fetch_tests_from_worker() to
     * retrieve results from a worker. See apisample16.html.
     */
    function WorkerTestEnvironment() {
        this.name_counter = 0;
        this.all_loaded = true;
        this.message_list = [];
        this.message_ports = [];
    }

    WorkerTestEnvironment.prototype._dispatch = function(message) {
        this.message_list.push(message);
        for (var i = 0; i < this.message_ports.length; ++i)
        {
            this.message_ports[i].postMessage(message);
        }
    };

    // The only requirement is that port has a postMessage() method. It doesn't
    // have to be an instance of a MessagePort, and often isn't.
    WorkerTestEnvironment.prototype._add_message_port = function(port) {
        this.message_ports.push(port);
        for (var i = 0; i < this.message_list.length; ++i)
        {
            port.postMessage(this.message_list[i]);
        }
    };

    WorkerTestEnvironment.prototype.next_default_test_name = function() {
        var suffix = this.name_counter > 0 ? " " + this.name_counter : "";
        this.name_counter++;
        return get_title() + suffix;
    };

    WorkerTestEnvironment.prototype.on_new_harness_properties = function() {};

    WorkerTestEnvironment.prototype.on_tests_ready = function() {
        var this_obj = this;
        add_start_callback(
                function(properties) {
                    this_obj._dispatch({
                        type: "start",
                        properties: properties,
                    });
                });
        add_test_state_callback(
                function(test) {
                    this_obj._dispatch({
                        type: "test_state",
                        test: test.structured_clone()
                    });
                });
        add_result_callback(
                function(test) {
                    this_obj._dispatch({
                        type: "result",
                        test: test.structured_clone()
                    });
                });
        add_completion_callback(
                function(tests, harness_status, asserts) {
                    this_obj._dispatch({
                        type: "complete",
                        tests: map(tests,
                            function(test) {
                                return test.structured_clone();
                            }),
                        status: harness_status.structured_clone(),
                        asserts: asserts.map(assert => assert.structured_clone()),
                    });
                });
    };

    WorkerTestEnvironment.prototype.add_on_loaded_callback = function() {};

    WorkerTestEnvironment.prototype.test_timeout = function() {
        // Tests running in a worker don't have a default timeout. I.e. all
        // worker tests behave as if settings.explicit_timeout is true.
        return null;
    };

    /*
     * Dedicated web workers.
     * https://html.spec.whatwg.org/multipage/workers.html#dedicatedworkerglobalscope
     *
     * This class is used as the test_environment when testharness is running
     * inside a dedicated worker.
     */
    function DedicatedWorkerTestEnvironment() {
        WorkerTestEnvironment.call(this);
        // self is an instance of DedicatedWorkerGlobalScope which exposes
        // a postMessage() method for communicating via the message channel
        // established when the worker is created.
        this._add_message_port(self);
    }
    DedicatedWorkerTestEnvironment.prototype = Object.create(WorkerTestEnvironment.prototype);

    DedicatedWorkerTestEnvironment.prototype.on_tests_ready = function() {
        WorkerTestEnvironment.prototype.on_tests_ready.call(this);
        // In the absence of an onload notification, we a require dedicated
        // workers to explicitly signal when the tests are done.
        tests.wait_for_finish = true;
    };

    /*
     * Shared web workers.
     * https://html.spec.whatwg.org/multipage/workers.html#sharedworkerglobalscope
     *
     * This class is used as the test_environment when testharness is running
     * inside a shared web worker.
     */
    function SharedWorkerTestEnvironment() {
        WorkerTestEnvironment.call(this);
        var this_obj = this;
        // Shared workers receive message ports via the 'onconnect' event for
        // each connection.
        self.addEventListener("connect",
                function(message_event) {
                    this_obj._add_message_port(message_event.source);
                }, false);
    }
    SharedWorkerTestEnvironment.prototype = Object.create(WorkerTestEnvironment.prototype);

    SharedWorkerTestEnvironment.prototype.on_tests_ready = function() {
        WorkerTestEnvironment.prototype.on_tests_ready.call(this);
        // In the absence of an onload notification, we a require shared
        // workers to explicitly signal when the tests are done.
        tests.wait_for_finish = true;
    };

    /*
     * Service workers.
     * http://www.w3.org/TR/service-workers/
     *
     * This class is used as the test_environment when testharness is running
     * inside a service worker.
     */
    function ServiceWorkerTestEnvironment() {
        WorkerTestEnvironment.call(this);
        this.all_loaded = false;
        this.on_loaded_callback = null;
        var this_obj = this;
        self.addEventListener("message",
                function(event) {
                    if (event.data && event.data.type && event.data.type === "connect") {
                        this_obj._add_message_port(event.source);
                    }
                }, false);

        // The oninstall event is received after the service worker script and
        // all imported scripts have been fetched and executed. It's the
        // equivalent of an onload event for a document. All tests should have
        // been added by the time this event is received, thus it's not
        // necessary to wait until the onactivate event. However, tests for
        // installed service workers need another event which is equivalent to
        // the onload event because oninstall is fired only on installation. The
        // onmessage event is used for that purpose since tests using
        // testharness.js should ask the result to its service worker by
        // PostMessage. If the onmessage event is triggered on the service
        // worker's context, that means the worker's script has been evaluated.
        on_event(self, "install", on_all_loaded);
        on_event(self, "message", on_all_loaded);
        function on_all_loaded() {
            if (this_obj.all_loaded)
                return;
            this_obj.all_loaded = true;
            if (this_obj.on_loaded_callback) {
              this_obj.on_loaded_callback();
            }
        }
    }

    ServiceWorkerTestEnvironment.prototype = Object.create(WorkerTestEnvironment.prototype);

    ServiceWorkerTestEnvironment.prototype.add_on_loaded_callback = function(callback) {
        if (this.all_loaded) {
            callback();
        } else {
            this.on_loaded_callback = callback;
        }
    };

    /*
     * Shadow realms.
     * https://github.com/tc39/proposal-shadowrealm
     *
     * This class is used as the test_environment when testharness is running
     * inside a shadow realm.
     */
    function ShadowRealmTestEnvironment() {
        WorkerTestEnvironment.call(this);
        this.all_loaded = false;
        this.on_loaded_callback = null;
    }

    ShadowRealmTestEnvironment.prototype = Object.create(WorkerTestEnvironment.prototype);

    /**
     * Signal to the test environment that the tests are ready and the on-loaded
     * callback should be run.
     *
     * Shadow realms are not *really* a DOM context: they have no `onload` or similar
     * event for us to use to set up the test environment; so, instead, this method
     * is manually triggered from the incubating realm
     *
     * @param {Function} message_destination - a function that receives JSON-serializable
     * data to send to the incubating realm, in the same format as used by RemoteContext
     */
    ShadowRealmTestEnvironment.prototype.begin = function(message_destination) {
        if (this.all_loaded) {
            throw new Error("Tried to start a shadow realm test environment after it has already started");
        }
        var fakeMessagePort = {};
        fakeMessagePort.postMessage = message_destination;
        this._add_message_port(fakeMessagePort);
        this.all_loaded = true;
        if (this.on_loaded_callback) {
            this.on_loaded_callback();
        }
    };

    ShadowRealmTestEnvironment.prototype.add_on_loaded_callback = function(callback) {
        if (this.all_loaded) {
            callback();
        } else {
            this.on_loaded_callback = callback;
        }
    };

    /*
     * JavaScript shells.
     *
     * This class is used as the test_environment when testharness is running
     * inside a JavaScript shell.
     */
    function ShellTestEnvironment() {
        this.name_counter = 0;
        this.all_loaded = false;
        this.on_loaded_callback = null;
        Promise.resolve().then(function() {
            this.all_loaded = true
            if (this.on_loaded_callback) {
                this.on_loaded_callback();
            }
        }.bind(this));
        this.message_list = [];
        this.message_ports = [];
    }

    ShellTestEnvironment.prototype.next_default_test_name = function() {
        var suffix = this.name_counter > 0 ? " " + this.name_counter : "";
        this.name_counter++;
        return get_title() + suffix;
    };

    ShellTestEnvironment.prototype.on_new_harness_properties = function() {};

    ShellTestEnvironment.prototype.on_tests_ready = function() {};

    ShellTestEnvironment.prototype.add_on_loaded_callback = function(callback) {
        if (this.all_loaded) {
            callback();
        } else {
            this.on_loaded_callback = callback;
        }
    };

    ShellTestEnvironment.prototype.test_timeout = function() {
        // Tests running in a shell don't have a default timeout, so behave as
        // if settings.explicit_timeout is true.
        return null;
    };

    function create_test_environment() {
        if ('document' in global_scope) {
            return new WindowTestEnvironment();
        }
        if ('DedicatedWorkerGlobalScope' in global_scope &&
            global_scope instanceof DedicatedWorkerGlobalScope) {
            return new DedicatedWorkerTestEnvironment();
        }
        if ('SharedWorkerGlobalScope' in global_scope &&
            global_scope instanceof SharedWorkerGlobalScope) {
            return new SharedWorkerTestEnvironment();
        }
        if ('ServiceWorkerGlobalScope' in global_scope &&
            global_scope instanceof ServiceWorkerGlobalScope) {
            return new ServiceWorkerTestEnvironment();
        }
        if ('WorkerGlobalScope' in global_scope &&
            global_scope instanceof WorkerGlobalScope) {
            return new DedicatedWorkerTestEnvironment();
        }
        /* Shadow realm global objects are _ordinary_ objects (i.e. their prototype is
         * Object) so we don't have a nice `instanceof` test to use; instead, we
         * check if the there is a GLOBAL.isShadowRealm() property
         * on the global object. that was set by the test harness when it
         * created the ShadowRealm.
         */
        if (global_scope.GLOBAL && global_scope.GLOBAL.isShadowRealm()) {
            return new ShadowRealmTestEnvironment();
        }

        return new ShellTestEnvironment();
    }

    var test_environment = create_test_environment();

    function is_shared_worker(worker) {
        return 'SharedWorker' in global_scope && worker instanceof SharedWorker;
    }

    function is_service_worker(worker) {
        // The worker object may be from another execution context,
        // so do not use instanceof here.
        return 'ServiceWorker' in global_scope &&
            Object.prototype.toString.call(worker) == '[object ServiceWorker]';
    }

    var seen_func_name = Object.create(null);

    function get_test_name(func, name)
    {
        if (name) {
            return name;
        }

        if (func) {
            var func_code = func.toString();

            // Try and match with brackets, but fallback to matching without
            var arrow = func_code.match(/^\(\)\s*=>\s*(?:{(.*)}\s*|(.*))$/);

            // Check for JS line separators
            if (arrow !== null && !/[\u000A\u000D\u2028\u2029]/.test(func_code)) {
                var trimmed = (arrow[1] !== undefined ? arrow[1] : arrow[2]).trim();
                // drop trailing ; if there's no earlier ones
                trimmed = trimmed.replace(/^([^;]*)(;\s*)+$/, "$1");

                if (trimmed) {
                    let name = trimmed;
                    if (seen_func_name[trimmed]) {
                        // This subtest name already exists, so add a suffix.
                        name += " " + seen_func_name[trimmed];
                    } else {
                        seen_func_name[trimmed] = 0;
                    }
                    seen_func_name[trimmed] += 1;
                    return name;
                }
            }
        }

        return test_environment.next_default_test_name();
    }

    /**
     * @callback TestFunction
     * @param {Test} test - The test currnetly being run.
     * @param {Any[]} args - Additional args to pass to function.
     *
     */

    /**
     * Create a synchronous test
     *
     * @param {TestFunction} func - Test function. This is executed
     * immediately. If it returns without error, the test status is
     * set to ``PASS``. If it throws an :js:class:`AssertionError`, or
     * any other exception, the test status is set to ``FAIL``
     * (typically from an `assert` function).
     * @param {String} name - Test name. This must be unique in a
     * given file and must be invariant between runs.
     */
    function test(func, name, properties)
    {
        if (tests.promise_setup_called) {
            tests.status.status = tests.status.ERROR;
            tests.status.message = '`test` invoked after `promise_setup`';
            tests.complete();
        }
        var test_name = get_test_name(func, name);
        var test_obj = new Test(test_name, properties);
        var value = test_obj.step(func, test_obj, test_obj);

        if (value !== undefined) {
            var msg = 'Test named "' + test_name +
                '" passed a function to `test` that returned a value.';

            try {
                if (value && typeof value.then === 'function') {
                    msg += ' Consider using `promise_test` instead when ' +
                        'using Promises or async/await.';
                }
            } catch (err) {}

            tests.status.status = tests.status.ERROR;
            tests.status.message = msg;
        }

        if (test_obj.phase === test_obj.phases.STARTED) {
            test_obj.done();
        }
    }

    /**
     * Create an asynchronous test
     *
     * @param {TestFunction|string} funcOrName - Initial step function
     * to call immediately with the test name as an argument (if any),
     * or name of the test.
     * @param {String} name - Test name (if a test function was
     * provided). This must be unique in a given file and must be
     * invariant between runs.
     * @returns {Test} An object representing the ongoing test.
     */
    function async_test(func, name, properties)
    {
        if (tests.promise_setup_called) {
            tests.status.status = tests.status.ERROR;
            tests.status.message = '`async_test` invoked after `promise_setup`';
            tests.complete();
        }
        if (typeof func !== "function") {
            properties = name;
            name = func;
            func = null;
        }
        var test_name = get_test_name(func, name);
        var test_obj = new Test(test_name, properties);
        if (func) {
            var value = test_obj.step(func, test_obj, test_obj);

            // Test authors sometimes return values to async_test, expecting us
            // to handle the value somehow. Make doing so a harness error to be
            // clear this is invalid, and point authors to promise_test if it
            // may be appropriate.
            //
            // Note that we only perform this check on the initial function
            // passed to async_test, not on any later steps - we haven't seen a
            // consistent problem with those (and it's harder to check).
            if (value !== undefined) {
                var msg = 'Test named "' + test_name +
                    '" passed a function to `async_test` that returned a value.';

                try {
                    if (value && typeof value.then === 'function') {
                        msg += ' Consider using `promise_test` instead when ' +
                            'using Promises or async/await.';
                    }
                } catch (err) {}

                tests.set_status(tests.status.ERROR, msg);
                tests.complete();
            }
        }
        return test_obj;
    }

    /**
     * Create a promise test.
     *
     * Promise tests are tests which are represented by a promise
     * object. If the promise is fulfilled the test passes, if it's
     * rejected the test fails, otherwise the test passes.
     *
     * @param {TestFunction} func - Test function. This must return a
     * promise. The test is automatically marked as complete once the
     * promise settles.
     * @param {String} name - Test name. This must be unique in a
     * given file and must be invariant between runs.
     */
    function promise_test(func, name, properties) {
        if (typeof func !== "function") {
            properties = name;
            name = func;
            func = null;
        }
        var test_name = get_test_name(func, name);
        var test = new Test(test_name, properties);
        test._is_promise_test = true;

        // If there is no promise tests queue make one.
        if (!tests.promise_tests) {
            tests.promise_tests = Promise.resolve();
        }
        tests.promise_tests = tests.promise_tests.then(function() {
            return new Promise(function(resolve) {
                var promise = test.step(func, test, test);

                test.step(function() {
                    assert(!!promise, "promise_test", null,
                           "test body must return a 'thenable' object (received ${value})",
                           {value:promise});
                    assert(typeof promise.then === "function", "promise_test", null,
                           "test body must return a 'thenable' object (received an object with no `then` method)",
                           null);
                });

                // Test authors may use the `step` method within a
                // `promise_test` even though this reflects a mixture of
                // asynchronous control flow paradigms. The "done" callback
                // should be registered prior to the resolution of the
                // user-provided Promise to avoid timeouts in cases where the
                // Promise does not settle but a `step` function has thrown an
                // error.
                add_test_done_callback(test, resolve);

                Promise.resolve(promise)
                    .catch(test.step_func(
                        function(value) {
                            if (value instanceof AssertionError) {
                                throw value;
                            }
                            assert(false, "promise_test", null,
                                   "Unhandled rejection with value: ${value}", {value:value});
                        }))
                    .then(function() {
                        test.done();
                    });
                });
        });
    }

    /**
     * Make a copy of a Promise in the current realm.
     *
     * @param {Promise} promise the given promise that may be from a different
     *                          realm
     * @returns {Promise}
     *
     * An arbitrary promise provided by the caller may have originated
     * in another frame that have since navigated away, rendering the
     * frame's document inactive. Such a promise cannot be used with
     * `await` or Promise.resolve(), as microtasks associated with it
     * may be prevented from being run. See `issue
     * 5319<https://github.com/whatwg/html/issues/5319>`_ for a
     * particular case.
     *
     * In functions we define here, there is an expectation from the caller
     * that the promise is from the current realm, that can always be used with
     * `await`, etc. We therefore create a new promise in this realm that
     * inherit the value and status from the given promise.
     */

    function bring_promise_to_current_realm(promise) {
        return new Promise(promise.then.bind(promise));
    }

    /**
     * Assert that a Promise is rejected with the right ECMAScript exception.
     *
     * @param {Test} test - the `Test` to use for the assertion.
     * @param {Function} constructor - The expected exception constructor.
     * @param {Promise} promise - The promise that's expected to
     * reject with the given exception.
     * @param {string} [description] Error message to add to assert in case of
     *                               failure.
     */
    function promise_rejects_js(test, constructor, promise, description) {
        return bring_promise_to_current_realm(promise)
            .then(test.unreached_func("Should have rejected: " + description))
            .catch(function(e) {
                assert_throws_js_impl(constructor, function() { throw e },
                                      description, "promise_rejects_js");
            });
    }

    /**
     * Assert that a Promise is rejected with the right DOMException.
     *
     * For the remaining arguments, there are two ways of calling
     * promise_rejects_dom:
     *
     * 1) If the DOMException is expected to come from the current global, the
     * third argument should be the promise expected to reject, and a fourth,
     * optional, argument is the assertion description.
     *
     * 2) If the DOMException is expected to come from some other global, the
     * third argument should be the DOMException constructor from that global,
     * the fourth argument the promise expected to reject, and the fifth,
     * optional, argument the assertion description.
     *
     * @param {Test} test - the `Test` to use for the assertion.
     * @param {number|string} type - See documentation for
     * `assert_throws_dom <#assert_throws_dom>`_.
     * @param {Function} promiseOrConstructor - Either the constructor
     * for the expected exception (if the exception comes from another
     * global), or the promise that's expected to reject (if the
     * exception comes from the current global).
     * @param {Function|string} descriptionOrPromise - Either the
     * promise that's expected to reject (if the exception comes from
     * another global), or the optional description of the condition
     * being tested (if the exception comes from the current global).
     * @param {string} [description] - Description of the condition
     * being tested (if the exception comes from another global).
     *
     */
    function promise_rejects_dom(test, type, promiseOrConstructor, descriptionOrPromise, maybeDescription) {
        let constructor, promise, description;
        if (typeof promiseOrConstructor === "function" &&
            promiseOrConstructor.name === "DOMException") {
            constructor = promiseOrConstructor;
            promise = descriptionOrPromise;
            description = maybeDescription;
        } else {
            constructor = self.DOMException;
            promise = promiseOrConstructor;
            description = descriptionOrPromise;
            assert(maybeDescription === undefined,
                   "Too many args pased to no-constructor version of promise_rejects_dom");
        }
        return bring_promise_to_current_realm(promise)
            .then(test.unreached_func("Should have rejected: " + description))
            .catch(function(e) {
                assert_throws_dom_impl(type, function() { throw e }, description,
                                       "promise_rejects_dom", constructor);
            });
    }

    /**
     * Assert that a Promise is rejected with the provided value.
     *
     * @param {Test} test - the `Test` to use for the assertion.
     * @param {Any} exception - The expected value of the rejected promise.
     * @param {Promise} promise - The promise that's expected to
     * reject.
     * @param {string} [description] Error message to add to assert in case of
     *                               failure.
     */
    function promise_rejects_exactly(test, exception, promise, description) {
        return bring_promise_to_current_realm(promise)
            .then(test.unreached_func("Should have rejected: " + description))
            .catch(function(e) {
                assert_throws_exactly_impl(exception, function() { throw e },
                                           description, "promise_rejects_exactly");
            });
    }

    /**
     * Allow DOM events to be handled using Promises.
     *
     * This can make it a lot easier to test a very specific series of events,
     * including ensuring that unexpected events are not fired at any point.
     *
     * `EventWatcher` will assert if an event occurs while there is no `wait_for`
     * created Promise waiting to be fulfilled, or if the event is of a different type
     * to the type currently expected. This ensures that only the events that are
     * expected occur, in the correct order, and with the correct timing.
     *
     * @constructor
     * @param {Test} test - The `Test` to use for the assertion.
     * @param {EventTarget} watchedNode - The target expected to receive the events.
     * @param {string[]} eventTypes - List of events to watch for.
     * @param {Promise} timeoutPromise - Promise that will cause the
     * test to be set to `TIMEOUT` once fulfilled.
     *
     */
    function EventWatcher(test, watchedNode, eventTypes, timeoutPromise)
    {
        if (typeof eventTypes == 'string') {
            eventTypes = [eventTypes];
        }

        var waitingFor = null;

        // This is null unless we are recording all events, in which case it
        // will be an Array object.
        var recordedEvents = null;

        var eventHandler = test.step_func(function(evt) {
            assert_true(!!waitingFor,
                        'Not expecting event, but got ' + evt.type + ' event');
            assert_equals(evt.type, waitingFor.types[0],
                          'Expected ' + waitingFor.types[0] + ' event, but got ' +
                          evt.type + ' event instead');

            if (Array.isArray(recordedEvents)) {
                recordedEvents.push(evt);
            }

            if (waitingFor.types.length > 1) {
                // Pop first event from array
                waitingFor.types.shift();
                return;
            }
            // We need to null out waitingFor before calling the resolve function
            // since the Promise's resolve handlers may call wait_for() which will
            // need to set waitingFor.
            var resolveFunc = waitingFor.resolve;
            waitingFor = null;
            // Likewise, we should reset the state of recordedEvents.
            var result = recordedEvents || evt;
            recordedEvents = null;
            resolveFunc(result);
        });

        for (var i = 0; i < eventTypes.length; i++) {
            watchedNode.addEventListener(eventTypes[i], eventHandler, false);
        }

        /**
         * Returns a Promise that will resolve after the specified event or
         * series of events has occurred.
         *
         * @param {Object} options An optional options object. If the 'record' property
         *                 on this object has the value 'all', when the Promise
         *                 returned by this function is resolved,  *all* Event
         *                 objects that were waited for will be returned as an
         *                 array.
         *
         * @example
         * const watcher = new EventWatcher(t, div, [ 'animationstart',
         *                                            'animationiteration',
         *                                            'animationend' ]);
         * return watcher.wait_for([ 'animationstart', 'animationend' ],
         *                         { record: 'all' }).then(evts => {
         *   assert_equals(evts[0].elapsedTime, 0.0);
         *   assert_equals(evts[1].elapsedTime, 2.0);
         * });
         */
        this.wait_for = function(types, options) {
            if (waitingFor) {
                return Promise.reject('Already waiting for an event or events');
            }
            if (typeof types == 'string') {
                types = [types];
            }
            if (options && options.record && options.record === 'all') {
                recordedEvents = [];
            }
            return new Promise(function(resolve, reject) {
                var timeout = test.step_func(function() {
                    // If the timeout fires after the events have been received
                    // or during a subsequent call to wait_for, ignore it.
                    if (!waitingFor || waitingFor.resolve !== resolve)
                        return;

                    // This should always fail, otherwise we should have
                    // resolved the promise.
                    assert_true(waitingFor.types.length == 0,
                                'Timed out waiting for ' + waitingFor.types.join(', '));
                    var result = recordedEvents;
                    recordedEvents = null;
                    var resolveFunc = waitingFor.resolve;
                    waitingFor = null;
                    resolveFunc(result);
                });

                if (timeoutPromise) {
                    timeoutPromise().then(timeout);
                }

                waitingFor = {
                    types: types,
                    resolve: resolve,
                    reject: reject
                };
            });
        };

        /**
         * Stop listening for events
         */
        function stop_watching() {
            for (var i = 0; i < eventTypes.length; i++) {
                watchedNode.removeEventListener(eventTypes[i], eventHandler, false);
            }
        };

        test._add_cleanup(stop_watching);

        return this;
    }
    expose(EventWatcher, 'EventWatcher');

    /**
     * @typedef {Object} SettingsObject
     * @property {bool} single_test - Use the single-page-test
     * mode. In this mode the Document represents a single
     * `async_test`. Asserts may be used directly without requiring
     * `Test.step` or similar wrappers, and any exceptions set the
     * status of the test rather than the status of the harness.
     * @property {bool} allow_uncaught_exception - don't treat an
     * uncaught exception as an error; needed when e.g. testing the
     * `window.onerror` handler.
     * @property {boolean} explicit_done - Wait for a call to `done()`
     * before declaring all tests complete (this is always true for
     * single-page tests).
     * @property hide_test_state - hide the test state output while
     * the test is running; This is helpful when the output of the test state
     * may interfere the test results.
     * @property {bool} explicit_timeout - disable file timeout; only
     * stop waiting for results when the `timeout()` function is
     * called This should typically only be set for manual tests, or
     * by a test runner that providees its own timeout mechanism.
     * @property {number} timeout_multiplier - Multiplier to apply to
     * per-test timeouts. This should only be set by a test runner.
     * @property {Document} output_document - The document to which
     * results should be logged. By default this is the current
     * document but could be an ancestor document in some cases e.g. a
     * SVG test loaded in an HTML wrapper
     *
     */

    /**
     * Configure the harness
     *
     * @param {Function|SettingsObject} funcOrProperties - Either a
     * setup function to run, or a set of properties. If this is a
     * function that function is run synchronously. Any exception in
     * the function will set the overall harness status to `ERROR`.
     * @param {SettingsObject} maybeProperties - An object containing
     * the settings to use, if the first argument is a function.
     *
     */
    function setup(func_or_properties, maybe_properties)
    {
        var func = null;
        var properties = {};
        if (arguments.length === 2) {
            func = func_or_properties;
            properties = maybe_properties;
        } else if (func_or_properties instanceof Function) {
            func = func_or_properties;
        } else {
            properties = func_or_properties;
        }
        tests.setup(func, properties);
        test_environment.on_new_harness_properties(properties);
    }

    /**
     * Configure the harness, waiting for a promise to resolve
     * before running any `promise_test` tests.
     *
     * @param {Function} func - Function returning a promise that's
     * run synchronously. Promise tests are not run until after this
     * function has resolved.
     * @param {SettingsObject} [properties] - An object containing
     * the harness settings to use.
     *
     */
    function promise_setup(func, properties={})
    {
        if (typeof func !== "function") {
            tests.set_status(tests.status.ERROR,
                             "promise_test invoked without a function");
            tests.complete();
            return;
        }
        tests.promise_setup_called = true;

        if (!tests.promise_tests) {
            tests.promise_tests = Promise.resolve();
        }

        tests.promise_tests = tests.promise_tests
            .then(function()
                  {
                      var result;

                      tests.setup(null, properties);
                      result = func();
                      test_environment.on_new_harness_properties(properties);

                      if (!result || typeof result.then !== "function") {
                          throw "Non-thenable returned by function passed to `promise_setup`";
                      }
                      return result;
                  })
            .catch(function(e)
                   {
                       tests.set_status(tests.status.ERROR,
                                        String(e),
                                        e && e.stack);
                       tests.complete();
                   });
    }

    /**
     * Mark test loading as complete.
     *
     * Typically this function is called implicitly on page load; it's
     * only necessary for users to call this when either the
     * ``explicit_done`` or ``single_page`` properties have been set
     * via the :js:func:`setup` function.
     *
     * For single page tests this marks the test as complete and sets its status.
     * For other tests, this marks test loading as complete, but doesn't affect ongoing tests.
     */
    function done() {
        if (tests.tests.length === 0) {
            // `done` is invoked after handling uncaught exceptions, so if the
            // harness status is already set, the corresponding message is more
            // descriptive than the generic message defined here.
            if (tests.status.status === null) {
                tests.status.status = tests.status.ERROR;
                tests.status.message = "done() was called without first defining any tests";
            }

            tests.complete();
            return;
        }
        if (tests.file_is_test) {
            // file is test files never have asynchronous cleanup logic,
            // meaning the fully-synchronous `done` function can be used here.
            tests.tests[0].done();
        }
        tests.end_wait();
    }

    /**
     * @deprecated generate a list of tests from a function and list of arguments
     *
     * This is deprecated because it runs all the tests outside of the test functions
     * and as a result any test throwing an exception will result in no tests being
     * run. In almost all cases, you should simply call test within the loop you would
     * use to generate the parameter list array.
     *
     * @param {Function} func - The function that will be called for each generated tests.
     * @param {Any[][]} args - An array of arrays. Each nested array
     * has the structure `[testName, ...testArgs]`. For each of these nested arrays
     * array, a test is generated with name `testName` and test function equivalent to
     * `func(..testArgs)`.
     */
    function generate_tests(func, args, properties) {
        forEach(args, function(x, i)
                {
                    var name = x[0];
                    test(function()
                         {
                             func.apply(this, x.slice(1));
                         },
                         name,
                         Array.isArray(properties) ? properties[i] : properties);
                });
    }

    /**
     * @deprecated
     *
     * Register a function as a DOM event listener to the
     * given object for the event bubbling phase.
     *
     * @param {EventTarget} object - Event target
     * @param {string} event - Event name
     * @param {Function} callback - Event handler.
     */
    function on_event(object, event, callback)
    {
        object.addEventListener(event, callback, false);
    }

    /**
     * Global version of :js:func:`Test.step_timeout` for use in single page tests.
     *
     * @param {Function} func - Function to run after the timeout
     * @param {number} timeout - Time in ms to wait before running the
     * test step. The actual wait time is ``timeout`` x
     * ``timeout_multiplier``.
     */
    function step_timeout(func, timeout) {
        var outer_this = this;
        var args = Array.prototype.slice.call(arguments, 2);
        return setTimeout(function() {
            func.apply(outer_this, args);
        }, timeout * tests.timeout_multiplier);
    }

    expose(test, 'test');
    expose(async_test, 'async_test');
    expose(promise_test, 'promise_test');
    expose(promise_rejects_js, 'promise_rejects_js');
    expose(promise_rejects_dom, 'promise_rejects_dom');
    expose(promise_rejects_exactly, 'promise_rejects_exactly');
    expose(generate_tests, 'generate_tests');
    expose(setup, 'setup');
    expose(promise_setup, 'promise_setup');
    expose(done, 'done');
    expose(on_event, 'on_event');
    expose(step_timeout, 'step_timeout');

    /*
     * Return a string truncated to the given length, with ... added at the end
     * if it was longer.
     */
    function truncate(s, len)
    {
        if (s.length > len) {
            return s.substring(0, len - 3) + "...";
        }
        return s;
    }

    /*
     * Return true if object is probably a Node object.
     */
    function is_node(object)
    {
        // I use duck-typing instead of instanceof, because
        // instanceof doesn't work if the node is from another window (like an
        // iframe's contentWindow):
        // http://www.w3.org/Bugs/Public/show_bug.cgi?id=12295
        try {
            var has_node_properties = ("nodeType" in object &&
                                       "nodeName" in object &&
                                       "nodeValue" in object &&
                                       "childNodes" in object);
        } catch (e) {
            // We're probably cross-origin, which means we aren't a node
            return false;
        }

        if (has_node_properties) {
            try {
                object.nodeType;
            } catch (e) {
                // The object is probably Node.prototype or another prototype
                // object that inherits from it, and not a Node instance.
                return false;
            }
            return true;
        }
        return false;
    }

    var replacements = {
        "0": "0",
        "1": "x01",
        "2": "x02",
        "3": "x03",
        "4": "x04",
        "5": "x05",
        "6": "x06",
        "7": "x07",
        "8": "b",
        "9": "t",
        "10": "n",
        "11": "v",
        "12": "f",
        "13": "r",
        "14": "x0e",
        "15": "x0f",
        "16": "x10",
        "17": "x11",
        "18": "x12",
        "19": "x13",
        "20": "x14",
        "21": "x15",
        "22": "x16",
        "23": "x17",
        "24": "x18",
        "25": "x19",
        "26": "x1a",
        "27": "x1b",
        "28": "x1c",
        "29": "x1d",
        "30": "x1e",
        "31": "x1f",
        "0xfffd": "ufffd",
        "0xfffe": "ufffe",
        "0xffff": "uffff",
    };

    /**
     * Convert a value to a nice, human-readable string
     *
     * When many JavaScript Object values are coerced to a String, the
     * resulting value will be ``"[object Object]"``. This obscures
     * helpful information, making the coerced value unsuitable for
     * use in assertion messages, test names, and debugging
     * statements. `format_value` produces more distinctive string
     * representations of many kinds of objects, including arrays and
     * the more important DOM Node types. It also translates String
     * values containing control characters to include human-readable
     * representations.
     *
     * @example
     * // "Document node with 2 children"
     * format_value(document);
     * @example
     * // "\"foo\\uffffbar\""
     * format_value("foo\uffffbar");
     * @example
     * // "[-0, Infinity]"
     * format_value([-0, Infinity]);
     * @param {Any} val - The value to convert to a string.
     * @returns {string} - A string representation of ``val``, optimised for human readability.
     */
    function format_value(val, seen)
    {
        if (!seen) {
            seen = [];
        }
        if (typeof val === "object" && val !== null) {
            if (seen.indexOf(val) >= 0) {
                return "[...]";
            }
            seen.push(val);
        }
        if (Array.isArray(val)) {
            let output = "[";
            if (val.beginEllipsis !== undefined) {
                output += "…, ";
            }
            output += val.map(function(x) {return format_value(x, seen);}).join(", ");
            if (val.endEllipsis !== undefined) {
                output += ", …";
            }
            return output + "]";
        }

        switch (typeof val) {
        case "string":
            val = val.replace(/\\/g, "\\\\");
            for (var p in replacements) {
                var replace = "\\" + replacements[p];
                val = val.replace(RegExp(String.fromCharCode(p), "g"), replace);
            }
            return '"' + val.replace(/"/g, '\\"') + '"';
        case "boolean":
        case "undefined":
            return String(val);
        case "number":
            // In JavaScript, -0 === 0 and String(-0) == "0", so we have to
            // special-case.
            if (val === -0 && 1/val === -Infinity) {
                return "-0";
            }
            return String(val);
        case "object":
            if (val === null) {
                return "null";
            }

            // Special-case Node objects, since those come up a lot in my tests.  I
            // ignore namespaces.
            if (is_node(val)) {
                switch (val.nodeType) {
                case Node.ELEMENT_NODE:
                    var ret = "<" + val.localName;
                    for (var i = 0; i < val.attributes.length; i++) {
                        ret += " " + val.attributes[i].name + '="' + val.attributes[i].value + '"';
                    }
                    ret += ">" + val.innerHTML + "</" + val.localName + ">";
                    return "Element node " + truncate(ret, 60);
                case Node.TEXT_NODE:
                    return 'Text node "' + truncate(val.data, 60) + '"';
                case Node.PROCESSING_INSTRUCTION_NODE:
                    return "ProcessingInstruction node with target " + format_value(truncate(val.target, 60)) + " and data " + format_value(truncate(val.data, 60));
                case Node.COMMENT_NODE:
                    return "Comment node <!--" + truncate(val.data, 60) + "-->";
                case Node.DOCUMENT_NODE:
                    return "Document node with " + val.childNodes.length + (val.childNodes.length == 1 ? " child" : " children");
                case Node.DOCUMENT_TYPE_NODE:
                    return "DocumentType node";
                case Node.DOCUMENT_FRAGMENT_NODE:
                    return "DocumentFragment node with " + val.childNodes.length + (val.childNodes.length == 1 ? " child" : " children");
                default:
                    return "Node object of unknown type";
                }
            }

        /* falls through */
        default:
            try {
                return typeof val + ' "' + truncate(String(val), 1000) + '"';
            } catch(e) {
                return ("[stringifying object threw " + String(e) +
                        " with type " + String(typeof e) + "]");
            }
        }
    }
    expose(format_value, "format_value");

    /*
     * Assertions
     */

    function expose_assert(f, name) {
        function assert_wrapper(...args) {
            let status = Test.statuses.TIMEOUT;
            let stack = null;
            let new_assert_index = null;
            try {
                if (settings.debug) {
                    console.debug("ASSERT", name, tests.current_test && tests.current_test.name, args);
                }
                if (tests.output) {
                    tests.set_assert(name, args);
                    // Remember the newly pushed assert's index, because `apply`
                    // below might push new asserts.
                    new_assert_index = tests.asserts_run.length - 1;
                }
                const rv = f.apply(undefined, args);
                status = Test.statuses.PASS;
                return rv;
            } catch(e) {
                status = Test.statuses.FAIL;
                stack = e.stack ? e.stack : null;
                throw e;
            } finally {
                if (tests.output && !stack) {
                    stack = get_stack();
                }
                if (tests.output) {
                    tests.set_assert_status(new_assert_index, status, stack);
                }
            }
        }
        expose(assert_wrapper, name);
    }

    /**
     * Assert that ``actual`` is strictly true
     *
     * @param {Any} actual - Value that is asserted to be true
     * @param {string} [description] - Description of the condition being tested
     */
    function assert_true(actual, description)
    {
        assert(actual === true, "assert_true", description,
                                "expected true got ${actual}", {actual:actual});
    }
    expose_assert(assert_true, "assert_true");

    /**
     * Assert that ``actual`` is strictly false
     *
     * @param {Any} actual - Value that is asserted to be false
     * @param {string} [description] - Description of the condition being tested
     */
    function assert_false(actual, description)
    {
        assert(actual === false, "assert_false", description,
                                 "expected false got ${actual}", {actual:actual});
    }
    expose_assert(assert_false, "assert_false");

    function same_value(x, y) {
        if (y !== y) {
            //NaN case
            return x !== x;
        }
        if (x === 0 && y === 0) {
            //Distinguish +0 and -0
            return 1/x === 1/y;
        }
        return x === y;
    }

    /**
     * Assert that ``actual`` is the same value as ``expected``.
     *
     * For objects this compares by object identity; for primitives
     * this distinguishes between 0 and -0, and has correct handling
     * of NaN.
     *
     * @param {Any} actual - Test value.
     * @param {Any} expected - Expected value.
     * @param {string} [description] - Description of the condition being tested.
     */
    function assert_equals(actual, expected, description)
    {
         /*
          * Test if two primitives are equal or two objects
          * are the same object
          */
        if (typeof actual != typeof expected) {
            assert(false, "assert_equals", description,
                          "expected (" + typeof expected + ") ${expected} but got (" + typeof actual + ") ${actual}",
                          {expected:expected, actual:actual});
            return;
        }
        assert(same_value(actual, expected), "assert_equals", description,
                                             "expected ${expected} but got ${actual}",
                                             {expected:expected, actual:actual});
    }
    expose_assert(assert_equals, "assert_equals");

    /**
     * Assert that ``actual`` is not the same value as ``expected``.
     *
     * Comparison is as for :js:func:`assert_equals`.
     *
     * @param {Any} actual - Test value.
     * @param {Any} expected - The value ``actual`` is expected to be different to.
     * @param {string} [description] - Description of the condition being tested.
     */
    function assert_not_equals(actual, expected, description)
    {
        assert(!same_value(actual, expected), "assert_not_equals", description,
                                              "got disallowed value ${actual}",
                                              {actual:actual});
    }
    expose_assert(assert_not_equals, "assert_not_equals");

    /**
     * Assert that ``expected`` is an array and ``actual`` is one of the members.
     * This is implemented using ``indexOf``, so doesn't handle NaN or ±0 correctly.
     *
     * @param {Any} actual - Test value.
     * @param {Array} expected - An array that ``actual`` is expected to
     * be a member of.
     * @param {string} [description] - Description of the condition being tested.
     */
    function assert_in_array(actual, expected, description)
    {
        assert(expected.indexOf(actual) != -1, "assert_in_array", description,
                                               "value ${actual} not in array ${expected}",
                                               {actual:actual, expected:expected});
    }
    expose_assert(assert_in_array, "assert_in_array");

    // This function was deprecated in July of 2015.
    // See https://github.com/web-platform-tests/wpt/issues/2033
    /**
     * @deprecated
     * Recursively compare two objects for equality.
     *
     * See `Issue 2033
     * <https://github.com/web-platform-tests/wpt/issues/2033>`_ for
     * more information.
     *
     * @param {Object} actual - Test value.
     * @param {Object} expected - Expected value.
     * @param {string} [description] - Description of the condition being tested.
     */
    function assert_object_equals(actual, expected, description)
    {
         assert(typeof actual === "object" && actual !== null, "assert_object_equals", description,
                                                               "value is ${actual}, expected object",
                                                               {actual: actual});
         //This needs to be improved a great deal
         function check_equal(actual, expected, stack)
         {
             stack.push(actual);

             var p;
             for (p in actual) {
                 assert(expected.hasOwnProperty(p), "assert_object_equals", description,
                                                    "unexpected property ${p}", {p:p});

                 if (typeof actual[p] === "object" && actual[p] !== null) {
                     if (stack.indexOf(actual[p]) === -1) {
                         check_equal(actual[p], expected[p], stack);
                     }
                 } else {
                     assert(same_value(actual[p], expected[p]), "assert_object_equals", description,
                                                       "property ${p} expected ${expected} got ${actual}",
                                                       {p:p, expected:expected[p], actual:actual[p]});
                 }
             }
             for (p in expected) {
                 assert(actual.hasOwnProperty(p),
                        "assert_object_equals", description,
                        "expected property ${p} missing", {p:p});
             }
             stack.pop();
         }
         check_equal(actual, expected, []);
    }
    expose_assert(assert_object_equals, "assert_object_equals");

    /**
     * Assert that ``actual`` and ``expected`` are both arrays, and that the array properties of
     * ``actual`` and ``expected`` are all the same value (as for :js:func:`assert_equals`).
     *
     * @param {Array} actual - Test array.
     * @param {Array} expected - Array that is expected to contain the same values as ``actual``.
     * @param {string} [description] - Description of the condition being tested.
     */
    function assert_array_equals(actual, expected, description)
    {
        const max_array_length = 20;
        function shorten_array(arr, offset = 0) {
            // Make ", …" only show up when it would likely reduce the length, not accounting for
            // fonts.
            if (arr.length < max_array_length + 2) {
                return arr;
            }
            // By default we want half the elements after the offset and half before
            // But if that takes us past the end of the array, we have more before, and
            // if it takes us before the start we have more after.
            const length_after_offset = Math.floor(max_array_length / 2);
            let upper_bound = Math.min(length_after_offset + offset, arr.length);
            const lower_bound = Math.max(upper_bound - max_array_length, 0);

            if (lower_bound === 0) {
                upper_bound = max_array_length;
            }

            const output = arr.slice(lower_bound, upper_bound);
            if (lower_bound > 0) {
                output.beginEllipsis = true;
            }
            if (upper_bound < arr.length) {
                output.endEllipsis = true;
            }
            return output;
        }

        assert(typeof actual === "object" && actual !== null && "length" in actual,
               "assert_array_equals", description,
               "value is ${actual}, expected array",
               {actual:actual});
        assert(actual.length === expected.length,
               "assert_array_equals", description,
               "lengths differ, expected array ${expected} length ${expectedLength}, got ${actual} length ${actualLength}",
               {expected:shorten_array(expected, expected.length - 1), expectedLength:expected.length,
                actual:shorten_array(actual, actual.length - 1), actualLength:actual.length
               });

        for (var i = 0; i < actual.length; i++) {
            assert(actual.hasOwnProperty(i) === expected.hasOwnProperty(i),
                   "assert_array_equals", description,
                   "expected property ${i} to be ${expected} but was ${actual} (expected array ${arrayExpected} got ${arrayActual})",
                   {i:i, expected:expected.hasOwnProperty(i) ? "present" : "missing",
                    actual:actual.hasOwnProperty(i) ? "present" : "missing",
                    arrayExpected:shorten_array(expected, i), arrayActual:shorten_array(actual, i)});
            assert(same_value(expected[i], actual[i]),
                   "assert_array_equals", description,
                   "expected property ${i} to be ${expected} but got ${actual} (expected array ${arrayExpected} got ${arrayActual})",
                   {i:i, expected:expected[i], actual:actual[i],
                    arrayExpected:shorten_array(expected, i), arrayActual:shorten_array(actual, i)});
        }
    }
    expose_assert(assert_array_equals, "assert_array_equals");

    /**
     * Assert that each array property in ``actual`` is a number within
     * ± `epsilon` of the corresponding property in `expected`.
     *
     * @param {Array} actual - Array of test values.
     * @param {Array} expected - Array of values expected to be close to the values in ``actual``.
     * @param {number} epsilon - Magnitude of allowed difference
     * between each value in ``actual`` and ``expected``.
     * @param {string} [description] - Description of the condition being tested.
     */
    function assert_array_approx_equals(actual, expected, epsilon, description)
    {
        /*
         * Test if two primitive arrays are equal within +/- epsilon
         */
        assert(actual.length === expected.length,
               "assert_array_approx_equals", description,
               "lengths differ, expected ${expected} got ${actual}",
               {expected:expected.length, actual:actual.length});

        for (var i = 0; i < actual.length; i++) {
            assert(actual.hasOwnProperty(i) === expected.hasOwnProperty(i),
                   "assert_array_approx_equals", description,
                   "property ${i}, property expected to be ${expected} but was ${actual}",
                   {i:i, expected:expected.hasOwnProperty(i) ? "present" : "missing",
                    actual:actual.hasOwnProperty(i) ? "present" : "missing"});
            assert(typeof actual[i] === "number",
                   "assert_array_approx_equals", description,
                   "property ${i}, expected a number but got a ${type_actual}",
                   {i:i, type_actual:typeof actual[i]});
            assert(Math.abs(actual[i] - expected[i]) <= epsilon,
                   "assert_array_approx_equals", description,
                   "property ${i}, expected ${expected} +/- ${epsilon}, expected ${expected} but got ${actual}",
                   {i:i, expected:expected[i], actual:actual[i], epsilon:epsilon});
        }
    }
    expose_assert(assert_array_approx_equals, "assert_array_approx_equals");

    /**
     * Assert that ``actual`` is within ± ``epsilon`` of ``expected``.
     *
     * @param {number} actual - Test value.
     * @param {number} expected - Value number is expected to be close to.
     * @param {number} epsilon - Magnitude of allowed difference between ``actual`` and ``expected``.
     * @param {string} [description] - Description of the condition being tested.
     */
    function assert_approx_equals(actual, expected, epsilon, description)
    {
        /*
         * Test if two primitive numbers are equal within +/- epsilon
         */
        assert(typeof actual === "number",
               "assert_approx_equals", description,
               "expected a number but got a ${type_actual}",
               {type_actual:typeof actual});

        // The epsilon math below does not place nice with NaN and Infinity
        // But in this case Infinity = Infinity and NaN = NaN
        if (isFinite(actual) || isFinite(expected)) {
            assert(Math.abs(actual - expected) <= epsilon,
                   "assert_approx_equals", description,
                   "expected ${expected} +/- ${epsilon} but got ${actual}",
                   {expected:expected, actual:actual, epsilon:epsilon});
        } else {
            assert_equals(actual, expected);
        }
    }
    expose_assert(assert_approx_equals, "assert_approx_equals");

    /**
     * Assert that ``actual`` is a number less than ``expected``.
     *
     * @param {number} actual - Test value.
     * @param {number} expected - Number that ``actual`` must be less than.
     * @param {string} [description] - Description of the condition being tested.
     */
    function assert_less_than(actual, expected, description)
    {
        /*
         * Test if a primitive number is less than another
         */
        assert(typeof actual === "number",
               "assert_less_than", description,
               "expected a number but got a ${type_actual}",
               {type_actual:typeof actual});

        assert(actual < expected,
               "assert_less_than", description,
               "expected a number less than ${expected} but got ${actual}",
               {expected:expected, actual:actual});
    }
    expose_assert(assert_less_than, "assert_less_than");

    /**
     * Assert that ``actual`` is a number greater than ``expected``.
     *
     * @param {number} actual - Test value.
     * @param {number} expected - Number that ``actual`` must be greater than.
     * @param {string} [description] - Description of the condition being tested.
     */
    function assert_greater_than(actual, expected, description)
    {
        /*
         * Test if a primitive number is greater than another
         */
        assert(typeof actual === "number",
               "assert_greater_than", description,
               "expected a number but got a ${type_actual}",
               {type_actual:typeof actual});

        assert(actual > expected,
               "assert_greater_than", description,
               "expected a number greater than ${expected} but got ${actual}",
               {expected:expected, actual:actual});
    }
    expose_assert(assert_greater_than, "assert_greater_than");

    /**
     * Assert that ``actual`` is a number greater than ``lower`` and less
     * than ``upper`` but not equal to either.
     *
     * @param {number} actual - Test value.
     * @param {number} lower - Number that ``actual`` must be greater than.
     * @param {number} upper - Number that ``actual`` must be less than.
     * @param {string} [description] - Description of the condition being tested.
     */
    function assert_between_exclusive(actual, lower, upper, description)
    {
        /*
         * Test if a primitive number is between two others
         */
        assert(typeof actual === "number",
               "assert_between_exclusive", description,
               "expected a number but got a ${type_actual}",
               {type_actual:typeof actual});

        assert(actual > lower && actual < upper,
               "assert_between_exclusive", description,
               "expected a number greater than ${lower} " +
               "and less than ${upper} but got ${actual}",
               {lower:lower, upper:upper, actual:actual});
    }
    expose_assert(assert_between_exclusive, "assert_between_exclusive");

    /**
     * Assert that ``actual`` is a number less than or equal to ``expected``.
     *
     * @param {number} actual - Test value.
     * @param {number} expected - Number that ``actual`` must be less
     * than or equal to.
     * @param {string} [description] - Description of the condition being tested.
     */
    function assert_less_than_equal(actual, expected, description)
    {
        /*
         * Test if a primitive number is less than or equal to another
         */
        assert(typeof actual === "number",
               "assert_less_than_equal", description,
               "expected a number but got a ${type_actual}",
               {type_actual:typeof actual});

        assert(actual <= expected,
               "assert_less_than_equal", description,
               "expected a number less than or equal to ${expected} but got ${actual}",
               {expected:expected, actual:actual});
    }
    expose_assert(assert_less_than_equal, "assert_less_than_equal");

    /**
     * Assert that ``actual`` is a number greater than or equal to ``expected``.
     *
     * @param {number} actual - Test value.
     * @param {number} expected - Number that ``actual`` must be greater
     * than or equal to.
     * @param {string} [description] - Description of the condition being tested.
     */
    function assert_greater_than_equal(actual, expected, description)
    {
        /*
         * Test if a primitive number is greater than or equal to another
         */
        assert(typeof actual === "number",
               "assert_greater_than_equal", description,
               "expected a number but got a ${type_actual}",
               {type_actual:typeof actual});

        assert(actual >= expected,
               "assert_greater_than_equal", description,
               "expected a number greater than or equal to ${expected} but got ${actual}",
               {expected:expected, actual:actual});
    }
    expose_assert(assert_greater_than_equal, "assert_greater_than_equal");

    /**
     * Assert that ``actual`` is a number greater than or equal to ``lower`` and less
     * than or equal to ``upper``.
     *
     * @param {number} actual - Test value.
     * @param {number} lower - Number that ``actual`` must be greater than or equal to.
     * @param {number} upper - Number that ``actual`` must be less than or equal to.
     * @param {string} [description] - Description of the condition being tested.
     */
    function assert_between_inclusive(actual, lower, upper, description)
    {
        /*
         * Test if a primitive number is between to two others or equal to either of them
         */
        assert(typeof actual === "number",
               "assert_between_inclusive", description,
               "expected a number but got a ${type_actual}",
               {type_actual:typeof actual});

        assert(actual >= lower && actual <= upper,
               "assert_between_inclusive", description,
               "expected a number greater than or equal to ${lower} " +
               "and less than or equal to ${upper} but got ${actual}",
               {lower:lower, upper:upper, actual:actual});
    }
    expose_assert(assert_between_inclusive, "assert_between_inclusive");

    /**
     * Assert that ``actual`` matches the RegExp ``expected``.
     *
     * @param {String} actual - Test string.
     * @param {RegExp} expected - RegExp ``actual`` must match.
     * @param {string} [description] - Description of the condition being tested.
     */
    function assert_regexp_match(actual, expected, description) {
        /*
         * Test if a string (actual) matches a regexp (expected)
         */
        assert(expected.test(actual),
               "assert_regexp_match", description,
               "expected ${expected} but got ${actual}",
               {expected:expected, actual:actual});
    }
    expose_assert(assert_regexp_match, "assert_regexp_match");

    /**
     * Assert that the class string of ``object`` as returned in
     * ``Object.prototype.toString`` is equal to ``class_name``.
     *
     * @param {Object} object - Object to stringify.
     * @param {string} class_string - Expected class string for ``object``.
     * @param {string} [description] - Description of the condition being tested.
     */
    function assert_class_string(object, class_string, description) {
        var actual = {}.toString.call(object);
        var expected = "[object " + class_string + "]";
        assert(same_value(actual, expected), "assert_class_string", description,
                                             "expected ${expected} but got ${actual}",
                                             {expected:expected, actual:actual});
    }
    expose_assert(assert_class_string, "assert_class_string");

    /**
     * Assert that ``object`` has an own property with name ``property_name``.
     *
     * @param {Object} object - Object that should have the given property.
     * @param {string} property_name - Expected property name.
     * @param {string} [description] - Description of the condition being tested.
     */
    function assert_own_property(object, property_name, description) {
        assert(object.hasOwnProperty(property_name),
               "assert_own_property", description,
               "expected property ${p} missing", {p:property_name});
    }
    expose_assert(assert_own_property, "assert_own_property");

    /**
     * Assert that ``object`` does not have an own property with name ``property_name``.
     *
     * @param {Object} object - Object that should not have the given property.
     * @param {string} property_name - Property name to test.
     * @param {string} [description] - Description of the condition being tested.
     */
    function assert_not_own_property(object, property_name, description) {
        assert(!object.hasOwnProperty(property_name),
               "assert_not_own_property", description,
               "unexpected property ${p} is found on object", {p:property_name});
    }
    expose_assert(assert_not_own_property, "assert_not_own_property");

    function _assert_inherits(name) {
        return function (object, property_name, description)
        {
            assert((typeof object === "object" && object !== null) ||
                   typeof object === "function" ||
                   // Or has [[IsHTMLDDA]] slot
                   String(object) === "[object HTMLAllCollection]",
                   name, description,
                   "provided value is not an object");

            assert("hasOwnProperty" in object,
                   name, description,
                   "provided value is an object but has no hasOwnProperty method");

            assert(!object.hasOwnProperty(property_name),
                   name, description,
                   "property ${p} found on object expected in prototype chain",
                   {p:property_name});

            assert(property_name in object,
                   name, description,
                   "property ${p} not found in prototype chain",
                   {p:property_name});
        };
    }

    /**
     * Assert that ``object`` does not have an own property with name
     * ``property_name``, but inherits one through the prototype chain.
     *
     * @param {Object} object - Object that should have the given property in its prototype chain.
     * @param {string} property_name - Expected property name.
     * @param {string} [description] - Description of the condition being tested.
     */
    function assert_inherits(object, property_name, description) {
        return _assert_inherits("assert_inherits")(object, property_name, description);
    }
    expose_assert(assert_inherits, "assert_inherits");

    /**
     * Alias for :js:func:`insert_inherits`.
     *
     * @param {Object} object - Object that should have the given property in its prototype chain.
     * @param {string} property_name - Expected property name.
     * @param {string} [description] - Description of the condition being tested.
     */
    function assert_idl_attribute(object, property_name, description) {
        return _assert_inherits("assert_idl_attribute")(object, property_name, description);
    }
    expose_assert(assert_idl_attribute, "assert_idl_attribute");


    /**
     * Assert that ``object`` has a property named ``property_name`` and that the property is readonly.
     *
     * Note: The implementation tries to update the named property, so
     * any side effects of updating will be triggered. Users are
     * encouraged to instead inspect the property descriptor of ``property_name`` on ``object``.
     *
     * @param {Object} object - Object that should have the given property in its prototype chain.
     * @param {string} property_name - Expected property name.
     * @param {string} [description] - Description of the condition being tested.
     */
    function assert_readonly(object, property_name, description)
    {
         var initial_value = object[property_name];
         try {
             //Note that this can have side effects in the case where
             //the property has PutForwards
             object[property_name] = initial_value + "a"; //XXX use some other value here?
             assert(same_value(object[property_name], initial_value),
                    "assert_readonly", description,
                    "changing property ${p} succeeded",
                    {p:property_name});
         } finally {
             object[property_name] = initial_value;
         }
    }
    expose_assert(assert_readonly, "assert_readonly");

    /**
     * Assert a JS Error with the expected constructor is thrown.
     *
     * @param {object} constructor The expected exception constructor.
     * @param {Function} func Function which should throw.
     * @param {string} [description] Error description for the case that the error is not thrown.
     */
    function assert_throws_js(constructor, func, description)
    {
        assert_throws_js_impl(constructor, func, description,
                              "assert_throws_js");
    }
    expose_assert(assert_throws_js, "assert_throws_js");

    /**
     * Like assert_throws_js but allows specifying the assertion type
     * (assert_throws_js or promise_rejects_js, in practice).
     */
    function assert_throws_js_impl(constructor, func, description,
                                   assertion_type)
    {
        try {
            func.call(this);
            assert(false, assertion_type, description,
                   "${func} did not throw", {func:func});
        } catch (e) {
            if (e instanceof AssertionError) {
                throw e;
            }

            // Basic sanity-checks on the thrown exception.
            assert(typeof e === "object",
                   assertion_type, description,
                   "${func} threw ${e} with type ${type}, not an object",
                   {func:func, e:e, type:typeof e});

            assert(e !== null,
                   assertion_type, description,
                   "${func} threw null, not an object",
                   {func:func});

            // Basic sanity-check on the passed-in constructor
            assert(typeof constructor == "function",
                   assertion_type, description,
                   "${constructor} is not a constructor",
                   {constructor:constructor});
            var obj = constructor;
            while (obj) {
                if (typeof obj === "function" &&
                    obj.name === "Error") {
                    break;
                }
                obj = Object.getPrototypeOf(obj);
            }
            assert(obj != null,
                   assertion_type, description,
                   "${constructor} is not an Error subtype",
                   {constructor:constructor});

            // And checking that our exception is reasonable
            assert(e.constructor === constructor &&
                   e.name === constructor.name,
                   assertion_type, description,
                   "${func} threw ${actual} (${actual_name}) expected instance of ${expected} (${expected_name})",
                   {func:func, actual:e, actual_name:e.name,
                    expected:constructor,
                    expected_name:constructor.name});
        }
    }

    // TODO: Figure out how to document the overloads better.
    // sphinx-js doesn't seem to handle @variation correctly,
    // and only expects a single JSDoc entry per function.
    /**
     * Assert a DOMException with the expected type is thrown.
     *
     * There are two ways of calling assert_throws_dom:
     *
     * 1) If the DOMException is expected to come from the current global, the
     * second argument should be the function expected to throw and a third,
     * optional, argument is the assertion description.
     *
     * 2) If the DOMException is expected to come from some other global, the
     * second argument should be the DOMException constructor from that global,
     * the third argument the function expected to throw, and the fourth, optional,
     * argument the assertion description.
     *
     * @param {number|string} type - The expected exception name or
     * code.  See the `table of names and codes
     * <https://webidl.spec.whatwg.org/#dfn-error-names-table>`_. If a
     * number is passed it should be one of the numeric code values in
     * that table (e.g. 3, 4, etc).  If a string is passed it can
     * either be an exception name (e.g. "HierarchyRequestError",
     * "WrongDocumentError") or the name of the corresponding error
     * code (e.g. "``HIERARCHY_REQUEST_ERR``", "``WRONG_DOCUMENT_ERR``").
     * @param {Function} descriptionOrFunc - The function expected to
     * throw (if the exception comes from another global), or the
     * optional description of the condition being tested (if the
     * exception comes from the current global).
     * @param {string} [description] - Description of the condition
     * being tested (if the exception comes from another global).
     *
     */
    function assert_throws_dom(type, funcOrConstructor, descriptionOrFunc, maybeDescription)
    {
        let constructor, func, description;
        if (funcOrConstructor.name === "DOMException") {
            constructor = funcOrConstructor;
            func = descriptionOrFunc;
            description = maybeDescription;
        } else {
            constructor = self.DOMException;
            func = funcOrConstructor;
            description = descriptionOrFunc;
            assert(maybeDescription === undefined,
                   "Too many args pased to no-constructor version of assert_throws_dom");
        }
        assert_throws_dom_impl(type, func, description, "assert_throws_dom", constructor)
    }
    expose_assert(assert_throws_dom, "assert_throws_dom");

    /**
     * Similar to assert_throws_dom but allows specifying the assertion type
     * (assert_throws_dom or promise_rejects_dom, in practice).  The
     * "constructor" argument must be the DOMException constructor from the
     * global we expect the exception to come from.
     */
    function assert_throws_dom_impl(type, func, description, assertion_type, constructor)
    {
        try {
            func.call(this);
            assert(false, assertion_type, description,
                   "${func} did not throw", {func:func});
        } catch (e) {
            if (e instanceof AssertionError) {
                throw e;
            }

            // Basic sanity-checks on the thrown exception.
            assert(typeof e === "object",
                   assertion_type, description,
                   "${func} threw ${e} with type ${type}, not an object",
                   {func:func, e:e, type:typeof e});

            assert(e !== null,
                   assertion_type, description,
                   "${func} threw null, not an object",
                   {func:func});

            // Sanity-check our type
            assert(typeof type == "number" ||
                   typeof type == "string",
                   assertion_type, description,
                   "${type} is not a number or string",
                   {type:type});

            var codename_name_map = {
                INDEX_SIZE_ERR: 'IndexSizeError',
                HIERARCHY_REQUEST_ERR: 'HierarchyRequestError',
                WRONG_DOCUMENT_ERR: 'WrongDocumentError',
                INVALID_CHARACTER_ERR: 'InvalidCharacterError',
                NO_MODIFICATION_ALLOWED_ERR: 'NoModificationAllowedError',
                NOT_FOUND_ERR: 'NotFoundError',
                NOT_SUPPORTED_ERR: 'NotSupportedError',
                INUSE_ATTRIBUTE_ERR: 'InUseAttributeError',
                INVALID_STATE_ERR: 'InvalidStateError',
                SYNTAX_ERR: 'SyntaxError',
                INVALID_MODIFICATION_ERR: 'InvalidModificationError',
                NAMESPACE_ERR: 'NamespaceError',
                INVALID_ACCESS_ERR: 'InvalidAccessError',
                TYPE_MISMATCH_ERR: 'TypeMismatchError',
                SECURITY_ERR: 'SecurityError',
                NETWORK_ERR: 'NetworkError',
                ABORT_ERR: 'AbortError',
                URL_MISMATCH_ERR: 'URLMismatchError',
                QUOTA_EXCEEDED_ERR: 'QuotaExceededError',
                TIMEOUT_ERR: 'TimeoutError',
                INVALID_NODE_TYPE_ERR: 'InvalidNodeTypeError',
                DATA_CLONE_ERR: 'DataCloneError'
            };

            var name_code_map = {
                IndexSizeError: 1,
                HierarchyRequestError: 3,
                WrongDocumentError: 4,
                InvalidCharacterError: 5,
                NoModificationAllowedError: 7,
                NotFoundError: 8,
                NotSupportedError: 9,
                InUseAttributeError: 10,
                InvalidStateError: 11,
                SyntaxError: 12,
                InvalidModificationError: 13,
                NamespaceError: 14,
                InvalidAccessError: 15,
                TypeMismatchError: 17,
                SecurityError: 18,
                NetworkError: 19,
                AbortError: 20,
                URLMismatchError: 21,
                QuotaExceededError: 22,
                TimeoutError: 23,
                InvalidNodeTypeError: 24,
                DataCloneError: 25,

                EncodingError: 0,
                NotReadableError: 0,
                UnknownError: 0,
                ConstraintError: 0,
                DataError: 0,
                TransactionInactiveError: 0,
                ReadOnlyError: 0,
                VersionError: 0,
                OperationError: 0,
                NotAllowedError: 0,
                OptOutError: 0
            };

            var code_name_map = {};
            for (var key in name_code_map) {
                if (name_code_map[key] > 0) {
                    code_name_map[name_code_map[key]] = key;
                }
            }

            var required_props = {};
            var name;

            if (typeof type === "number") {
                if (type === 0) {
                    throw new AssertionError('Test bug: ambiguous DOMException code 0 passed to assert_throws_dom()');
                } else if (!(type in code_name_map)) {
                    throw new AssertionError('Test bug: unrecognized DOMException code "' + type + '" passed to assert_throws_dom()');
                }
                name = code_name_map[type];
                required_props.code = type;
            } else if (typeof type === "string") {
                name = type in codename_name_map ? codename_name_map[type] : type;
                if (!(name in name_code_map)) {
                    throw new AssertionError('Test bug: unrecognized DOMException code name or name "' + type + '" passed to assert_throws_dom()');
                }

                required_props.code = name_code_map[name];
            }

            if (required_props.code === 0 ||
               ("name" in e &&
                e.name !== e.name.toUpperCase() &&
                e.name !== "DOMException")) {
                // New style exception: also test the name property.
                required_props.name = name;
            }

            for (var prop in required_props) {
                assert(prop in e && e[prop] == required_props[prop],
                       assertion_type, description,
                       "${func} threw ${e} that is not a DOMException " + type + ": property ${prop} is equal to ${actual}, expected ${expected}",
                       {func:func, e:e, prop:prop, actual:e[prop], expected:required_props[prop]});
            }

            // Check that the exception is from the right global.  This check is last
            // so more specific, and more informative, checks on the properties can
            // happen in case a totally incorrect exception is thrown.
            assert(e.constructor === constructor,
                   assertion_type, description,
                   "${func} threw an exception from the wrong global",
                   {func});

        }
    }

    /**
     * Assert the provided value is thrown.
     *
     * @param {value} exception The expected exception.
     * @param {Function} func Function which should throw.
     * @param {string} [description] Error description for the case that the error is not thrown.
     */
    function assert_throws_exactly(exception, func, description)
    {
        assert_throws_exactly_impl(exception, func, description,
                                   "assert_throws_exactly");
    }
    expose_assert(assert_throws_exactly, "assert_throws_exactly");

    /**
     * Like assert_throws_exactly but allows specifying the assertion type
     * (assert_throws_exactly or promise_rejects_exactly, in practice).
     */
    function assert_throws_exactly_impl(exception, func, description,
                                        assertion_type)
    {
        try {
            func.call(this);
            assert(false, assertion_type, description,
                   "${func} did not throw", {func:func});
        } catch (e) {
            if (e instanceof AssertionError) {
                throw e;
            }

            assert(same_value(e, exception), assertion_type, description,
                   "${func} threw ${e} but we expected it to throw ${exception}",
                   {func:func, e:e, exception:exception});
        }
    }

    /**
     * Asserts if called. Used to ensure that a specific codepath is
     * not taken e.g. that an error event isn't fired.
     *
     * @param {string} [description] - Description of the condition being tested.
     */
    function assert_unreached(description) {
         assert(false, "assert_unreached", description,
                "Reached unreachable code");
    }
    expose_assert(assert_unreached, "assert_unreached");

    /**
     * @callback AssertFunc
     * @param {Any} actual
     * @param {Any} expected
     * @param {Any[]} args
     */

    /**
     * Asserts that ``actual`` matches at least one value of ``expected``
     * according to a comparison defined by ``assert_func``.
     *
     * Note that tests with multiple allowed pass conditions are bad
     * practice unless the spec specifically allows multiple
     * behaviours. Test authors should not use this method simply to
     * hide UA bugs.
     *
     * @param {AssertFunc} assert_func - Function to compare actual
     * and expected. It must throw when the comparison fails and
     * return when the comparison passes.
     * @param {Any} actual - Test value.
     * @param {Array} expected_array - Array of possible expected values.
     * @param {Any[]} args - Additional arguments to pass to ``assert_func``.
     */
    function assert_any(assert_func, actual, expected_array, ...args)
    {
        var errors = [];
        var passed = false;
        forEach(expected_array,
                function(expected)
                {
                    try {
                        assert_func.apply(this, [actual, expected].concat(args));
                        passed = true;
                    } catch (e) {
                        errors.push(e.message);
                    }
                });
        if (!passed) {
            throw new AssertionError(errors.join("\n\n"));
        }
    }
    // FIXME: assert_any cannot use expose_assert, because assert_wrapper does
    // not support nested assert calls (e.g. to assert_func). We need to
    // support bypassing assert_wrapper for the inner asserts here.
    expose(assert_any, "assert_any");

    /**
     * Assert that a feature is implemented, based on a 'truthy' condition.
     *
     * This function should be used to early-exit from tests in which there is
     * no point continuing without support for a non-optional spec or spec
     * feature. For example:
     *
     *     assert_implements(window.Foo, 'Foo is not supported');
     *
     * @param {object} condition The truthy value to test
     * @param {string} [description] Error description for the case that the condition is not truthy.
     */
    function assert_implements(condition, description) {
        assert(!!condition, "assert_implements", description);
    }
    expose_assert(assert_implements, "assert_implements")

    /**
     * Assert that an optional feature is implemented, based on a 'truthy' condition.
     *
     * This function should be used to early-exit from tests in which there is
     * no point continuing without support for an explicitly optional spec or
     * spec feature. For example:
     *
     *     assert_implements_optional(video.canPlayType("video/webm"),
     *                                "webm video playback not supported");
     *
     * @param {object} condition The truthy value to test
     * @param {string} [description] Error description for the case that the condition is not truthy.
     */
    function assert_implements_optional(condition, description) {
        if (!condition) {
            throw new OptionalFeatureUnsupportedError(description);
        }
    }
    expose_assert(assert_implements_optional, "assert_implements_optional");

    /**
     * @class
     *
     * A single subtest. A Test is not constructed directly but via the
     * :js:func:`test`, :js:func:`async_test` or :js:func:`promise_test` functions.
     *
     * @param {string} name - This must be unique in a given file and must be
     * invariant between runs.
     *
     */
    function Test(name, properties)
    {
        if (tests.file_is_test && tests.tests.length) {
            throw new Error("Tried to create a test with file_is_test");
        }
        /** The test name. */
        this.name = name;

        this.phase = (tests.is_aborted || tests.phase === tests.phases.COMPLETE) ?
            this.phases.COMPLETE : this.phases.INITIAL;

        /** The test status code.*/
        this.status = this.NOTRUN;
        this.timeout_id = null;
        this.index = null;

        this.properties = properties || {};
        this.timeout_length = settings.test_timeout;
        if (this.timeout_length !== null) {
            this.timeout_length *= tests.timeout_multiplier;
        }

        /** A message indicating the reason for test failure. */
        this.message = null;
        /** Stack trace in case of failure. */
        this.stack = null;

        this.steps = [];
        this._is_promise_test = false;

        this.cleanup_callbacks = [];
        this._user_defined_cleanup_count = 0;
        this._done_callbacks = [];

        if (typeof AbortController === "function") {
            this._abortController = new AbortController();
        }

        // Tests declared following harness completion are likely an indication
        // of a programming error, but they cannot be reported
        // deterministically.
        if (tests.phase === tests.phases.COMPLETE) {
            return;
        }

        tests.push(this);
    }

    /**
     * Enum of possible test statuses.
     *
     * :values:
     *   - ``PASS``
     *   - ``FAIL``
     *   - ``TIMEOUT``
     *   - ``NOTRUN``
     *   - ``PRECONDITION_FAILED``
     */
    Test.statuses = {
        PASS:0,
        FAIL:1,
        TIMEOUT:2,
        NOTRUN:3,
        PRECONDITION_FAILED:4
    };

    Test.prototype = merge({}, Test.statuses);

    Test.prototype.phases = {
        INITIAL:0,
        STARTED:1,
        HAS_RESULT:2,
        CLEANING:3,
        COMPLETE:4
    };

    Test.prototype.status_formats = {
        0: "Pass",
        1: "Fail",
        2: "Timeout",
        3: "Not Run",
        4: "Optional Feature Unsupported",
    }

    Test.prototype.format_status = function() {
        return this.status_formats[this.status];
    }

    Test.prototype.structured_clone = function()
    {
        if (!this._structured_clone) {
            var msg = this.message;
            msg = msg ? String(msg) : msg;
            this._structured_clone = merge({
                name:String(this.name),
                properties:merge({}, this.properties),
                phases:merge({}, this.phases)
            }, Test.statuses);
        }
        this._structured_clone.status = this.status;
        this._structured_clone.message = this.message;
        this._structured_clone.stack = this.stack;
        this._structured_clone.index = this.index;
        this._structured_clone.phase = this.phase;
        return this._structured_clone;
    };

    /**
     * Run a single step of an ongoing test.
     *
     * @param {string} func - Callback function to run as a step. If
     * this throws an :js:func:`AssertionError`, or any other
     * exception, the :js:class:`Test` status is set to ``FAIL``.
     * @param {Object} [this_obj] - The object to use as the this
     * value when calling ``func``. Defaults to the  :js:class:`Test` object.
     */
    Test.prototype.step = function(func, this_obj)
    {
        if (this.phase > this.phases.STARTED) {
            return;
        }

        if (settings.debug && this.phase !== this.phases.STARTED) {
            console.log("TEST START", this.name);
        }
        this.phase = this.phases.STARTED;
        //If we don't get a result before the harness times out that will be a test timeout
        this.set_status(this.TIMEOUT, "Test timed out");

        tests.started = true;
        tests.current_test = this;
        tests.notify_test_state(this);

        if (this.timeout_id === null) {
            this.set_timeout();
        }

        this.steps.push(func);

        if (arguments.length === 1) {
            this_obj = this;
        }

        if (settings.debug) {
            console.debug("TEST STEP", this.name);
        }

        try {
            return func.apply(this_obj, Array.prototype.slice.call(arguments, 2));
        } catch (e) {
            if (this.phase >= this.phases.HAS_RESULT) {
                return;
            }
            var status = e instanceof OptionalFeatureUnsupportedError ? this.PRECONDITION_FAILED : this.FAIL;
            var message = String((typeof e === "object" && e !== null) ? e.message : e);
            var stack = e.stack ? e.stack : null;

            this.set_status(status, message, stack);
            this.phase = this.phases.HAS_RESULT;
            this.done();
        } finally {
            this.current_test = null;
        }
    };

    /**
     * Wrap a function so that it runs as a step of the current test.
     *
     * This allows creating a callback function that will run as a
     * test step.
     *
     * @example
     * let t = async_test("Example");
     * onload = t.step_func(e => {
     *   assert_equals(e.name, "load");
     *   // Mark the test as complete.
     *   t.done();
     * })
     *
     * @param {string} func - Function to run as a step. If this
     * throws an :js:func:`AssertionError`, or any other exception,
     * the :js:class:`Test` status is set to ``FAIL``.
     * @param {Object} [this_obj] - The object to use as the this
     * value when calling ``func``. Defaults to the :js:class:`Test` object.
     */
    Test.prototype.step_func = function(func, this_obj)
    {
        var test_this = this;

        if (arguments.length === 1) {
            this_obj = test_this;
        }

        return function()
        {
            return test_this.step.apply(test_this, [func, this_obj].concat(
                Array.prototype.slice.call(arguments)));
        };
    };

    /**
     * Wrap a function so that it runs as a step of the current test,
     * and automatically marks the test as complete if the function
     * returns without error.
     *
     * @param {string} func - Function to run as a step. If this
     * throws an :js:func:`AssertionError`, or any other exception,
     * the :js:class:`Test` status is set to ``FAIL``. If it returns
     * without error the status is set to ``PASS``.
     * @param {Object} [this_obj] - The object to use as the this
     * value when calling `func`. Defaults to the :js:class:`Test` object.
     */
    Test.prototype.step_func_done = function(func, this_obj)
    {
        var test_this = this;

        if (arguments.length === 1) {
            this_obj = test_this;
        }

        return function()
        {
            if (func) {
                test_this.step.apply(test_this, [func, this_obj].concat(
                    Array.prototype.slice.call(arguments)));
            }
            test_this.done();
        };
    };

    /**
     * Return a function that automatically sets the current test to
     * ``FAIL`` if it's called.
     *
     * @param {string} [description] - Error message to add to assert
     * in case of failure.
     *
     */
    Test.prototype.unreached_func = function(description)
    {
        return this.step_func(function() {
            assert_unreached(description);
        });
    };

    /**
     * Run a function as a step of the test after a given timeout.
     *
     * This multiplies the timeout by the global timeout multiplier to
     * account for the expected execution speed of the current test
     * environment. For example ``test.step_timeout(f, 2000)`` with a
     * timeout multiplier of 2 will wait for 4000ms before calling ``f``.
     *
     * In general it's encouraged to use :js:func:`Test.step_wait` or
     * :js:func:`step_wait_func` in preference to this function where possible,
     * as they provide better test performance.
     *
     * @param {Function} func - Function to run as a test
     * step.
     * @param {number} timeout - Time in ms to wait before running the
     * test step. The actual wait time is ``timeout`` x
     * ``timeout_multiplier``.
     *
     */
    Test.prototype.step_timeout = function(func, timeout) {
        var test_this = this;
        var args = Array.prototype.slice.call(arguments, 2);
        return setTimeout(this.step_func(function() {
            return func.apply(test_this, args);
        }), timeout * tests.timeout_multiplier);
    };

    /**
     * Poll for a function to return true, and call a callback
     * function once it does, or assert if a timeout is
     * reached. This is preferred over a simple step_timeout
     * whenever possible since it allows the timeout to be longer
     * to reduce intermittents without compromising test execution
     * speed when the condition is quickly met.
     *
     * @param {Function} cond A function taking no arguments and
     *                        returning a boolean or a Promise. The callback is
     *                        called when this function returns true, or the
     *                        returned Promise is resolved with true.
     * @param {Function} func A function taking no arguments to call once
     *                        the condition is met.
     * @param {string} [description] Error message to add to assert in case of
     *                               failure.
     * @param {number} timeout Timeout in ms. This is multiplied by the global
     *                         timeout_multiplier
     * @param {number} interval Polling interval in ms
     *
     */
    Test.prototype.step_wait_func = function(cond, func, description,
                                             timeout=3000, interval=100) {
        var timeout_full = timeout * tests.timeout_multiplier;
        var remaining = Math.ceil(timeout_full / interval);
        var test_this = this;

        const step = test_this.step_func((result) => {
            if (result) {
                func();
            } else {
                if (remaining === 0) {
                    assert(false, "step_wait_func", description,
                           "Timed out waiting on condition");
                }
                remaining--;
                setTimeout(wait_for_inner, interval);
            }
        });

        var wait_for_inner = test_this.step_func(() => {
            Promise.resolve(cond()).then(
                step,
                test_this.unreached_func("step_wait_func"));
        });

        wait_for_inner();
    };

    /**
     * Poll for a function to return true, and invoke a callback
     * followed by this.done() once it does, or assert if a timeout
     * is reached. This is preferred over a simple step_timeout
     * whenever possible since it allows the timeout to be longer
     * to reduce intermittents without compromising test execution speed
     * when the condition is quickly met.
     *
     * @example
     * async_test(t => {
     *  const popup = window.open("resources/coop-coep.py?coop=same-origin&coep=&navigate=about:blank");
     *  t.add_cleanup(() => popup.close());
     *  assert_equals(window, popup.opener);
     *
     *  popup.onload = t.step_func(() => {
     *    assert_true(popup.location.href.endsWith("&navigate=about:blank"));
     *    // Use step_wait_func_done as about:blank cannot message back.
     *    t.step_wait_func_done(() => popup.location.href === "about:blank");
     *  });
     * }, "Navigating a popup to about:blank");
     *
     * @param {Function} cond A function taking no arguments and
     *                        returning a boolean or a Promise. The callback is
     *                        called when this function returns true, or the
     *                        returned Promise is resolved with true.
     * @param {Function} func A function taking no arguments to call once
     *                        the condition is met.
     * @param {string} [description] Error message to add to assert in case of
     *                               failure.
     * @param {number} timeout Timeout in ms. This is multiplied by the global
     *                         timeout_multiplier
     * @param {number} interval Polling interval in ms
     *
     */
    Test.prototype.step_wait_func_done = function(cond, func, description,
                                                  timeout=3000, interval=100) {
         this.step_wait_func(cond, () => {
            if (func) {
                func();
            }
            this.done();
         }, description, timeout, interval);
    };

    /**
     * Poll for a function to return true, and resolve a promise
     * once it does, or assert if a timeout is reached. This is
     * preferred over a simple step_timeout whenever possible
     * since it allows the timeout to be longer to reduce
     * intermittents without compromising test execution speed
     * when the condition is quickly met.
     *
     * @example
     * promise_test(async t => {
     *  // …
     * await t.step_wait(() => frame.contentDocument === null, "Frame navigated to a cross-origin document");
     * // …
     * }, "");
     *
     * @param {Function} cond A function taking no arguments and
     *                        returning a boolean or a Promise.
     * @param {string} [description] Error message to add to assert in case of
     *                              failure.
     * @param {number} timeout Timeout in ms. This is multiplied by the global
     *                         timeout_multiplier
     * @param {number} interval Polling interval in ms
     * @returns {Promise} Promise resolved once cond is met.
     *
     */
    Test.prototype.step_wait = function(cond, description, timeout=3000, interval=100) {
        return new Promise(resolve => {
            this.step_wait_func(cond, resolve, description, timeout, interval);
        });
    }

    /*
     * Private method for registering cleanup functions. `testharness.js`
     * internals should use this method instead of the public `add_cleanup`
     * method in order to hide implementation details from the harness status
     * message in the case errors.
     */
    Test.prototype._add_cleanup = function(callback) {
        this.cleanup_callbacks.push(callback);
    };

    /**
     * Schedule a function to be run after the test result is known, regardless
     * of passing or failing state.
     *
     * The behavior of this function will not
     * influence the result of the test, but if an exception is thrown, the
     * test harness will report an error.
     *
     * @param {Function} callback - The cleanup function to run. This
     * is called with no arguments.
     */
    Test.prototype.add_cleanup = function(callback) {
        this._user_defined_cleanup_count += 1;
        this._add_cleanup(callback);
    };

    Test.prototype.set_timeout = function()
    {
        if (this.timeout_length !== null) {
            var this_obj = this;
            this.timeout_id = setTimeout(function()
                                         {
                                             this_obj.timeout();
                                         }, this.timeout_length);
        }
    };

    Test.prototype.set_status = function(status, message, stack)
    {
        this.status = status;
        this.message = message;
        this.stack = stack ? stack : null;
    };

    /**
     * Manually set the test status to ``TIMEOUT``.
     */
    Test.prototype.timeout = function()
    {
        this.timeout_id = null;
        this.set_status(this.TIMEOUT, "Test timed out");
        this.phase = this.phases.HAS_RESULT;
        this.done();
    };

    /**
     * Manually set the test status to ``TIMEOUT``.
     *
     * Alias for `Test.timeout <#Test.timeout>`_.
     */
    Test.prototype.force_timeout = function() {
        return this.timeout();
    };

    /**
     * Mark the test as complete.
     *
     * This sets the test status to ``PASS`` if no other status was
     * already recorded. Any subsequent attempts to run additional
     * test steps will be ignored.
     *
     * After setting the test status any test cleanup functions will
     * be run.
     */
    Test.prototype.done = function()
    {
        if (this.phase >= this.phases.CLEANING) {
            return;
        }

        if (this.phase <= this.phases.STARTED) {
            this.set_status(this.PASS, null);
        }

        if (global_scope.clearTimeout) {
            clearTimeout(this.timeout_id);
        }

        if (settings.debug) {
            console.log("TEST DONE",
                        this.status,
                        this.name);
        }

        this.cleanup();
    };

    function add_test_done_callback(test, callback)
    {
        if (test.phase === test.phases.COMPLETE) {
            callback();
            return;
        }

        test._done_callbacks.push(callback);
    }

    /*
     * Invoke all specified cleanup functions. If one or more produce an error,
     * the context is in an unpredictable state, so all further testing should
     * be cancelled.
     */
    Test.prototype.cleanup = function() {
        var errors = [];
        var bad_value_count = 0;
        function on_error(e) {
            errors.push(e);
            // Abort tests immediately so that tests declared within subsequent
            // cleanup functions are not run.
            tests.abort();
        }
        var this_obj = this;
        var results = [];

        this.phase = this.phases.CLEANING;

        if (this._abortController) {
            this._abortController.abort("Test cleanup");
        }

        forEach(this.cleanup_callbacks,
                function(cleanup_callback) {
                    var result;

                    try {
                        result = cleanup_callback();
                    } catch (e) {
                        on_error(e);
                        return;
                    }

                    if (!is_valid_cleanup_result(this_obj, result)) {
                        bad_value_count += 1;
                        // Abort tests immediately so that tests declared
                        // within subsequent cleanup functions are not run.
                        tests.abort();
                    }

                    results.push(result);
                });

        if (!this._is_promise_test) {
            cleanup_done(this_obj, errors, bad_value_count);
        } else {
            all_async(results,
                      function(result, done) {
                          if (result && typeof result.then === "function") {
                              result
                                  .then(null, on_error)
                                  .then(done);
                          } else {
                              done();
                          }
                      },
                      function() {
                          cleanup_done(this_obj, errors, bad_value_count);
                      });
        }
    };

    /*
     * Determine if the return value of a cleanup function is valid for a given
     * test. Any test may return the value `undefined`. Tests created with
     * `promise_test` may alternatively return "thenable" object values.
     */
    function is_valid_cleanup_result(test, result) {
        if (result === undefined) {
            return true;
        }

        if (test._is_promise_test) {
            return result && typeof result.then === "function";
        }

        return false;
    }

    function cleanup_done(test, errors, bad_value_count) {
        if (errors.length || bad_value_count) {
            var total = test._user_defined_cleanup_count;

            tests.status.status = tests.status.ERROR;
            tests.status.stack = null;
            tests.status.message = "Test named '" + test.name +
                "' specified " + total +
                " 'cleanup' function" + (total > 1 ? "s" : "");

            if (errors.length) {
                tests.status.message += ", and " + errors.length + " failed";
                tests.status.stack = ((typeof errors[0] === "object" &&
                                       errors[0].hasOwnProperty("stack")) ?
                                      errors[0].stack : null);
            }

            if (bad_value_count) {
                var type = test._is_promise_test ?
                   "non-thenable" : "non-undefined";
                tests.status.message += ", and " + bad_value_count +
                    " returned a " + type + " value";
            }

            tests.status.message += ".";
        }

        test.phase = test.phases.COMPLETE;
        tests.result(test);
        forEach(test._done_callbacks,
                function(callback) {
                    callback();
                });
        test._done_callbacks.length = 0;
    }

    /**
     * Gives an AbortSignal that will be aborted when the test finishes.
     */
    Test.prototype.get_signal = function() {
        if (!this._abortController) {
            throw new Error("AbortController is not supported in this browser");
        }
        return this._abortController.signal;
    }

    /**
     * A RemoteTest object mirrors a Test object on a remote worker. The
     * associated RemoteWorker updates the RemoteTest object in response to
     * received events. In turn, the RemoteTest object replicates these events
     * on the local document. This allows listeners (test result reporting
     * etc..) to transparently handle local and remote events.
     */
    function RemoteTest(clone) {
        var this_obj = this;
        Object.keys(clone).forEach(
                function(key) {
                    this_obj[key] = clone[key];
                });
        this.index = null;
        this.phase = this.phases.INITIAL;
        this.update_state_from(clone);
        this._done_callbacks = [];
        tests.push(this);
    }

    RemoteTest.prototype.structured_clone = function() {
        var clone = {};
        Object.keys(this).forEach(
                (function(key) {
                    var value = this[key];
                    // `RemoteTest` instances are responsible for managing
                    // their own "done" callback functions, so those functions
                    // are not relevant in other execution contexts. Because of
                    // this (and because Function values cannot be serialized
                    // for cross-realm transmittance), the property should not
                    // be considered when cloning instances.
                    if (key === '_done_callbacks' ) {
                        return;
                    }

                    if (typeof value === "object" && value !== null) {
                        clone[key] = merge({}, value);
                    } else {
                        clone[key] = value;
                    }
                }).bind(this));
        clone.phases = merge({}, this.phases);
        return clone;
    };

    /**
     * `RemoteTest` instances are objects which represent tests running in
     * another realm. They do not define "cleanup" functions (if necessary,
     * such functions are defined on the associated `Test` instance within the
     * external realm). However, `RemoteTests` may have "done" callbacks (e.g.
     * as attached by the `Tests` instance responsible for tracking the overall
     * test status in the parent realm). The `cleanup` method delegates to
     * `done` in order to ensure that such callbacks are invoked following the
     * completion of the `RemoteTest`.
     */
    RemoteTest.prototype.cleanup = function() {
        this.done();
    };
    RemoteTest.prototype.phases = Test.prototype.phases;
    RemoteTest.prototype.update_state_from = function(clone) {
        this.status = clone.status;
        this.message = clone.message;
        this.stack = clone.stack;
        if (this.phase === this.phases.INITIAL) {
            this.phase = this.phases.STARTED;
        }
    };
    RemoteTest.prototype.done = function() {
        this.phase = this.phases.COMPLETE;

        forEach(this._done_callbacks,
                function(callback) {
                    callback();
                });
    }

    RemoteTest.prototype.format_status = function() {
        return Test.prototype.status_formats[this.status];
    }

    /*
     * A RemoteContext listens for test events from a remote test context, such
     * as another window or a worker. These events are then used to construct
     * and maintain RemoteTest objects that mirror the tests running in the
     * remote context.
     *
     * An optional third parameter can be used as a predicate to filter incoming
     * MessageEvents.
     */
    function RemoteContext(remote, message_target, message_filter) {
        this.running = true;
        this.started = false;
        this.tests = new Array();
        this.early_exception = null;

        var this_obj = this;
        // If remote context is cross origin assigning to onerror is not
        // possible, so silently catch those errors.
        try {
          remote.onerror = function(error) { this_obj.remote_error(error); };
        } catch (e) {
          // Ignore.
        }

        // Keeping a reference to the remote object and the message handler until
        // remote_done() is seen prevents the remote object and its message channel
        // from going away before all the messages are dispatched.
        this.remote = remote;
        this.message_target = message_target;
        this.message_handler = function(message) {
            var passesFilter = !message_filter || message_filter(message);
            // The reference to the `running` property in the following
            // condition is unnecessary because that value is only set to
            // `false` after the `message_handler` function has been
            // unsubscribed.
            // TODO: Simplify the condition by removing the reference.
            if (this_obj.running && message.data && passesFilter &&
                (message.data.type in this_obj.message_handlers)) {
                this_obj.message_handlers[message.data.type].call(this_obj, message.data);
            }
        };

        if (self.Promise) {
            this.done = new Promise(function(resolve) {
                this_obj.doneResolve = resolve;
            });
        }

        this.message_target.addEventListener("message", this.message_handler);
    }

    RemoteContext.prototype.remote_error = function(error) {
        if (error.preventDefault) {
            error.preventDefault();
        }

        // Defer interpretation of errors until the testing protocol has
        // started and the remote test's `allow_uncaught_exception` property
        // is available.
        if (!this.started) {
            this.early_exception = error;
        } else if (!this.allow_uncaught_exception) {
            this.report_uncaught(error);
        }
    };

    RemoteContext.prototype.report_uncaught = function(error) {
        var message = error.message || String(error);
        var filename = (error.filename ? " " + error.filename: "");
        // FIXME: Display remote error states separately from main document
        // error state.
        tests.set_status(tests.status.ERROR,
                         "Error in remote" + filename + ": " + message,
                         error.stack);
    };

    RemoteContext.prototype.start = function(data) {
        this.started = true;
        this.allow_uncaught_exception = data.properties.allow_uncaught_exception;

        if (this.early_exception && !this.allow_uncaught_exception) {
            this.report_uncaught(this.early_exception);
        }
    };

    RemoteContext.prototype.test_state = function(data) {
        var remote_test = this.tests[data.test.index];
        if (!remote_test) {
            remote_test = new RemoteTest(data.test);
            this.tests[data.test.index] = remote_test;
        }
        remote_test.update_state_from(data.test);
        tests.notify_test_state(remote_test);
    };

    RemoteContext.prototype.test_done = function(data) {
        var remote_test = this.tests[data.test.index];
        remote_test.update_state_from(data.test);
        remote_test.done();
        tests.result(remote_test);
    };

    RemoteContext.prototype.remote_done = function(data) {
        if (tests.status.status === null &&
            data.status.status !== data.status.OK) {
            tests.set_status(data.status.status, data.status.message, data.status.stack);
        }

        for (let assert of data.asserts) {
            var record = new AssertRecord();
            record.assert_name = assert.assert_name;
            record.args = assert.args;
            record.test = assert.test != null ? this.tests[assert.test.index] : null;
            record.status = assert.status;
            record.stack = assert.stack;
            tests.asserts_run.push(record);
        }

        this.message_target.removeEventListener("message", this.message_handler);
        this.running = false;

        // If remote context is cross origin assigning to onerror is not
        // possible, so silently catch those errors.
        try {
          this.remote.onerror = null;
        } catch (e) {
          // Ignore.
        }

        this.remote = null;
        this.message_target = null;
        if (this.doneResolve) {
            this.doneResolve();
        }

        if (tests.all_done()) {
            tests.complete();
        }
    };

    RemoteContext.prototype.message_handlers = {
        start: RemoteContext.prototype.start,
        test_state: RemoteContext.prototype.test_state,
        result: RemoteContext.prototype.test_done,
        complete: RemoteContext.prototype.remote_done
    };

    /**
     * @class
     * Status of the overall harness
     */
    function TestsStatus()
    {
        /** The status code */
        this.status = null;
        /** Message in case of failure */
        this.message = null;
        /** Stack trace in case of an exception. */
        this.stack = null;
    }

    /**
     * Enum of possible harness statuses.
     *
     * :values:
     *   - ``OK``
     *   - ``ERROR``
     *   - ``TIMEOUT``
     *   - ``PRECONDITION_FAILED``
     */
    TestsStatus.statuses = {
        OK:0,
        ERROR:1,
        TIMEOUT:2,
        PRECONDITION_FAILED:3
    };

    TestsStatus.prototype = merge({}, TestsStatus.statuses);

    TestsStatus.prototype.formats = {
        0: "OK",
        1: "Error",
        2: "Timeout",
        3: "Optional Feature Unsupported"
    };

    TestsStatus.prototype.structured_clone = function()
    {
        if (!this._structured_clone) {
            var msg = this.message;
            msg = msg ? String(msg) : msg;
            this._structured_clone = merge({
                status:this.status,
                message:msg,
                stack:this.stack
            }, TestsStatus.statuses);
        }
        return this._structured_clone;
    };

    TestsStatus.prototype.format_status = function() {
        return this.formats[this.status];
    };

    /**
     * @class
     * Record of an assert that ran.
     *
     * @param {Test} test - The test which ran the assert.
     * @param {string} assert_name - The function name of the assert.
     * @param {Any} args - The arguments passed to the assert function.
     */
    function AssertRecord(test, assert_name, args = []) {
        /** Name of the assert that ran */
        this.assert_name = assert_name;
        /** Test that ran the assert */
        this.test = test;
        // Avoid keeping complex objects alive
        /** Stringification of the arguments that were passed to the assert function */
        this.args = args.map(x => format_value(x).replace(/\n/g, " "));
        /** Status of the assert */
        this.status = null;
    }

    AssertRecord.prototype.structured_clone = function() {
        return {
            assert_name: this.assert_name,
            test: this.test ? this.test.structured_clone() : null,
            args: this.args,
            status: this.status,
        };
    };

    function Tests()
    {
        this.tests = [];
        this.num_pending = 0;

        this.phases = {
            INITIAL:0,
            SETUP:1,
            HAVE_TESTS:2,
            HAVE_RESULTS:3,
            COMPLETE:4
        };
        this.phase = this.phases.INITIAL;

        this.properties = {};

        this.wait_for_finish = false;
        this.processing_callbacks = false;

        this.allow_uncaught_exception = false;

        this.file_is_test = false;
        // This value is lazily initialized in order to avoid introducing a
        // dependency on ECMAScript 2015 Promises to all tests.
        this.promise_tests = null;
        this.promise_setup_called = false;

        this.timeout_multiplier = 1;
        this.timeout_length = test_environment.test_timeout();
        this.timeout_id = null;

        this.start_callbacks = [];
        this.test_state_callbacks = [];
        this.test_done_callbacks = [];
        this.all_done_callbacks = [];

        this.hide_test_state = false;
        this.pending_remotes = [];

        this.current_test = null;
        this.asserts_run = [];

        // Track whether output is enabled, and thus whether or not we should
        // track asserts.
        //
        // On workers we don't get properties set from testharnessreport.js, so
        // we don't know whether or not to track asserts. To avoid the
        // resulting performance hit, we assume we are not meant to. This means
        // that assert tracking does not function on workers.
        this.output = settings.output && 'document' in global_scope;

        this.status = new TestsStatus();

        var this_obj = this;

        test_environment.add_on_loaded_callback(function() {
            if (this_obj.all_done()) {
                this_obj.complete();
            }
        });

        this.set_timeout();
    }

    Tests.prototype.setup = function(func, properties)
    {
        if (this.phase >= this.phases.HAVE_RESULTS) {
            return;
        }

        if (this.phase < this.phases.SETUP) {
            this.phase = this.phases.SETUP;
        }

        this.properties = properties;

        for (var p in properties) {
            if (properties.hasOwnProperty(p)) {
                var value = properties[p];
                if (p == "allow_uncaught_exception") {
                    this.allow_uncaught_exception = value;
                } else if (p == "explicit_done" && value) {
                    this.wait_for_finish = true;
                } else if (p == "explicit_timeout" && value) {
                    this.timeout_length = null;
                    if (this.timeout_id)
                    {
                        clearTimeout(this.timeout_id);
                    }
                } else if (p == "single_test" && value) {
                    this.set_file_is_test();
                } else if (p == "timeout_multiplier") {
                    this.timeout_multiplier = value;
                    if (this.timeout_length) {
                         this.timeout_length *= this.timeout_multiplier;
                    }
                } else if (p == "hide_test_state") {
                    this.hide_test_state = value;
                } else if (p == "output") {
                    this.output = value;
                } else if (p === "debug") {
                    settings.debug = value;
                }
            }
        }

        if (func) {
            try {
                func();
            } catch (e) {
                this.status.status = e instanceof OptionalFeatureUnsupportedError ? this.status.PRECONDITION_FAILED : this.status.ERROR;
                this.status.message = String(e);
                this.status.stack = e.stack ? e.stack : null;
                this.complete();
            }
        }
        this.set_timeout();
    };

    Tests.prototype.set_file_is_test = function() {
        if (this.tests.length > 0) {
            throw new Error("Tried to set file as test after creating a test");
        }
        this.wait_for_finish = true;
        this.file_is_test = true;
        // Create the test, which will add it to the list of tests
        tests.current_test = async_test();
    };

    Tests.prototype.set_status = function(status, message, stack)
    {
        this.status.status = status;
        this.status.message = message;
        this.status.stack = stack ? stack : null;
    };

    Tests.prototype.set_timeout = function() {
        if (global_scope.clearTimeout) {
            var this_obj = this;
            clearTimeout(this.timeout_id);
            if (this.timeout_length !== null) {
                this.timeout_id = setTimeout(function() {
                                                 this_obj.timeout();
                                             }, this.timeout_length);
            }
        }
    };

    Tests.prototype.timeout = function() {
        var test_in_cleanup = null;

        if (this.status.status === null) {
            forEach(this.tests,
                    function(test) {
                        // No more than one test is expected to be in the
                        // "CLEANUP" phase at any time
                        if (test.phase === test.phases.CLEANING) {
                            test_in_cleanup = test;
                        }

                        test.phase = test.phases.COMPLETE;
                    });

            // Timeouts that occur while a test is in the "cleanup" phase
            // indicate that some global state was not properly reverted. This
            // invalidates the overall test execution, so the timeout should be
            // reported as an error and cancel the execution of any remaining
            // tests.
            if (test_in_cleanup) {
                this.status.status = this.status.ERROR;
                this.status.message = "Timeout while running cleanup for " +
                    "test named \"" + test_in_cleanup.name + "\".";
                tests.status.stack = null;
            } else {
                this.status.status = this.status.TIMEOUT;
            }
        }

        this.complete();
    };

    Tests.prototype.end_wait = function()
    {
        this.wait_for_finish = false;
        if (this.all_done()) {
            this.complete();
        }
    };

    Tests.prototype.push = function(test)
    {
        if (this.phase < this.phases.HAVE_TESTS) {
            this.start();
        }
        this.num_pending++;
        test.index = this.tests.push(test);
        this.notify_test_state(test);
    };

    Tests.prototype.notify_test_state = function(test) {
        var this_obj = this;
        forEach(this.test_state_callbacks,
                function(callback) {
                    callback(test, this_obj);
                });
    };

    Tests.prototype.all_done = function() {
        return (this.tests.length > 0 || this.pending_remotes.length > 0) &&
                test_environment.all_loaded &&
                (this.num_pending === 0 || this.is_aborted) && !this.wait_for_finish &&
                !this.processing_callbacks &&
                !this.pending_remotes.some(function(w) { return w.running; });
    };

    Tests.prototype.start = function() {
        this.phase = this.phases.HAVE_TESTS;
        this.notify_start();
    };

    Tests.prototype.notify_start = function() {
        var this_obj = this;
        forEach (this.start_callbacks,
                 function(callback)
                 {
                     callback(this_obj.properties);
                 });
    };

    Tests.prototype.result = function(test)
    {
        // If the harness has already transitioned beyond the `HAVE_RESULTS`
        // phase, subsequent tests should not cause it to revert.
        if (this.phase <= this.phases.HAVE_RESULTS) {
            this.phase = this.phases.HAVE_RESULTS;
        }
        this.num_pending--;
        this.notify_result(test);
    };

    Tests.prototype.notify_result = function(test) {
        var this_obj = this;
        this.processing_callbacks = true;
        forEach(this.test_done_callbacks,
                function(callback)
                {
                    callback(test, this_obj);
                });
        this.processing_callbacks = false;
        if (this_obj.all_done()) {
            this_obj.complete();
        }
    };

    Tests.prototype.complete = function() {
        if (this.phase === this.phases.COMPLETE) {
            return;
        }
        var this_obj = this;
        var all_complete = function() {
            this_obj.phase = this_obj.phases.COMPLETE;
            this_obj.notify_complete();
        };
        var incomplete = filter(this.tests,
                                function(test) {
                                    return test.phase < test.phases.COMPLETE;
                                });

        /**
         * To preserve legacy behavior, overall test completion must be
         * signaled synchronously.
         */
        if (incomplete.length === 0) {
            all_complete();
            return;
        }

        all_async(incomplete,
                  function(test, testDone)
                  {
                      if (test.phase === test.phases.INITIAL) {
                          test.phase = test.phases.COMPLETE;
                          testDone();
                      } else {
                          add_test_done_callback(test, testDone);
                          test.cleanup();
                      }
                  },
                  all_complete);
    };

    Tests.prototype.set_assert = function(assert_name, args) {
        this.asserts_run.push(new AssertRecord(this.current_test, assert_name, args))
    }

    Tests.prototype.set_assert_status = function(index, status, stack) {
        let assert_record = this.asserts_run[index];
        assert_record.status = status;
        assert_record.stack = stack;
    }

    /**
     * Update the harness status to reflect an unrecoverable harness error that
     * should cancel all further testing. Update all previously-defined tests
     * which have not yet started to indicate that they will not be executed.
     */
    Tests.prototype.abort = function() {
        this.status.status = this.status.ERROR;
        this.is_aborted = true;

        forEach(this.tests,
                function(test) {
                    if (test.phase === test.phases.INITIAL) {
                        test.phase = test.phases.COMPLETE;
                    }
                });
    };

    /*
     * Determine if any tests share the same `name` property. Return an array
     * containing the names of any such duplicates.
     */
    Tests.prototype.find_duplicates = function() {
        var names = Object.create(null);
        var duplicates = [];

        forEach (this.tests,
                 function(test)
                 {
                     if (test.name in names && duplicates.indexOf(test.name) === -1) {
                        duplicates.push(test.name);
                     }
                     names[test.name] = true;
                 });

        return duplicates;
    };

    function code_unit_str(char) {
        return 'U+' + char.charCodeAt(0).toString(16);
    }

    function sanitize_unpaired_surrogates(str) {
        return str.replace(
            /([\ud800-\udbff]+)(?![\udc00-\udfff])|(^|[^\ud800-\udbff])([\udc00-\udfff]+)/g,
            function(_, low, prefix, high) {
                var output = prefix || "";  // prefix may be undefined
                var string = low || high;  // only one of these alternates can match
                for (var i = 0; i < string.length; i++) {
                    output += code_unit_str(string[i]);
                }
                return output;
            });
    }

    function sanitize_all_unpaired_surrogates(tests) {
        forEach (tests,
                 function (test)
                 {
                     var sanitized = sanitize_unpaired_surrogates(test.name);

                     if (test.name !== sanitized) {
                         test.name = sanitized;
                         delete test._structured_clone;
                     }
                 });
    }

    Tests.prototype.notify_complete = function() {
        var this_obj = this;
        var duplicates;

        if (this.status.status === null) {
            duplicates = this.find_duplicates();

            // Some transports adhere to UTF-8's restriction on unpaired
            // surrogates. Sanitize the titles so that the results can be
            // consistently sent via all transports.
            sanitize_all_unpaired_surrogates(this.tests);

            // Test names are presumed to be unique within test files--this
            // allows consumers to use them for identification purposes.
            // Duplicated names violate this expectation and should therefore
            // be reported as an error.
            if (duplicates.length) {
                this.status.status = this.status.ERROR;
                this.status.message =
                   duplicates.length + ' duplicate test name' +
                   (duplicates.length > 1 ? 's' : '') + ': "' +
                   duplicates.join('", "') + '"';
            } else {
                this.status.status = this.status.OK;
            }
        }

        forEach (this.all_done_callbacks,
                 function(callback)
                 {
                     callback(this_obj.tests, this_obj.status, this_obj.asserts_run);
                 });
    };

    /*
     * Constructs a RemoteContext that tracks tests from a specific worker.
     */
    Tests.prototype.create_remote_worker = function(worker) {
        var message_port;

        if (is_service_worker(worker)) {
            message_port = navigator.serviceWorker;
            worker.postMessage({type: "connect"});
        } else if (is_shared_worker(worker)) {
            message_port = worker.port;
            message_port.start();
        } else {
            message_port = worker;
        }

        return new RemoteContext(worker, message_port);
    };

    /*
     * Constructs a RemoteContext that tracks tests from a specific window.
     */
    Tests.prototype.create_remote_window = function(remote) {
        remote.postMessage({type: "getmessages"}, "*");
        return new RemoteContext(
            remote,
            window,
            function(msg) {
                return msg.source === remote;
            }
        );
    };

    Tests.prototype.fetch_tests_from_worker = function(worker) {
        if (this.phase >= this.phases.COMPLETE) {
            return;
        }

        var remoteContext = this.create_remote_worker(worker);
        this.pending_remotes.push(remoteContext);
        return remoteContext.done;
    };

    /**
     * Get test results from a worker and include them in the current test.
     *
     * @param {Worker|SharedWorker|ServiceWorker|MessagePort} port -
     * Either a worker object or a port connected to a worker which is
     * running tests..
     * @returns {Promise} - A promise that's resolved once all the remote tests are complete.
     */
    function fetch_tests_from_worker(port) {
        return tests.fetch_tests_from_worker(port);
    }
    expose(fetch_tests_from_worker, 'fetch_tests_from_worker');

    Tests.prototype.fetch_tests_from_window = function(remote) {
        if (this.phase >= this.phases.COMPLETE) {
            return;
        }

        var remoteContext = this.create_remote_window(remote);
        this.pending_remotes.push(remoteContext);
        return remoteContext.done;
    };

    /**
     * Aggregate tests from separate windows or iframes
     * into the current document as if they were all part of the same test file.
     *
     * The document of the second window (or iframe) should include
     * ``testharness.js``, but not ``testharnessreport.js``, and use
     * :js:func:`test`, :js:func:`async_test`, and :js:func:`promise_test` in
     * the usual manner.
     *
     * @param {Window} window - The window to fetch tests from.
     */
    function fetch_tests_from_window(window) {
        return tests.fetch_tests_from_window(window);
    }
    expose(fetch_tests_from_window, 'fetch_tests_from_window');

    /**
     * Get test results from a shadow realm and include them in the current test.
     *
     * @param {ShadowRealm} realm - A shadow realm also running the test harness
     * @returns {Promise} - A promise that's resolved once all the remote tests are complete.
     */
    function fetch_tests_from_shadow_realm(realm) {
        var chan = new MessageChannel();
        function receiveMessage(msg_json) {
            chan.port1.postMessage(JSON.parse(msg_json));
        }
        var done = tests.fetch_tests_from_worker(chan.port2);
        realm.evaluate("begin_shadow_realm_tests")(receiveMessage);
        chan.port2.start();
        return done;
    }
    expose(fetch_tests_from_shadow_realm, 'fetch_tests_from_shadow_realm');

    /**
     * Begin running tests in this shadow realm test harness.
     *
     * To be called after all tests have been loaded; it is an error to call
     * this more than once or in a non-Shadow Realm environment
     *
     * @param {Function} postMessage - A function to send test updates to the
     * incubating realm-- accepts JSON-encoded messages in the format used by
     * RemoteContext
     */
    function begin_shadow_realm_tests(postMessage) {
        if (!(test_environment instanceof ShadowRealmTestEnvironment)) {
            throw new Error("begin_shadow_realm_tests called in non-Shadow Realm environment");
        }

        test_environment.begin(function (msg) {
            postMessage(JSON.stringify(msg));
        });
    }
    expose(begin_shadow_realm_tests, 'begin_shadow_realm_tests');

    /**
     * Timeout the tests.
     *
     * This only has an effect when ``explicit_timeout`` has been set
     * in :js:func:`setup`. In other cases any call is a no-op.
     *
     */
    function timeout() {
        if (tests.timeout_length === null) {
            tests.timeout();
        }
    }
    expose(timeout, 'timeout');

    /**
     * Add a callback that's triggered when the first :js:class:`Test` is created.
     *
     * @param {Function} callback - Callback function. This is called
     * without arguments.
     */
    function add_start_callback(callback) {
        tests.start_callbacks.push(callback);
    }

    /**
     * Add a callback that's triggered when a test state changes.
     *
     * @param {Function} callback - Callback function, called with the
     * :js:class:`Test` as the only argument.
     */
    function add_test_state_callback(callback) {
        tests.test_state_callbacks.push(callback);
    }

    /**
     * Add a callback that's triggered when a test result is received.
     *
     * @param {Function} callback - Callback function, called with the
     * :js:class:`Test` as the only argument.
     */
    function add_result_callback(callback) {
        tests.test_done_callbacks.push(callback);
    }

    /**
     * Add a callback that's triggered when all tests are complete.
     *
     * @param {Function} callback - Callback function, called with an
     * array of :js:class:`Test` objects, a :js:class:`TestsStatus`
     * object and an array of :js:class:`AssertRecord` objects. If the
     * debug setting is ``false`` the final argument will be an empty
     * array.
     *
     * For performance reasons asserts are only tracked when the debug
     * setting is ``true``. In other cases the array of asserts will be
     * empty.
     */
    function add_completion_callback(callback) {
        tests.all_done_callbacks.push(callback);
    }

    expose(add_start_callback, 'add_start_callback');
    expose(add_test_state_callback, 'add_test_state_callback');
    expose(add_result_callback, 'add_result_callback');
    expose(add_completion_callback, 'add_completion_callback');

    function remove(array, item) {
        var index = array.indexOf(item);
        if (index > -1) {
            array.splice(index, 1);
        }
    }

    function remove_start_callback(callback) {
        remove(tests.start_callbacks, callback);
    }

    function remove_test_state_callback(callback) {
        remove(tests.test_state_callbacks, callback);
    }

    function remove_result_callback(callback) {
        remove(tests.test_done_callbacks, callback);
    }

    function remove_completion_callback(callback) {
       remove(tests.all_done_callbacks, callback);
    }

    /*
     * Output listener
    */

    function Output() {
        this.output_document = document;
        this.output_node = null;
        this.enabled = settings.output;
        this.phase = this.INITIAL;
    }

    Output.prototype.INITIAL = 0;
    Output.prototype.STARTED = 1;
    Output.prototype.HAVE_RESULTS = 2;
    Output.prototype.COMPLETE = 3;

    Output.prototype.setup = function(properties) {
        if (this.phase > this.INITIAL) {
            return;
        }

        //If output is disabled in testharnessreport.js the test shouldn't be
        //able to override that
        this.enabled = this.enabled && (properties.hasOwnProperty("output") ?
                                        properties.output : settings.output);
    };

    Output.prototype.init = function(properties) {
        if (this.phase >= this.STARTED) {
            return;
        }
        if (properties.output_document) {
            this.output_document = properties.output_document;
        } else {
            this.output_document = document;
        }
        this.phase = this.STARTED;
    };

    Output.prototype.resolve_log = function() {
        var output_document;
        if (this.output_node) {
            return;
        }
        if (typeof this.output_document === "function") {
            output_document = this.output_document.apply(undefined);
        } else {
            output_document = this.output_document;
        }
        if (!output_document) {
            return;
        }
        var node = output_document.getElementById("log");
        if (!node) {
            if (output_document.readyState === "loading") {
                return;
            }
            node = output_document.createElementNS("http://www.w3.org/1999/xhtml", "div");
            node.id = "log";
            if (output_document.body) {
                output_document.body.appendChild(node);
            } else {
                var root = output_document.documentElement;
                var is_html = (root &&
                               root.namespaceURI == "http://www.w3.org/1999/xhtml" &&
                               root.localName == "html");
                var is_svg = (output_document.defaultView &&
                              "SVGSVGElement" in output_document.defaultView &&
                              root instanceof output_document.defaultView.SVGSVGElement);
                if (is_svg) {
                    var foreignObject = output_document.createElementNS("http://www.w3.org/2000/svg", "foreignObject");
                    foreignObject.setAttribute("width", "100%");
                    foreignObject.setAttribute("height", "100%");
                    root.appendChild(foreignObject);
                    foreignObject.appendChild(node);
                } else if (is_html) {
                    root.appendChild(output_document.createElementNS("http://www.w3.org/1999/xhtml", "body"))
                        .appendChild(node);
                } else {
                    root.appendChild(node);
                }
            }
        }
        this.output_document = output_document;
        this.output_node = node;
    };

    Output.prototype.show_status = function() {
        if (this.phase < this.STARTED) {
            this.init({});
        }
        if (!this.enabled || this.phase === this.COMPLETE) {
            return;
        }
        this.resolve_log();
        if (this.phase < this.HAVE_RESULTS) {
            this.phase = this.HAVE_RESULTS;
        }
        var done_count = tests.tests.length - tests.num_pending;
        if (this.output_node && !tests.hide_test_state) {
            if (done_count < 100 ||
                (done_count < 1000 && done_count % 100 === 0) ||
                done_count % 1000 === 0) {
                this.output_node.textContent = "Running, " +
                    done_count + " complete, " +
                    tests.num_pending + " remain";
            }
        }
    };

    Output.prototype.show_results = function (tests, harness_status, asserts_run) {
        if (this.phase >= this.COMPLETE) {
            return;
        }
        if (!this.enabled) {
            return;
        }
        if (!this.output_node) {
            this.resolve_log();
        }
        this.phase = this.COMPLETE;

        var log = this.output_node;
        if (!log) {
            return;
        }
        var output_document = this.output_document;

        while (log.lastChild) {
            log.removeChild(log.lastChild);
        }

        var stylesheet = output_document.createElementNS(xhtml_ns, "style");
        stylesheet.textContent = stylesheetContent;
        var heads = output_document.getElementsByTagName("head");
        if (heads.length) {
            heads[0].appendChild(stylesheet);
        }

        var status_number = {};
        forEach(tests,
                function(test) {
                    var status = test.format_status();
                    if (status_number.hasOwnProperty(status)) {
                        status_number[status] += 1;
                    } else {
                        status_number[status] = 1;
                    }
                });

        function status_class(status)
        {
            return status.replace(/\s/g, '').toLowerCase();
        }

        var summary_template = ["section", {"id":"summary"},
                                ["h2", {}, "Summary"],
                                function()
                                {
                                    var status = harness_status.format_status();
                                    var rv = [["section", {},
                                               ["p", {},
                                                "Harness status: ",
                                                ["span", {"class":status_class(status)},
                                                 status
                                                ],
                                               ],
                                               ["button",
                                                {"onclick": "let evt = new Event('__test_restart'); " +
                                                 "let canceled = !window.dispatchEvent(evt);" +
                                                 "if (!canceled) { location.reload() }"},
                                                "Rerun"]
                                              ]];

                                    if (harness_status.status === harness_status.ERROR) {
                                        rv[0].push(["pre", {}, harness_status.message]);
                                        if (harness_status.stack) {
                                            rv[0].push(["pre", {}, harness_status.stack]);
                                        }
                                    }
                                    return rv;
                                },
                                ["p", {}, "Found ${num_tests} tests"],
                                function() {
                                    var rv = [["div", {}]];
                                    var i = 0;
                                    while (Test.prototype.status_formats.hasOwnProperty(i)) {
                                        if (status_number.hasOwnProperty(Test.prototype.status_formats[i])) {
                                            var status = Test.prototype.status_formats[i];
                                            rv[0].push(["div", {},
                                                        ["label", {},
                                                         ["input", {type:"checkbox", checked:"checked"}],
                                                         status_number[status] + " ",
                                                         ["span", {"class":status_class(status)}, status]]]);
                                        }
                                        i++;
                                    }
                                    return rv;
                                },
                               ];

        log.appendChild(render(summary_template, {num_tests:tests.length}, output_document));

        forEach(output_document.querySelectorAll("section#summary label"),
                function(element)
                {
                    on_event(element, "click",
                             function(e)
                             {
                                 if (output_document.getElementById("results") === null) {
                                     e.preventDefault();
                                     return;
                                 }
                                 var result_class = element.querySelector("span[class]").getAttribute("class");
                                 var style_element = output_document.querySelector("style#hide-" + result_class);
                                 var input_element = element.querySelector("input");
                                 if (!style_element && !input_element.checked) {
                                     style_element = output_document.createElementNS(xhtml_ns, "style");
                                     style_element.id = "hide-" + result_class;
                                     style_element.textContent = "table#results > tbody > tr.overall-"+result_class+"{display:none}";
                                     output_document.body.appendChild(style_element);
                                 } else if (style_element && input_element.checked) {
                                     style_element.parentNode.removeChild(style_element);
                                 }
                             });
                });

        // This use of innerHTML plus manual escaping is not recommended in
        // general, but is necessary here for performance.  Using textContent
        // on each individual <td> adds tens of seconds of execution time for
        // large test suites (tens of thousands of tests).
        function escape_html(s)
        {
            return s.replace(/\&/g, "&amp;")
                .replace(/</g, "&lt;")
                .replace(/"/g, "&quot;")
                .replace(/'/g, "&#39;");
        }

        function has_assertions()
        {
            for (var i = 0; i < tests.length; i++) {
                if (tests[i].properties.hasOwnProperty("assert")) {
                    return true;
                }
            }
            return false;
        }

        function get_assertion(test)
        {
            if (test.properties.hasOwnProperty("assert")) {
                if (Array.isArray(test.properties.assert)) {
                    return test.properties.assert.join(' ');
                }
                return test.properties.assert;
            }
            return '';
        }

        var asserts_run_by_test = new Map();
        asserts_run.forEach(assert => {
            if (!asserts_run_by_test.has(assert.test)) {
                asserts_run_by_test.set(assert.test, []);
            }
            asserts_run_by_test.get(assert.test).push(assert);
        });

        function get_asserts_output(test) {
            var asserts = asserts_run_by_test.get(test);
            if (!asserts) {
                return "No asserts ran";
            }
            rv = "<table>";
            rv += asserts.map(assert => {
                var output_fn = "<strong>" + escape_html(assert.assert_name) + "</strong>(";
                var prefix_len = output_fn.length;
                var output_args = assert.args;
                var output_len = output_args.reduce((prev, current) => prev+current, prefix_len);
                if (output_len[output_len.length - 1] > 50) {
                    output_args = output_args.map((x, i) =>
                    (i > 0 ? "  ".repeat(prefix_len) : "" )+ x + (i < output_args.length - 1 ? ",\n" : ""));
                } else {
                    output_args = output_args.map((x, i) => x + (i < output_args.length - 1 ? ", " : ""));
                }
                output_fn += escape_html(output_args.join(""));
                output_fn += ')';
                var output_location;
                if (assert.stack) {
                    output_location = assert.stack.split("\n", 1)[0].replace(/@?\w+:\/\/[^ "\/]+(?::\d+)?/g, " ");
                }
                return "<tr class='overall-" +
                    status_class(Test.prototype.status_formats[assert.status]) + "'>" +
                    "<td class='" +
                    status_class(Test.prototype.status_formats[assert.status]) + "'>" +
                    Test.prototype.status_formats[assert.status] + "</td>" +
                    "<td><pre>" +
                    output_fn +
                    (output_location ? "\n" + escape_html(output_location) : "") +
                    "</pre></td></tr>";
            }
            ).join("\n");
            rv += "</table>";
            return rv;
        }

        log.appendChild(document.createElementNS(xhtml_ns, "section"));
        var assertions = has_assertions();
        var html = "<h2>Details</h2><table id='results' " + (assertions ? "class='assertions'" : "" ) + ">" +
            "<thead><tr><th>Result</th><th>Test Name</th>" +
            (assertions ? "<th>Assertion</th>" : "") +
            "<th>Message</th></tr></thead>" +
            "<tbody>";
        for (var i = 0; i < tests.length; i++) {
            var test = tests[i];
            html += '<tr class="overall-' +
                status_class(test.format_status()) +
                '">' +
                '<td class="' +
                status_class(test.format_status()) +
                '">' +
                test.format_status() +
                "</td><td>" +
                escape_html(test.name) +
                "</td><td>" +
                (assertions ? escape_html(get_assertion(test)) + "</td><td>" : "") +
                escape_html(test.message ? tests[i].message : " ") +
                (tests[i].stack ? "<pre>" +
                 escape_html(tests[i].stack) +
                 "</pre>": "");
            if (!(test instanceof RemoteTest)) {
                 html += "<details><summary>Asserts run</summary>" + get_asserts_output(test) + "</details>"
            }
            html += "</td></tr>";
        }
        html += "</tbody></table>";
        try {
            log.lastChild.innerHTML = html;
        } catch (e) {
            log.appendChild(document.createElementNS(xhtml_ns, "p"))
               .textContent = "Setting innerHTML for the log threw an exception.";
            log.appendChild(document.createElementNS(xhtml_ns, "pre"))
               .textContent = html;
        }
    };

    /*
     * Template code
     *
     * A template is just a JavaScript structure. An element is represented as:
     *
     * [tag_name, {attr_name:attr_value}, child1, child2]
     *
     * the children can either be strings (which act like text nodes), other templates or
     * functions (see below)
     *
     * A text node is represented as
     *
     * ["{text}", value]
     *
     * String values have a simple substitution syntax; ${foo} represents a variable foo.
     *
     * It is possible to embed logic in templates by using a function in a place where a
     * node would usually go. The function must either return part of a template or null.
     *
     * In cases where a set of nodes are required as output rather than a single node
     * with children it is possible to just use a list
     * [node1, node2, node3]
     *
     * Usage:
     *
     * render(template, substitutions) - take a template and an object mapping
     * variable names to parameters and return either a DOM node or a list of DOM nodes
     *
     * substitute(template, substitutions) - take a template and variable mapping object,
     * make the variable substitutions and return the substituted template
     *
     */

    function is_single_node(template)
    {
        return typeof template[0] === "string";
    }

    function substitute(template, substitutions)
    {
        if (typeof template === "function") {
            var replacement = template(substitutions);
            if (!replacement) {
                return null;
            }

            return substitute(replacement, substitutions);
        }

        if (is_single_node(template)) {
            return substitute_single(template, substitutions);
        }

        return filter(map(template, function(x) {
                              return substitute(x, substitutions);
                          }), function(x) {return x !== null;});
    }

    function substitute_single(template, substitutions)
    {
        var substitution_re = /\$\{([^ }]*)\}/g;

        function do_substitution(input) {
            var components = input.split(substitution_re);
            var rv = [];
            for (var i = 0; i < components.length; i += 2) {
                rv.push(components[i]);
                if (components[i + 1]) {
                    rv.push(String(substitutions[components[i + 1]]));
                }
            }
            return rv;
        }

        function substitute_attrs(attrs, rv)
        {
            rv[1] = {};
            for (var name in template[1]) {
                if (attrs.hasOwnProperty(name)) {
                    var new_name = do_substitution(name).join("");
                    var new_value = do_substitution(attrs[name]).join("");
                    rv[1][new_name] = new_value;
                }
            }
        }

        function substitute_children(children, rv)
        {
            for (var i = 0; i < children.length; i++) {
                if (children[i] instanceof Object) {
                    var replacement = substitute(children[i], substitutions);
                    if (replacement !== null) {
                        if (is_single_node(replacement)) {
                            rv.push(replacement);
                        } else {
                            extend(rv, replacement);
                        }
                    }
                } else {
                    extend(rv, do_substitution(String(children[i])));
                }
            }
            return rv;
        }

        var rv = [];
        rv.push(do_substitution(String(template[0])).join(""));

        if (template[0] === "{text}") {
            substitute_children(template.slice(1), rv);
        } else {
            substitute_attrs(template[1], rv);
            substitute_children(template.slice(2), rv);
        }

        return rv;
    }

    function make_dom_single(template, doc)
    {
        var output_document = doc || document;
        var element;
        if (template[0] === "{text}") {
            element = output_document.createTextNode("");
            for (var i = 1; i < template.length; i++) {
                element.data += template[i];
            }
        } else {
            element = output_document.createElementNS(xhtml_ns, template[0]);
            for (var name in template[1]) {
                if (template[1].hasOwnProperty(name)) {
                    element.setAttribute(name, template[1][name]);
                }
            }
            for (var i = 2; i < template.length; i++) {
                if (template[i] instanceof Object) {
                    var sub_element = make_dom(template[i]);
                    element.appendChild(sub_element);
                } else {
                    var text_node = output_document.createTextNode(template[i]);
                    element.appendChild(text_node);
                }
            }
        }

        return element;
    }

    function make_dom(template, substitutions, output_document)
    {
        if (is_single_node(template)) {
            return make_dom_single(template, output_document);
        }

        return map(template, function(x) {
                       return make_dom_single(x, output_document);
                   });
    }

    function render(template, substitutions, output_document)
    {
        return make_dom(substitute(template, substitutions), output_document);
    }

    /*
     * Utility functions
     */
    function assert(expected_true, function_name, description, error, substitutions)
    {
        if (expected_true !== true) {
            var msg = make_message(function_name, description,
                                   error, substitutions);
            throw new AssertionError(msg);
        }
    }

    /**
     * @class
     * Exception type that represents a failing assert.
     *
     * @param {string} message - Error message.
     */
    function AssertionError(message)
    {
        if (typeof message == "string") {
            message = sanitize_unpaired_surrogates(message);
        }
        this.message = message;
        this.stack = get_stack();
    }
    expose(AssertionError, "AssertionError");

    AssertionError.prototype = Object.create(Error.prototype);

    const get_stack = function() {
        var stack = new Error().stack;

        // 'Error.stack' is not supported in all browsers/versions
        if (!stack) {
            return "(Stack trace unavailable)";
        }

        var lines = stack.split("\n");

        // Create a pattern to match stack frames originating within testharness.js.  These include the
        // script URL, followed by the line/col (e.g., '/resources/testharness.js:120:21').
        // Escape the URL per http://stackoverflow.com/questions/3561493/is-there-a-regexp-escape-function-in-javascript
        // in case it contains RegExp characters.
        var script_url = get_script_url();
        var re_text = script_url ? script_url.replace(/[-\/\\^$*+?.()|[\]{}]/g, '\\$&') : "\\btestharness.js";
        var re = new RegExp(re_text + ":\\d+:\\d+");

        // Some browsers include a preamble that specifies the type of the error object.  Skip this by
        // advancing until we find the first stack frame originating from testharness.js.
        var i = 0;
        while (!re.test(lines[i]) && i < lines.length) {
            i++;
        }

        // Then skip the top frames originating from testharness.js to begin the stack at the test code.
        while (re.test(lines[i]) && i < lines.length) {
            i++;
        }

        // Paranoid check that we didn't skip all frames.  If so, return the original stack unmodified.
        if (i >= lines.length) {
            return stack;
        }

        return lines.slice(i).join("\n");
    }

    function OptionalFeatureUnsupportedError(message)
    {
        AssertionError.call(this, message);
    }
    OptionalFeatureUnsupportedError.prototype = Object.create(AssertionError.prototype);
    expose(OptionalFeatureUnsupportedError, "OptionalFeatureUnsupportedError");

    function make_message(function_name, description, error, substitutions)
    {
        for (var p in substitutions) {
            if (substitutions.hasOwnProperty(p)) {
                substitutions[p] = format_value(substitutions[p]);
            }
        }
        var node_form = substitute(["{text}", "${function_name}: ${description}" + error],
                                   merge({function_name:function_name,
                                          description:(description?description + " ":"")},
                                          substitutions));
        return node_form.slice(1).join("");
    }

    function filter(array, callable, thisObj) {
        var rv = [];
        for (var i = 0; i < array.length; i++) {
            if (array.hasOwnProperty(i)) {
                var pass = callable.call(thisObj, array[i], i, array);
                if (pass) {
                    rv.push(array[i]);
                }
            }
        }
        return rv;
    }

    function map(array, callable, thisObj)
    {
        var rv = [];
        rv.length = array.length;
        for (var i = 0; i < array.length; i++) {
            if (array.hasOwnProperty(i)) {
                rv[i] = callable.call(thisObj, array[i], i, array);
            }
        }
        return rv;
    }

    function extend(array, items)
    {
        Array.prototype.push.apply(array, items);
    }

    function forEach(array, callback, thisObj)
    {
        for (var i = 0; i < array.length; i++) {
            if (array.hasOwnProperty(i)) {
                callback.call(thisObj, array[i], i, array);
            }
        }
    }

    /**
     * Immediately invoke a "iteratee" function with a series of values in
     * parallel and invoke a final "done" function when all of the "iteratee"
     * invocations have signaled completion.
     *
     * If all callbacks complete synchronously (or if no callbacks are
     * specified), the ``done_callback`` will be invoked synchronously. It is the
     * responsibility of the caller to ensure asynchronicity in cases where
     * that is desired.
     *
     * @param {array} value Zero or more values to use in the invocation of
     *                      ``iter_callback``
     * @param {function} iter_callback A function that will be invoked
     *                                 once for each of the values min
     *                                 ``value``. Two arguments will
     *                                 be available in each
     *                                 invocation: the value from
     *                                 ``value`` and a function that
     *                                 must be invoked to signal
     *                                 completion
     * @param {function} done_callback A function that will be invoked after
     *                                 all operations initiated by the
     *                                 ``iter_callback`` function have signaled
     *                                 completion
     */
    function all_async(values, iter_callback, done_callback)
    {
        var remaining = values.length;

        if (remaining === 0) {
            done_callback();
        }

        forEach(values,
                function(element) {
                    var invoked = false;
                    var elDone = function() {
                        if (invoked) {
                            return;
                        }

                        invoked = true;
                        remaining -= 1;

                        if (remaining === 0) {
                            done_callback();
                        }
                    };

                    iter_callback(element, elDone);
                });
    }

    function merge(a,b)
    {
        var rv = {};
        var p;
        for (p in a) {
            rv[p] = a[p];
        }
        for (p in b) {
            rv[p] = b[p];
        }
        return rv;
    }

    function expose(object, name)
    {
        var components = name.split(".");
        var target = global_scope;
        for (var i = 0; i < components.length - 1; i++) {
            if (!(components[i] in target)) {
                target[components[i]] = {};
            }
            target = target[components[i]];
        }
        target[components[components.length - 1]] = object;
    }

    function is_same_origin(w) {
        try {
            'random_prop' in w;
            return true;
        } catch (e) {
            return false;
        }
    }

    /** Returns the 'src' URL of the first <script> tag in the page to include the file 'testharness.js'. */
    function get_script_url()
    {
        if (!('document' in global_scope)) {
            return undefined;
        }

        var scripts = document.getElementsByTagName("script");
        for (var i = 0; i < scripts.length; i++) {
            var src;
            if (scripts[i].src) {
                src = scripts[i].src;
            } else if (scripts[i].href) {
                //SVG case
                src = scripts[i].href.baseVal;
            }

            var matches = src && src.match(/^(.*\/|)testharness\.js$/);
            if (matches) {
                return src;
            }
        }
        return undefined;
    }

    /** Returns the <title> or filename or "Untitled" */
    function get_title()
    {
        if ('document' in global_scope) {
            //Don't use document.title to work around an Opera/Presto bug in XHTML documents
            var title = document.getElementsByTagName("title")[0];
            if (title && title.firstChild && title.firstChild.data) {
                return title.firstChild.data;
            }
        }
        if ('META_TITLE' in global_scope && META_TITLE) {
            return META_TITLE;
        }
        if ('location' in global_scope && 'pathname' in location) {
            return location.pathname.substring(location.pathname.lastIndexOf('/') + 1, location.pathname.indexOf('.'));
        }
        return "Untitled";
    }

    /**
     * Setup globals
     */

    var tests = new Tests();

    if (global_scope.addEventListener) {
        var error_handler = function(error, message, stack) {
            var optional_unsupported = error instanceof OptionalFeatureUnsupportedError;
            if (tests.file_is_test) {
                var test = tests.tests[0];
                if (test.phase >= test.phases.HAS_RESULT) {
                    return;
                }
                var status = optional_unsupported ? test.PRECONDITION_FAILED : test.FAIL;
                test.set_status(status, message, stack);
                test.phase = test.phases.HAS_RESULT;
            } else if (!tests.allow_uncaught_exception) {
                var status = optional_unsupported ? tests.status.PRECONDITION_FAILED : tests.status.ERROR;
                tests.status.status = status;
                tests.status.message = message;
                tests.status.stack = stack;
            }

            // Do not transition to the "complete" phase if the test has been
            // configured to allow uncaught exceptions. This gives the test an
            // opportunity to define subtests based on the exception reporting
            // behavior.
            if (!tests.allow_uncaught_exception) {
                done();
            }
        };

        addEventListener("error", function(e) {
            var message = e.message;
            var stack;
            if (e.error && e.error.stack) {
                stack = e.error.stack;
            } else {
                stack = e.filename + ":" + e.lineno + ":" + e.colno;
            }
            error_handler(e.error, message, stack);
        }, false);

        addEventListener("unhandledrejection", function(e) {
            var message;
            if (e.reason && e.reason.message) {
                message = "Unhandled rejection: " + e.reason.message;
            } else {
                message = "Unhandled rejection";
            }
            var stack;
            if (e.reason && e.reason.stack) {
                stack = e.reason.stack;
            }
            error_handler(e.reason, message, stack);
        }, false);
    }

    test_environment.on_tests_ready();

    /**
     * Stylesheet
     */
     var stylesheetContent = "\
html {\
    font-family:DejaVu Sans, Bitstream Vera Sans, Arial, Sans;\
}\
\
#log .warning,\
#log .warning a {\
  color: black;\
  background: yellow;\
}\
\
#log .error,\
#log .error a {\
  color: white;\
  background: red;\
}\
\
section#summary {\
    margin-bottom:1em;\
}\
\
table#results {\
    border-collapse:collapse;\
    table-layout:fixed;\
    width:100%;\
}\
\
table#results > thead > tr > th:first-child,\
table#results > tbody > tr > td:first-child {\
    width:8em;\
}\
\
table#results > thead > tr > th:last-child,\
table#results > thead > tr > td:last-child {\
    width:50%;\
}\
\
table#results.assertions > thead > tr > th:last-child,\
table#results.assertions > tbody > tr > td:last-child {\
    width:35%;\
}\
\
table#results > thead > > tr > th {\
    padding:0;\
    padding-bottom:0.5em;\
    border-bottom:medium solid black;\
}\
\
table#results > tbody > tr> td {\
    padding:1em;\
    padding-bottom:0.5em;\
    border-bottom:thin solid black;\
}\
\
.pass {\
    color:green;\
}\
\
.fail {\
    color:red;\
}\
\
tr.timeout {\
    color:red;\
}\
\
tr.notrun {\
    color:blue;\
}\
\
tr.optionalunsupported {\
    color:blue;\
}\
\
.ok {\
    color:green;\
}\
\
.error {\
    color:red;\
}\
\
.pass, .fail, .timeout, .notrun, .optionalunsupported .ok, .timeout, .error {\
    font-variant:small-caps;\
}\
\
table#results span {\
    display:block;\
}\
\
table#results span.expected {\
    font-family:DejaVu Sans Mono, Bitstream Vera Sans Mono, Monospace;\
    white-space:pre;\
}\
\
table#results span.actual {\
    font-family:DejaVu Sans Mono, Bitstream Vera Sans Mono, Monospace;\
    white-space:pre;\
}\
";

})(self);
// vim: set expandtab shiftwidth=4 tabstop=4:
