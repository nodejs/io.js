var store_list = [
  ["key0", "value0"],
  ["key1", "value1"],
  ["key2", "value2"]
];
["localStorage", "sessionStorage"].forEach(function(name) {
  test(function () {
    var storage = window[name];
    storage.clear();

    store_list.forEach(function(item) {
      storage.setItem(item[0], item[1]);
    });

    for (var i = 0; i < store_list.length; i++) {
        var value = storage.getItem("key" + i);
        assert_equals(value, "value" + i);
    }
  }, "enumerate a " + name + " object with the key and get the values");
});

