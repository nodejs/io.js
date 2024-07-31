["localStorage", "sessionStorage"].forEach(function(name) {
    test(function() {
        var storage = window[name];
        storage.clear();
        storage.setItem("name", "x");
        storage.setItem("undefined", "foo");
        storage.setItem("null", "bar");
        storage.setItem("", "baz");

        test(function() {
            assert_equals(storage.length, 4);
        }, "All items should be added to " + name + ".");

        test(function() {
            assert_equals(storage["unknown"], undefined, "storage['unknown']")
            assert_equals(storage["name"], "x", "storage['name']")
            assert_equals(storage["undefined"], "foo", "storage['undefined']")
            assert_equals(storage["null"], "bar", "storage['null']")
            assert_equals(storage[undefined], "foo", "storage[undefined]")
            assert_equals(storage[null], "bar", "storage[null]")
            assert_equals(storage[""], "baz", "storage['']")
        }, "Named access to " + name + " should be correct");

        test(function() {
            assert_equals(storage.getItem("unknown"), null, "storage.getItem('unknown')")
            assert_equals(storage.getItem("name"), "x", "storage.getItem('name')")
            assert_equals(storage.getItem("undefined"), "foo", "storage.getItem('undefined')")
            assert_equals(storage.getItem("null"), "bar", "storage.getItem('null')")
            assert_equals(storage.getItem(undefined), "foo", "storage.getItem(undefined)")
            assert_equals(storage.getItem(null), "bar", "storage.getItem(null)")
            assert_equals(storage.getItem(""), "baz", "storage.getItem('')")
        }, name + ".getItem should be correct")
    }, "Get value by getIten(key) and named access in " + name + ".");
});
