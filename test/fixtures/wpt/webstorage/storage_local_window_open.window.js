async_test(function(t) {

    var storage = window.localStorage;
    storage.clear();

    storage.setItem("FOO", "BAR");
    var win = window.open("resources/storage_local_window_open_second.html");
    window.addEventListener('message', t.step_func(function(e) {
        e.data.forEach(t.step_func(function(assertion) {
            assert_equals(assertion.actual, assertion.expected, assertion.message);
        }));
        win.close();
        t.done();
    }));

}, "A new window to make sure there is a copy of the previous window's localStorage, and that they do not diverge after a change");
