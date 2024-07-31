testStorages(function(storageString) {
    async_test(function(t) {
        assert_true(storageString in window, storageString + " exist");
        var storage = window[storageString];
        t.add_cleanup(function() { storage.clear() });

        clearStorage(storageString, t.step_func(step0));
        assert_equals(storage.length, 0, "storage.length");

        function step0(msg)
        {
            iframe.onload = t.step_func(step1);
            // Null out the existing handler eventTestHarness.js set up;
            // otherwise this test won't be testing much of anything useful.
            iframe.contentWindow.onstorage = null;
            iframe.src = "resources/event_setattribute_handler.html";
        }

        function step1(msg)
        {
            storage.setItem('FOO', 'BAR');

            runAfterNStorageEvents(t.step_func(step2), 1);
        }

        function step2(msg)
        {
            if(msg != undefined) {
                assert_unreached(msg);
            }
            assert_equals(storageEventList.length, 1);
            assert_equals(storageEventList[0].key, "FOO");
            assert_equals(storageEventList[0].oldValue, null);
            assert_equals(storageEventList[0].newValue, "BAR");

            storage.setItem('FU', 'BAR');
            storage.setItem('a', '1');
            storage.setItem('b', '2');
            storage.setItem('b', '3');

            runAfterNStorageEvents(t.step_func(step3), 5);
        }

        function step3(msg)
        {
            if(msg != undefined) {
                assert_unreached(msg);
            }
            assert_equals(storageEventList.length, 5);
            assert_equals(storageEventList[1].key, "FU");
            assert_equals(storageEventList[1].oldValue, null);
            assert_equals(storageEventList[1].newValue, "BAR");

            assert_equals(storageEventList[2].key, "a");
            assert_equals(storageEventList[2].oldValue, null);
            assert_equals(storageEventList[2].newValue, "1");

            assert_equals(storageEventList[3].key, "b");
            assert_equals(storageEventList[3].oldValue, null);
            assert_equals(storageEventList[3].newValue, "2");

            assert_equals(storageEventList[4].key, "b");
            assert_equals(storageEventList[4].oldValue, "2");
            assert_equals(storageEventList[4].newValue, "3");

            storage.removeItem('FOO');

            runAfterNStorageEvents(t.step_func(step4), 6);
        }

        function step4(msg)
        {
            if(msg != undefined) {
                assert_unreached(msg);
            }
            assert_equals(storageEventList.length, 6);
            assert_equals(storageEventList[5].key, "FOO");
            assert_equals(storageEventList[5].oldValue, "BAR");
            assert_equals(storageEventList[5].newValue, null);

            storage.removeItem('FU');

            runAfterNStorageEvents(t.step_func(step5), 7);
        }

        function step5(msg)
        {
            if(msg != undefined) {
                assert_unreached(msg);
            }
            assert_equals(storageEventList.length, 7);
            assert_equals(storageEventList[6].key, "FU");
            assert_equals(storageEventList[6].oldValue, "BAR");
            assert_equals(storageEventList[6].newValue, null);

            storage.clear();

            runAfterNStorageEvents(t.step_func(step6), 8);
        }

        function step6(msg)
        {
            if(msg != undefined) {
                assert_unreached(msg);
            }
            assert_equals(storageEventList.length, 8);
            assert_equals(storageEventList[7].key, null);
            assert_equals(storageEventList[7].oldValue, null);
            assert_equals(storageEventList[7].newValue, null);

            t.done();
        }

    }, storageString + " mutations fire StorageEvents that are caught by the event listener attached via setattribute.");
});
