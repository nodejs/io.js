const verifyEntries = (entries, filterOptions) => {
  for (const filterOption of filterOptions) {
    let countBeforeFiltering = entries.length;

    // Using negate of the condition so that the next filtering is applied on less entries.
    entries = entries.filter(
      e => !(e.entryType == filterOption['entryType'] && e.name.includes(filterOption['name'])));

    assert_equals(
      countBeforeFiltering - entries.length, filterOption['expectedCount'], filterOption['failureMsg']);
  }
}

const createFilterOption = (name, entryType, expectedCount, msgPrefix, description = '') => {
  if (description) {
    description = ' ' + description;
  }

  let failureMsg =
    `${msgPrefix} should have ${expectedCount} ${entryType} entries for name ${name}` + description;

  return {
    name: name,
    entryType: entryType,
    expectedCount: expectedCount,
    failureMsg: failureMsg
  };
}

const loadChildFrame = (src) => {
  return new Promise(resolve => {

    const childFrame = document.createElement('iframe');

    childFrame.addEventListener("load", resolve);

    childFrame.src = src;

    document.body.appendChild(childFrame);
  });
}

const loadChildFrameAndGrandchildFrame = (src) => {
  return new Promise(resolve => {

    const crossOriginChildFrame = document.createElement('iframe');

    // Wait for the child frame to send a message. The child frame would send a message
    // when it loads its child frame.
    window.addEventListener('message', e => {
      if (e.data == 'Load completed') {
        resolve();
      }
    });

    crossOriginChildFrame.src = src;

    document.body.appendChild(crossOriginChildFrame)
  });
}
