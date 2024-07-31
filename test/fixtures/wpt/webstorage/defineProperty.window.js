["localStorage", "sessionStorage"].forEach(function(name) {
    [9, "x"].forEach(function(key) {
        test(function() {
            var desc = {
                value: "value",
            };

            var storage = window[name];
            storage.clear();

            assert_equals(storage[key], undefined);
            assert_equals(storage.getItem(key), null);
            assert_equals(Object.defineProperty(storage, key, desc), storage);
            assert_equals(storage[key], "value");
            assert_equals(storage.getItem(key), "value");
        }, "Defining data property for key " + key + " on " + name);

        test(function() {
            var desc1 = {
                value: "value",
            };
            var desc2 = {
                value: "new value",
            };

            var storage = window[name];
            storage.clear();

            assert_equals(storage[key], undefined);
            assert_equals(storage.getItem(key), null);
            assert_equals(Object.defineProperty(storage, key, desc1), storage);
            assert_equals(storage[key], "value");
            assert_equals(storage.getItem(key), "value");

            assert_equals(Object.defineProperty(storage, key, desc2), storage);
            assert_equals(storage[key], "new value");
            assert_equals(storage.getItem(key), "new value");
        }, "Defining data property for key " + key + " on " + name + " twice");

        test(function() {
            var desc = {
                value: {
                    toString: function() { return "value"; }
                },
            };

            var storage = window[name];
            storage.clear();

            assert_equals(storage[key], undefined);
            assert_equals(storage.getItem(key), null);
            assert_equals(Object.defineProperty(storage, key, desc), storage);
            assert_equals(storage[key], "value");
            assert_equals(storage.getItem(key), "value");
        }, "Defining data property with toString for key " + key + " on " + name);
    });
});
