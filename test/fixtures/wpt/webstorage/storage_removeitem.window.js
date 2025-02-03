["localStorage", "sessionStorage"].forEach(function(name) {
    test(function() {
        var storage = window[name];
        storage.clear();

        storage.setItem("name", "user1");
        assert_equals(storage.getItem("name"), "user1");

        storage.removeItem("name");
        storage.removeItem("unknown");
        assert_equals(storage.getItem("name"), null, "storage.getItem('name')")
    }, name + ".removeItem()");

    test(function() {
        var storage = window[name];
        storage.clear();

        storage.setItem("name", "user1");
        assert_equals(storage.getItem("name"), "user1");
        delete storage["name"];
        delete storage["unknown"];
        assert_equals(storage.getItem("name"), null, "storage.getItem('name')")
    }, "delete " + name + "[]");

    test(function() {
        var storage = window[name];
        storage.clear();

        storage.setItem("null", "test");
        assert_true("null" in storage);
        storage.removeItem(null);
        assert_false("null" in storage);
    }, name + ".removeItem(null)");

    test(function() {
        var storage = window[name];
        storage.clear();

        storage.setItem("undefined", "test");
        assert_true("undefined" in storage);
        storage.removeItem(undefined);
        assert_false("undefined" in storage);
    }, name + ".removeItem(undefined)");
});
