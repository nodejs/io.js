["localStorage", "sessionStorage"].forEach(function(name) {
    test(function() {
        var storage = window[name];
        storage.clear();

        storage.setItem("name", "user1");
        assert_equals(storage.getItem("name"), "user1");
        assert_equals(storage.name, "user1");
        assert_equals(storage.length, 1);

        storage.clear();
        assert_equals(storage.getItem("name"), null, "storage.getItem('name')");
        assert_equals(storage.name, undefined, "storage.name");
        assert_equals(storage.length, 0, "storage.length");
    }, "Clear in " + name);
});
