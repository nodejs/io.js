test(function() {
  var popup = window.open("", "sessionStorageTestWindow");

  sessionStorage.setItem("FOO", "BAR");

  var reopened = window.open("", "sessionStorageTestWindow");

  assert_equals(
    popup,
    reopened,
    "window.open with the same name should re-open the same window"
  );

  assert_equals(
    sessionStorage.getItem("FOO"),
    "BAR",
    "local sessionStorage is correct"
  );
  assert_equals(
    popup.sessionStorage.getItem("FOO"),
    null,
    "popup sessionStorage is correct"
  );

  popup.close();
}, "ensure that re-opening a named window doesn't copy sessionStorage");
