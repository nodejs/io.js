["localStorage", "sessionStorage"].forEach(function(name) {
    test(function() {
        var storage = window[name];
        storage.clear();
        assert_equals(storage.length, 0, "storage.length");

        var builtins = ["key", "getItem", "setItem", "removeItem", "clear"];
        var origBuiltins = builtins.map(function(b) { return Storage.prototype[b]; });
        assert_array_equals(builtins.map(function(b) { return storage[b]; }), origBuiltins, "a");
        builtins.forEach(function(b) { storage[b] = b; });
        assert_array_equals(builtins.map(function(b) { return storage[b]; }), origBuiltins, "b");
        assert_array_equals(builtins.map(function(b) { return storage.getItem(b); }), builtins, "c");

        assert_equals(storage.length, builtins.length, "storage.length");
    }, "Builtins in " + name);
});
