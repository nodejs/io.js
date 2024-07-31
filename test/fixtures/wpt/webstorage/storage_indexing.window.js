["localStorage", "sessionStorage"].forEach(function(name) {
    test(function() {
        var storage = window[name];
        storage.clear();
        storage["name"] = "user1";
        storage["age"] = "42";

        test(function() {
            assert_equals(storage[-1], undefined);
            assert_equals(storage[0], undefined);
            assert_equals(storage[1], undefined);
            assert_equals(storage[2], undefined);
        }, "Getting number properties on " + name);

        test(function() {
            assert_equals(storage["-1"], undefined);
            assert_equals(storage["0"], undefined);
            assert_equals(storage["1"], undefined);
            assert_equals(storage["2"], undefined);
        }, "Getting number-valued string properties on " + name);

        test(function() {
            storage.setItem(1, "number");
            assert_equals(storage[1], "number");
            assert_equals(storage["1"], "number");
        }, "Getting existing number-valued properties on " + name);
    }, "Indexed getter on " + name);
});
