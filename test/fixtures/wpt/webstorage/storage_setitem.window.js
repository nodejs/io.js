["localStorage", "sessionStorage"].forEach(function(name) {
    var test_error = { name: "test" };
    var interesting_strs = ["\uD7FF", "\uD800", "\uDBFF", "\uDC00",
                            "\uDFFF", "\uE000", "\uFFFD", "\uFFFE", "\uFFFF",
                            "\uD83C\uDF4D", "\uD83Ca", "a\uDF4D",
                            "\uDBFF\uDFFF"];

    for (var i = 0; i <= 0xFF; i++) {
        interesting_strs.push(String.fromCharCode(i));
    }

    test(function() {
        var storage = window[name];
        storage.clear();

        storage.setItem("name", "user1");
        assert_equals(storage.length, 1, "localStorage.setItem")
    }, name + ".setItem()");

    test(function() {
        var storage = window[name];
        storage.clear();

        storage["name"] = "user1";
        assert_true("name" in storage);
        assert_equals(storage.length, 1, "storage.length")
        assert_equals(storage.getItem("name"), "user1");
        assert_equals(storage["name"], "user1");
    }, name + "[]");

    test(function() {
        var storage = window[name];
        storage.clear();

        storage["name"] = "user1";
        storage["name"] = "user2";
        assert_true("name" in storage);
        assert_equals(storage.length, 1, "storage.length")
        assert_equals(storage.getItem("name"), "user2");
        assert_equals(storage["name"], "user2");
    }, name + "[] update");

    test(function() {
        var storage = window[name];
        storage.clear();

        storage.setItem("age", null);
        assert_true("age" in storage);
        assert_equals(storage.length, 1, "storage.length")
        assert_equals(storage.getItem("age"), "null");
        assert_equals(storage["age"], "null");
    }, name + ".setItem(_, null)");

    test(function() {
        var storage = window[name];
        storage.clear();

        storage["age"] = null;
        assert_true("age" in storage);
        assert_equals(storage.length, 1, "storage.length")
        assert_equals(storage.getItem("age"), "null");
        assert_equals(storage["age"], "null");
    }, name + "[] = null");

    test(function() {
        var storage = window[name];
        storage.clear();

        storage.setItem("age", undefined);
        assert_true("age" in storage);
        assert_equals(storage.length, 1, "storage.length")
        assert_equals(storage.getItem("age"), "undefined");
        assert_equals(storage["age"], "undefined");
    }, name + ".setItem(_, undefined)");

    test(function() {
        var storage = window[name];
        storage.clear();

        storage["age"] = undefined;
        assert_true("age" in storage);
        assert_equals(storage.length, 1, "storage.length")
        assert_equals(storage.getItem("age"), "undefined");
        assert_equals(storage["age"], "undefined");
    }, name + "[] = undefined");

    test(function() {
        var storage = window[name];
        storage.clear();

        storage.setItem("age", "10");
        assert_throws_exactly(test_error, function() {
            storage.setItem("age",
                { toString: function() { throw test_error; } });
        });
        assert_true("age" in storage);
        assert_equals(storage.length, 1, "storage.length")
        assert_equals(storage.getItem("age"), "10");
        assert_equals(storage["age"], "10");
    }, name + ".setItem({ throws })");

    test(function() {
        var storage = window[name];
        storage.clear();

        storage.setItem("age", "10");
        assert_throws_exactly(test_error, function() {
            storage["age"] =
                { toString: function() { throw test_error; } };
        });
        assert_true("age" in storage);
        assert_equals(storage.length, 1, "storage.length")
        assert_equals(storage.getItem("age"), "10");
        assert_equals(storage["age"], "10");
    }, name + "[] = { throws }");

    test(function() {
        var storage = window[name];
        storage.clear();

        storage.setItem(undefined, "test");
        assert_true("undefined" in storage);
        assert_equals(storage.length, 1, "storage.length")
        assert_equals(storage.getItem("undefined"), "test");
        assert_equals(storage["undefined"], "test");
    }, name + ".setItem(undefined, _)");

    test(function() {
        var storage = window[name];
        storage.clear();

        storage[undefined] = "test2";
        assert_true("undefined" in storage);
        assert_equals(storage.length, 1, "storage.length")
        assert_equals(storage.getItem("undefined"), "test2");
        assert_equals(storage["undefined"], "test2");
    }, name + "[undefined]");

    test(function() {
        var storage = window[name];
        storage.clear();

        storage.setItem(null, "test");
        assert_true("null" in storage);
        assert_equals(storage.length, 1, "storage.length")
        assert_equals(storage.getItem("null"), "test");
        assert_equals(storage["null"], "test");
    }, name + ".setItem(null, _)");

    test(function() {
        var storage = window[name];
        storage.clear();

        storage[null] = "test2";
        assert_true("null" in storage);
        assert_equals(storage.length, 1, "storage.length")
        assert_equals(storage.getItem("null"), "test2");
        assert_equals(storage["null"], "test2");
    }, name + "[null]");

    test(function() {
        var storage = window[name];
        storage.clear();

        storage["foo\0bar"] = "user1";
        assert_true("foo\0bar" in storage);
        assert_false("foo\0" in storage);
        assert_false("foo\0baz" in storage);
        assert_false("foo" in storage);
        assert_equals(storage.length, 1, "storage.length")
        assert_equals(storage.getItem("foo\0bar"), "user1");
        assert_equals(storage.getItem("foo\0"), null);
        assert_equals(storage.getItem("foo\0baz"), null);
        assert_equals(storage.getItem("foo"), null);
        assert_equals(storage["foo\0bar"], "user1");
        assert_equals(storage["foo\0"], undefined);
        assert_equals(storage["foo\0baz"], undefined);
        assert_equals(storage["foo"], undefined);
    }, name + " key containing null");

    test(function() {
        var storage = window[name];
        storage.clear();

        storage["name"] = "foo\0bar";
        assert_true("name" in storage);
        assert_equals(storage.length, 1, "storage.length")
        assert_equals(storage.getItem("name"), "foo\0bar");
        assert_equals(storage["name"], "foo\0bar");
    }, name + " value containing null");

    for (i = 0; i < interesting_strs.length; i++) {
        var str = interesting_strs[i];
        test(function() {
            var storage = window[name];
            storage.clear();

            storage[str] = "user1";
            assert_true(str in storage);
            assert_equals(storage.length, 1, "storage.length")
            assert_equals(storage.getItem(str), "user1");
            assert_equals(storage[str], "user1");
        }, name + "[" + format_value(str) + "]");

        test(function() {
            var storage = window[name];
            storage.clear();

            storage["name"] = str;
            assert_equals(storage.length, 1, "storage.length")
            assert_equals(storage.getItem("name"), str);
            assert_equals(storage["name"], str);
        }, name + "[] = " + format_value(str));
    }
});
