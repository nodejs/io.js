["localStorage", "sessionStorage"].forEach(function(name) {
    test(function() {
        var storage = window[name];
        storage.clear();

        storage.setItem("name", "user1");
        storage.setItem("age", "20");
        storage.setItem("a", "1");
        storage.setItem("b", "2");

        var keys = ["name", "age", "a", "b"];
        function doTest(index) {
            test(function() {
                var key = storage.key(index);
                assert_not_equals(key, null);
                assert_true(keys.indexOf(key) >= 0,
                            "Unexpected key " + key + " found.");
            }, name + ".key(" + index + ") should return the right thing.");
        }
        for (var i = 0; i < keys.length; ++i) {
            doTest(i);
            doTest(i + 0x100000000);
        }

        test(function() {
            assert_equals(storage.key(-1), null, "storage.key(-1)");
            assert_equals(storage.key(4), null, "storage.key(4)");
        }, name + ".key() should return null for out-of-range arguments.");
    }, name + ".key");

    test(function() {
        var get_keys = function(s) {
            var keys = [];
            for (var i = 0; i < s.length; ++i) {
                keys.push(s.key(i));
            }
            return keys;
        };
        var storage = window[name];
        storage.clear();

        storage.setItem("name", "user1");
        storage.setItem("age", "20");
        storage.setItem("a", "1");
        storage.setItem("b", "2");

        var expected_keys = get_keys(storage);
        storage.setItem("name", "user2");
        assert_array_equals(get_keys(storage), expected_keys);
    }, name + ".key with value changes");
});
