["localStorage", "sessionStorage"].forEach(function(name) {
    test(function () {
      var storage = window[name];
      storage.clear();

      storage.setItem("", "empty string");
      assert_equals(storage.getItem(""), "empty string");

  }, name + ".key with empty string");
});
