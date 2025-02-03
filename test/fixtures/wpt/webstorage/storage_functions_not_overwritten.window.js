["localStorage", "sessionStorage"].forEach(function(name) {
    test(function() {
        var storage = window[name];
        storage.clear();

        runTest();
        function doWedgeThySelf() {
            storage.setItem("clear", "almost");
            storage.setItem("key", "too");
            storage.setItem("getItem", "funny");
            storage.setItem("removeItem", "to");
            storage.setItem("length", "be");
            storage.setItem("setItem", "true");
        }

        function runTest() {
            doWedgeThySelf();

            assert_equals(storage.getItem('clear'), "almost");
            assert_equals(storage.getItem('key'), "too");
            assert_equals(storage.getItem('getItem'), "funny");
            assert_equals(storage.getItem('removeItem'), "to");
            assert_equals(storage.getItem('length'), "be");
            assert_equals(storage.getItem('setItem'), "true");

            // Test to see if an exception is thrown for any of the built in
            // functions.
            storage.setItem("test", "123");
            storage.key(0);
            storage.getItem("test");
            storage.removeItem("test");
            storage.clear();
            assert_equals(storage.length, 0);
        }

    }, name + " should be not rendered unusable by setting a key with the same name as a storage function such that the function is hidden");
});
