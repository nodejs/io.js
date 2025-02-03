["localStorage", "sessionStorage"].forEach(function(name) {
    [9, "x"].forEach(function(key) {
        test(function() {
            var expected = "value for " + this.name;
            var value = expected;

            var storage = window[name];
            storage.clear();

            assert_equals(storage[key], undefined);
            assert_equals(storage.getItem(key), null);
            assert_equals(storage[key] = value, value);
            assert_equals(storage[key], expected);
            assert_equals(storage.getItem(key), expected);
        }, "Setting property for key " + key + " on " + name);

        test(function() {
            var expected = "value for " + this.name;
            var value = {
                toString: function() { return expected; }
            };

            var storage = window[name];
            storage.clear();

            assert_equals(storage[key], undefined);
            assert_equals(storage.getItem(key), null);
            assert_equals(storage[key] = value, value);
            assert_equals(storage[key], expected);
            assert_equals(storage.getItem(key), expected);
        }, "Setting property with toString for key " + key + " on " + name);

        test(function() {
            var proto = "proto for " + this.name;
            Storage.prototype[key] = proto;
            this.add_cleanup(function() { delete Storage.prototype[key]; });

            var value = "value for " + this.name;

            var storage = window[name];
            storage.clear();

            assert_equals(storage[key], proto);
            assert_equals(storage.getItem(key), null);
            assert_equals(storage[key] = value, value);
            // Hidden because no [LegacyOverrideBuiltIns].
            assert_equals(storage[key], proto);
            assert_equals(Object.getOwnPropertyDescriptor(storage, key), undefined);
            assert_equals(storage.getItem(key), value);
        }, "Setting property for key " + key + " on " + name + " with data property on prototype");

        test(function() {
            var proto = "proto for " + this.name;
            Storage.prototype[key] = proto;
            this.add_cleanup(function() { delete Storage.prototype[key]; });

            var value = "value for " + this.name;
            var existing = "existing for " + this.name;

            var storage = window[name];
            storage.clear();

            storage.setItem(key, existing);

            // Hidden because no [LegacyOverrideBuiltIns].
            assert_equals(storage[key], proto);
            assert_equals(Object.getOwnPropertyDescriptor(storage, key), undefined);
            assert_equals(storage.getItem(key), existing);
            assert_equals(storage[key] = value, value);
            assert_equals(storage[key], proto);
            assert_equals(Object.getOwnPropertyDescriptor(storage, key), undefined);
            assert_equals(storage.getItem(key), value);
        }, "Setting property for key " + key + " on " + name + " with data property on prototype and existing item");

        test(function() {
            var storage = window[name];
            storage.clear();

            var proto = "proto getter for " + this.name;
            Object.defineProperty(Storage.prototype, key, {
                "get": function() { return proto; },
                "set": this.unreached_func("Should not call [[Set]] on prototype"),
                "configurable": true,
            });
            this.add_cleanup(function() {
                delete Storage.prototype[key];
                delete storage[key];
                assert_false(key in storage);
            });

            var value = "value for " + this.name;

            assert_equals(storage[key], proto);
            assert_equals(storage.getItem(key), null);
            assert_equals(storage[key] = value, value);
            // Property is hidden because no [LegacyOverrideBuiltIns].
            assert_equals(storage[key], proto);
            assert_equals(Object.getOwnPropertyDescriptor(storage, key), undefined);
            assert_equals(storage.getItem(key), value);
        }, "Setting property for key " + key + " on " + name + " with accessor property on prototype");
    });
});
