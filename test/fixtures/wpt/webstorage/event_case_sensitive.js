testStorages(function(storageString) {
    async_test(function(t) {
        assert_true(storageString in window, storageString + " exist");
        var storage = window[storageString];
        t.add_cleanup(function() { storage.clear() });

        clearStorage(storageString, t.step_func(loadiframe));
        assert_equals(storage.length, 0, "storage.length");

        function loadiframe(msg)
        {
            iframe.onload = t.step_func(step0);
            iframe.src = "resources/event_basic.html";
        }

        function step0(msg)
        {
            storage.foo = "test";
            runAfterNStorageEvents(t.step_func(step1), 1);
        }

        function step1(msg)
        {
            storageEventList = new Array();
            storage.foo = "test";

            runAfterNStorageEvents(t.step_func(step2), 0);
        }

        function step2(msg)
        {
            if(msg != undefined) {
                assert_unreached(msg);
            }
            assert_equals(storageEventList.length, 0);

            storage.foo = "TEST";

            runAfterNStorageEvents(t.step_func(step3), 1);
        }

        function step3(msg)
        {
            if(msg != undefined) {
                assert_unreached(msg);
            }
            assert_equals(storageEventList.length, 1);

            t.done();
        }
    }, storageString + " storage events fire even when only the case of the value changes.");
});
