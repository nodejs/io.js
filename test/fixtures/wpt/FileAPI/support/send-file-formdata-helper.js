"use strict";

const kTestChars = "ABC~‾¥≈¤･・•∙·☼★星🌟星★☼·∙•・･¤≈¥‾~XYZ";

// formDataPostFileUploadTest - verifies multipart upload structure and
// numeric character reference replacement for filenames, field names,
// and field values using FormData and fetch().
//
// Uses /fetch/api/resources/echo-content.py to echo the upload
// POST (unlike in send-file-form-helper.js, here we expect all
// multipart/form-data request bodies to be UTF-8, so we don't need to
// escape controls and non-ASCII bytes).
//
// Fields in the parameter object:
//
// - fileNameSource: purely explanatory and gives a clue about which
//   character encoding is the source for the non-7-bit-ASCII parts of
//   the fileBaseName, or Unicode if no smaller-than-Unicode source
//   contains all the characters. Used in the test name.
// - fileBaseName: the not-necessarily-just-7-bit-ASCII file basename
//   used for the constructed test file. Used in the test name.
const formDataPostFileUploadTest = ({
  fileNameSource,
  fileBaseName,
}) => {
  promise_test(async (testCase) => {
    const formData = new FormData();
    let file = new Blob([kTestChars], { type: "text/plain" });
    try {
      // Switch to File in browsers that allow this
      file = new File([file], fileBaseName, { type: file.type });
    } catch (ignoredException) {
    }

    // Used to verify that the browser agrees with the test about
    // field value replacement and encoding independently of file system
    // idiosyncracies.
    formData.append("filename", fileBaseName);

    // Same, but with name and value reversed to ensure field names
    // get the same treatment.
    formData.append(fileBaseName, "filename");

    formData.append("file", file, fileBaseName);

    const formDataText = await (await fetch(
      `/fetch/api/resources/echo-content.py`,
      {
        method: "POST",
        body: formData,
      },
    )).text();
    const formDataLines = formDataText.split("\r\n");
    if (formDataLines.length && !formDataLines[formDataLines.length - 1]) {
      --formDataLines.length;
    }
    assert_greater_than(
      formDataLines.length,
      2,
      `${fileBaseName}: multipart form data must have at least 3 lines: ${
        JSON.stringify(formDataText)
      }`,
    );
    const boundary = formDataLines[0];
    assert_equals(
      formDataLines[formDataLines.length - 1],
      boundary + "--",
      `${fileBaseName}: multipart form data must end with ${boundary}--: ${
        JSON.stringify(formDataText)
      }`,
    );

    const asValue = fileBaseName.replace(/\r\n?|\n/g, "\r\n");
    const asName = asValue.replace(/[\r\n"]/g, encodeURIComponent);
    const asFilename = fileBaseName.replace(/[\r\n"]/g, encodeURIComponent);
    const expectedText = [
      boundary,
      'Content-Disposition: form-data; name="filename"',
      "",
      asValue,
      boundary,
      `Content-Disposition: form-data; name="${asName}"`,
      "",
      "filename",
      boundary,
      `Content-Disposition: form-data; name="file"; ` +
      `filename="${asFilename}"`,
      "Content-Type: text/plain",
      "",
      kTestChars,
      boundary + "--",
    ].join("\r\n");

    assert_true(
      formDataText.startsWith(expectedText),
      `Unexpected multipart-shaped form data received:\n${formDataText}\nExpected:\n${expectedText}`,
    );
  }, `Upload ${fileBaseName} (${fileNameSource}) in fetch with FormData`);
};
