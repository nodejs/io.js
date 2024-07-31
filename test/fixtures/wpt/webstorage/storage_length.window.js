["localStorage", "sessionStorage"].forEach(function(name) {
    test(function() {
        var storage = window[name];
        storage.clear();
        assert_equals(storage.length, 0, "storage.length")

        storage["name"] = "user1";
        storage["age"] = "20";

        assert_equals(storage.length, 2, "storage.length")
    }, name + ".length (method access)");

    test(function() {
        var storage = window[name];
        storage.clear();
        assert_equals(storage.length, 0, "storage.length")

        storage.setItem("name", "user1");
        storage.setItem("age", "20");

        assert_equals(storage.length, 2, "storage.length")
    }, name + ".length (proprty access)");
});
