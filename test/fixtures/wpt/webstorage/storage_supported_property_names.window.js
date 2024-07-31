["localStorage", "sessionStorage"].forEach(function(name) {
    test(function() {
        var storage = window[name];
        storage.clear();

        storage["name"] = "user1";
        assert_array_equals(Object.getOwnPropertyNames(storage), ['name']);
    }, "Object.getOwnPropertyNames on " + name + " Storage");

    test(function() {
        var storage = window[name];
        storage.clear();
        assert_array_equals(Object.getOwnPropertyNames(storage), []);
    }, "Object.getOwnPropertyNames on " + name + " storage with empty collection");
});
