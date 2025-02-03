test(() => {
    const event = new StorageEvent('storage');
    assert_throws_js(TypeError, () => event.initStorageEvent());
    // should be redundant, but .length can be wrong with custom bindings
    assert_equals(event.initStorageEvent.length, 1, 'event.initStorageEvent.length');
}, 'initStorageEvent with 0 arguments');

test(() => {
    const event = new StorageEvent('storage');
    event.initStorageEvent('type');
    assert_equals(event.type, 'type', 'event.type');
    assert_equals(event.bubbles, false, 'event.bubbles');
    assert_equals(event.cancelable, false, 'event.cancelable');
    assert_equals(event.key, null, 'event.key');
    assert_equals(event.oldValue, null, 'event.oldValue');
    assert_equals(event.newValue, null, 'event.newValue');
    assert_equals(event.url, '', 'event.url');
    assert_equals(event.storageArea, null, 'event.storageArea');
}, 'initStorageEvent with 1 argument');

test(() => {
    assert_not_equals(localStorage, null, 'localStorage'); // precondition

    const event = new StorageEvent('storage');
    event.initStorageEvent('type', true, true, 'key', 'oldValue', 'newValue', 'url', localStorage);
    assert_equals(event.type, 'type', 'event.type');
    assert_equals(event.bubbles, true, 'event.bubbles');
    assert_equals(event.cancelable, true, 'event.cancelable');
    assert_equals(event.key, 'key', 'event.key');
    assert_equals(event.oldValue, 'oldValue', 'event.oldValue');
    assert_equals(event.newValue, 'newValue', 'event.newValue');
    assert_equals(event.url, 'url', 'event.url');
    assert_equals(event.storageArea, localStorage, 'event.storageArea');
}, 'initStorageEvent with 8 sensible arguments');

test(() => {
    const event = new StorageEvent('storage');
    event.initStorageEvent(null, null, null, null, null, null, null, null);
    assert_equals(event.type, 'null', 'event.type');
    assert_equals(event.bubbles, false, 'event.bubbles');
    assert_equals(event.cancelable, false, 'event.cancelable');
    assert_equals(event.key, null, 'event.key');
    assert_equals(event.oldValue, null, 'event.oldValue');
    assert_equals(event.newValue, null, 'event.newValue');
    assert_equals(event.url, 'null', 'event.url');
    assert_equals(event.storageArea, null, 'event.storageArea');
}, 'initStorageEvent with 8 null arguments');

test(() => {
    const event = new StorageEvent('storage');
    event.initStorageEvent(undefined, undefined, undefined, undefined, undefined, undefined, undefined, undefined);
    assert_equals(event.type, 'undefined', 'event.type');
    assert_equals(event.bubbles, false, 'event.bubbles');
    assert_equals(event.cancelable, false, 'event.cancelable');
    assert_equals(event.key, null, 'event.key');
    assert_equals(event.oldValue, null, 'event.oldValue');
    assert_equals(event.newValue, null, 'event.newValue');
    assert_equals(event.url, '', 'event.url');
    assert_equals(event.storageArea, null, 'event.storageArea');
}, 'initStorageEvent with 8 undefined arguments');
