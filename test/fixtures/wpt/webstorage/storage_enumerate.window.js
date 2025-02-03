["localStorage", "sessionStorage"].forEach(function(name) {
    test(function() {
        assert_true(name in window, name + " exist");

        var storage = window[name];
        storage.clear();

        Storage.prototype.prototypeTestKey = "prototypeTestValue";
        storage.foo = "bar";
        storage.fu = "baz";
        storage.batman = "bin suparman";
        storage.bar = "foo";
        storage.alpha = "beta";
        storage.zeta = "gamma";

        const enumeratedArray = Object.keys(storage);
        enumeratedArray.sort();  // Storage order is implementation-defined.

        const expectArray = ["alpha", "bar", "batman", "foo", "fu", "zeta"];
        assert_array_equals(enumeratedArray, expectArray);

        // 'prototypeTestKey' is not an actual storage key, it is just a
        // property set on Storage's prototype object.
        assert_equals(storage.length, 6);
        assert_equals(storage.getItem("prototypeTestKey"), null);
        assert_equals(storage.prototypeTestKey, "prototypeTestValue");
    }, name + ": enumerate a Storage object and get only the keys as a result and the built-in properties of the Storage object should be ignored");

    test(function() {
        const storage = window[name];
        storage.clear();

        storage.setItem("foo", "bar");
        storage.baz = "quux";
        storage.setItem(0, "alpha");
        storage[42] = "beta";

        for (let prop in storage) {
            if (!storage.hasOwnProperty(prop))
                continue;
            const desc = Object.getOwnPropertyDescriptor(storage, prop);
            assert_true(desc.configurable);
            assert_true(desc.enumerable);
            assert_true(desc.writable);
        }

        const keys = Object.keys(storage);
        keys.sort();  // Storage order is implementation-defined.
        assert_array_equals(keys, ["0", "42", "baz", "foo"]);

        const values = Object.values(storage);
        values.sort();  // Storage order is implementation-defined.
        assert_array_equals(values, ["alpha", "bar", "beta", "quux"]);
    }, name + ": test enumeration of numeric and non-numeric keys");
});
