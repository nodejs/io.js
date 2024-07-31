async_test(function(t) {

    var storage = window.sessionStorage;
    storage.clear();

    storage.setItem("FOO", "BAR");

    let channel = new BroadcastChannel("storage_session_window_noopener");
    channel.addEventListener("message", t.step_func(function(e) {
        e.data.forEach(t.step_func(function(assertion) {
            assert_equals(assertion.actual, assertion.expected, assertion.message);
        }));
        assert_equals(storage.getItem("FOO"), "BAR", "value for FOO in original window");
        t.done();
    }));

    var win = window.open("resources/storage_session_window_noopener_second.html",
                          "_blank",
                          "noopener");

}, "A new noopener window to make sure there is a not copy of the previous window's sessionStorage");
