test(function() {
    assert_throws_js(
        TypeError,
        () => StorageEvent(""),
        "Calling StorageEvent constructor without 'new' must throw"
    );
}, "StorageEvent constructor called as normal function");

test(function() {
    assert_throws_js(TypeError, () => new StorageEvent());
    // should be redundant, but .length can be wrong with custom bindings
    assert_equals(StorageEvent.length, 1, 'StorageEvent.length');
}, 'constructor with no arguments');

test(function() {
    var event = new StorageEvent('type');
    assert_equals(event.type, 'type', 'type');
    assert_equals(event.key, null, 'key');
    assert_equals(event.oldValue, null, 'oldValue');
    assert_equals(event.newValue, null, 'newValue');
    assert_equals(event.url, '', 'url');
    assert_equals(event.storageArea, null, 'storageArea');
}, 'constructor with just type argument');

test(function() {
    assert_not_equals(localStorage, null, 'localStorage'); // precondition

    var event = new StorageEvent('storage', {
        bubbles: true,
        cancelable: true,
        key: 'key',
        oldValue: 'oldValue',
        newValue: 'newValue',
        url: 'url', // not an absolute URL to ensure it isn't resolved
        storageArea: localStorage
    });
    assert_equals(event.type, 'storage', 'type');
    assert_equals(event.bubbles, true, 'bubbles');
    assert_equals(event.cancelable, true, 'cancelable');
    assert_equals(event.key, 'key', 'key');
    assert_equals(event.oldValue, 'oldValue', 'oldValue');
    assert_equals(event.newValue, 'newValue', 'newValue');
    assert_equals(event.url, 'url', 'url');
    assert_equals(event.storageArea, localStorage, 'storageArea');
}, 'constructor with sensible type argument and members');

test(function() {
    var event = new StorageEvent(null, {
        key: null,
        oldValue: null,
        newValue: null,
        url: null,
        storageArea: null
    });
    assert_equals(event.type, 'null', 'type');
    assert_equals(event.key, null, 'key');
    assert_equals(event.oldValue, null, 'oldValue');
    assert_equals(event.newValue, null, 'newValue');
    assert_equals(event.url, 'null', 'url');
    assert_equals(event.storageArea, null, 'storageArea');
}, 'constructor with null type argument and members');

test(function() {
    var event = new StorageEvent(undefined, {
        key: undefined,
        oldValue: undefined,
        newValue: undefined,
        url: undefined,
        storageArea: undefined
    });
    assert_equals(event.type, 'undefined', 'type');
    assert_equals(event.key, null, 'key');
    assert_equals(event.oldValue, null, 'oldValue');
    assert_equals(event.newValue, null, 'newValue');
    assert_equals(event.url, '', 'url'); // not 'undefined'!
    assert_equals(event.storageArea, null, 'storageArea');
}, 'constructor with undefined type argument and members');
