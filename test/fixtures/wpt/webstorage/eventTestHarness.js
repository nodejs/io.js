storageEventList = [];
iframe = document.createElement("IFRAME");
document.body.appendChild(iframe);

function runAfterNStorageEvents(callback, expectedNumEvents)
{
    countStorageEvents(callback, expectedNumEvents, 0)
}

function countStorageEvents(callback, expectedNumEvents, times)
{
    function onTimeout()
    {
        var currentCount = storageEventList.length;
        if (currentCount == expectedNumEvents) {
            callback();
        } else if (currentCount > expectedNumEvents) {
            msg = "got at least " + currentCount + ", expected only " + expectedNumEvents + " events";
            callback(msg);
        } else if (times > 50) {
            msg = "Timeout: only got " + currentCount + ", expected " + expectedNumEvents + " events";
            callback(msg);
        } else {
            countStorageEvents(callback, expectedNumEvents, times+1);
        }
    }
    setTimeout(onTimeout, 20);
}

function clearStorage(storageName, callback)
{
    if (window[storageName].length === 0) {
        storageEventList = [];
        setTimeout(callback, 0);
    } else {
        window[storageName].clear();
        runAfterNStorageEvents(function() {
            storageEventList = [];
            callback();
        }, 1);
    }
}

function testStorages(testCallback)
{
    testCallback("sessionStorage");
    var hit = false;
    add_result_callback(function() {
        if (!hit) {
            hit = true;
            testCallback("localStorage");
        }
    });
}
