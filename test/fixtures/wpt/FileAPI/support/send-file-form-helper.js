'use strict';

// See /FileAPI/file/resources/echo-content-escaped.py
function escapeString(string) {
  return string.replace(/\\/g, "\\\\").replace(
    /[^\x20-\x7E]/g,
    (x) => {
      let hex = x.charCodeAt(0).toString(16);
      if (hex.length < 2) hex = "0" + hex;
      return `\\x${hex}`;
    },
  ).replace(/\\x0d\\x0a/g, "\r\n");
}

// Rationale for this particular test character sequence, which is
// used in filenames and also in file contents:
//
// - ABC~ ensures the string starts with something we can read to
//   ensure it is from the correct source; ~ is used because even
//   some 1-byte otherwise-ASCII-like parts of ISO-2022-JP
//   interpret it differently.
// - ‾¥ are inside a single-byte range of ISO-2022-JP and help
//   diagnose problems due to filesystem encoding or locale
// - ≈ is inside IBM437 and helps diagnose problems due to filesystem
//   encoding or locale
// - ¤ is inside Latin-1 and helps diagnose problems due to
//   filesystem encoding or locale; it is also the "simplest" case
//   needing substitution in ISO-2022-JP
// - ･ is inside a single-byte range of ISO-2022-JP in some variants
//   and helps diagnose problems due to filesystem encoding or locale;
//   on the web it is distinct when decoding but unified when encoding
// - ・ is inside a double-byte range of ISO-2022-JP and helps
//   diagnose problems due to filesystem encoding or locale
// - • is inside Windows-1252 and helps diagnose problems due to
//   filesystem encoding or locale and also ensures these aren't
//   accidentally turned into e.g. control codes
// - ∙ is inside IBM437 and helps diagnose problems due to filesystem
//   encoding or locale
// - · is inside Latin-1 and helps diagnose problems due to
//   filesystem encoding or locale and also ensures HTML named
//   character references (e.g. &middot;) are not used
// - ☼ is inside IBM437 shadowing C0 and helps diagnose problems due to
//   filesystem encoding or locale and also ensures these aren't
//   accidentally turned into e.g. control codes
// - ★ is inside ISO-2022-JP on a non-Kanji page and makes correct
//   output easier to spot
// - 星 is inside ISO-2022-JP on a Kanji page and makes correct
//   output easier to spot
// - 🌟 is outside the BMP and makes incorrect surrogate pair
//   substitution detectable and ensures substitutions work
//   correctly immediately after Kanji 2-byte ISO-2022-JP
// - 星 repeated here ensures the correct codec state is used
//   after a non-BMP substitution
// - ★ repeated here also makes correct output easier to spot
// - ☼ is inside IBM437 shadowing C0 and helps diagnose problems due to
//   filesystem encoding or locale and also ensures these aren't
//   accidentally turned into e.g. control codes and also ensures
//   substitutions work correctly immediately after non-Kanji
//   2-byte ISO-2022-JP
// - · is inside Latin-1 and helps diagnose problems due to
//   filesystem encoding or locale and also ensures HTML named
//   character references (e.g. &middot;) are not used
// - ∙ is inside IBM437 and helps diagnose problems due to filesystem
//   encoding or locale
// - • is inside Windows-1252 and again helps diagnose problems
//   due to filesystem encoding or locale
// - ・ is inside a double-byte range of ISO-2022-JP and helps
//   diagnose problems due to filesystem encoding or locale
// - ･ is inside a single-byte range of ISO-2022-JP in some variants
//   and helps diagnose problems due to filesystem encoding or locale;
//   on the web it is distinct when decoding but unified when encoding
// - ¤ is inside Latin-1 and helps diagnose problems due to
//   filesystem encoding or locale; again it is a "simple"
//   substitution case
// - ≈ is inside IBM437 and helps diagnose problems due to filesystem
//   encoding or locale
// - ¥‾ are inside a single-byte range of ISO-2022-JP and help
//   diagnose problems due to filesystem encoding or locale
// - ~XYZ ensures earlier errors don't lead to misencoding of
//   simple ASCII
//
// Overall the near-symmetry makes common I18N mistakes like
// off-by-1-after-non-BMP easier to spot. All the characters
// are also allowed in Windows Unicode filenames.
const kTestChars = 'ABC~‾¥≈¤･・•∙·☼★星🌟星★☼·∙•・･¤≈¥‾~XYZ';

// The kTestFallback* strings represent the expected byte sequence from
// encoding kTestChars with the given encoding with "html" replacement
// mode, isomorphic-decoded. That means, characters that can't be
// encoded in that encoding get HTML-escaped, but no further
// `escapeString`-like escapes are needed.
const kTestFallbackUtf8 = (
  "ABC~\xE2\x80\xBE\xC2\xA5\xE2\x89\x88\xC2\xA4\xEF\xBD\xA5\xE3\x83\xBB\xE2" +
    "\x80\xA2\xE2\x88\x99\xC2\xB7\xE2\x98\xBC\xE2\x98\x85\xE6\x98\x9F\xF0\x9F" +
    "\x8C\x9F\xE6\x98\x9F\xE2\x98\x85\xE2\x98\xBC\xC2\xB7\xE2\x88\x99\xE2\x80" +
    "\xA2\xE3\x83\xBB\xEF\xBD\xA5\xC2\xA4\xE2\x89\x88\xC2\xA5\xE2\x80\xBE~XYZ"
);

const kTestFallbackIso2022jp = (
  ("ABC~\x1B(J~\\≈¤\x1B$B!&!&\x1B(B•∙·☼\x1B$B!z@1\x1B(B🌟" +
    "\x1B$B@1!z\x1B(B☼·∙•\x1B$B!&!&\x1B(B¤≈\x1B(J\\~\x1B(B~XYZ")
    .replace(/[^\0-\x7F]/gu, (x) => `&#${x.codePointAt(0)};`)
);

const kTestFallbackWindows1252 = (
  "ABC~‾\xA5≈\xA4･・\x95∙\xB7☼★星🌟星★☼\xB7∙\x95・･\xA4≈\xA5‾~XYZ".replace(
    /[^\0-\xFF]/gu,
    (x) => `&#${x.codePointAt(0)};`,
  )
);

const kTestFallbackXUserDefined = kTestChars.replace(
  /[^\0-\x7F]/gu,
  (x) => `&#${x.codePointAt(0)};`,
);

// formPostFileUploadTest - verifies multipart upload structure and
// numeric character reference replacement for filenames, field names,
// and field values using form submission.
//
// Uses /FileAPI/file/resources/echo-content-escaped.py to echo the
// upload POST with controls and non-ASCII bytes escaped. This is done
// because navigations whose response body contains [\0\b\v] may get
// treated as a download, which is not what we want. Use the
// `escapeString` function to replicate that kind of escape (note that
// it takes an isomorphic-decoded string, not a byte sequence).
//
// Fields in the parameter object:
//
// - fileNameSource: purely explanatory and gives a clue about which
//   character encoding is the source for the non-7-bit-ASCII parts of
//   the fileBaseName, or Unicode if no smaller-than-Unicode source
//   contains all the characters. Used in the test name.
// - fileBaseName: the not-necessarily-just-7-bit-ASCII file basename
//   used for the constructed test file. Used in the test name.
// - formEncoding: the acceptCharset of the form used to submit the
//   test file. Used in the test name.
// - expectedEncodedBaseName: the expected formEncoding-encoded
//   version of fileBaseName, isomorphic-decoded. That means, characters
//   that can't be encoded in that encoding get HTML-escaped, but no
//   further `escapeString`-like escapes are needed.
const formPostFileUploadTest = ({
  fileNameSource,
  fileBaseName,
  formEncoding,
  expectedEncodedBaseName,
}) => {
  promise_test(async testCase => {

    if (document.readyState !== 'complete') {
      await new Promise(resolve => addEventListener('load', resolve));
    }

    const formTargetFrame = Object.assign(document.createElement('iframe'), {
      name: 'formtargetframe',
    });
    document.body.append(formTargetFrame);
    testCase.add_cleanup(() => {
      document.body.removeChild(formTargetFrame);
    });

    const form = Object.assign(document.createElement('form'), {
      acceptCharset: formEncoding,
      action: '/FileAPI/file/resources/echo-content-escaped.py',
      method: 'POST',
      enctype: 'multipart/form-data',
      target: formTargetFrame.name,
    });
    document.body.append(form);
    testCase.add_cleanup(() => {
      document.body.removeChild(form);
    });

    // Used to verify that the browser agrees with the test about
    // which form charset is used.
    form.append(Object.assign(document.createElement('input'), {
      type: 'hidden',
      name: '_charset_',
    }));

    // Used to verify that the browser agrees with the test about
    // field value replacement and encoding independently of file system
    // idiosyncrasies.
    form.append(Object.assign(document.createElement('input'), {
      type: 'hidden',
      name: 'filename',
      value: fileBaseName,
    }));

    // Same, but with name and value reversed to ensure field names
    // get the same treatment.
    form.append(Object.assign(document.createElement('input'), {
      type: 'hidden',
      name: fileBaseName,
      value: 'filename',
    }));

    const fileInput = Object.assign(document.createElement('input'), {
      type: 'file',
      name: 'file',
    });
    form.append(fileInput);

    // Removes c:\fakepath\ or other pseudofolder and returns just the
    // final component of filePath; allows both / and \ as segment
    // delimiters.
    const baseNameOfFilePath = filePath => filePath.split(/[\/\\]/).pop();
    await new Promise(resolve => {
      const dataTransfer = new DataTransfer;
      dataTransfer.items.add(
          new File([kTestChars], fileBaseName, {type: 'text/plain'}));
      fileInput.files = dataTransfer.files;
      // For historical reasons .value will be prefixed with
      // c:\fakepath\, but the basename should match the file name
      // exposed through the newer .files[0].name API. This check
      // verifies that assumption.
      assert_equals(
          baseNameOfFilePath(fileInput.files[0].name),
          baseNameOfFilePath(fileInput.value),
          `The basename of the field's value should match its files[0].name`);
      form.submit();
      formTargetFrame.onload = resolve;
    });

    const formDataText = formTargetFrame.contentDocument.body.textContent;
    const formDataLines = formDataText.split('\n');
    if (formDataLines.length && !formDataLines[formDataLines.length - 1]) {
      --formDataLines.length;
    }
    assert_greater_than(
        formDataLines.length,
        2,
        `${fileBaseName}: multipart form data must have at least 3 lines: ${
             JSON.stringify(formDataText)
           }`);
    const boundary = formDataLines[0];
    assert_equals(
        formDataLines[formDataLines.length - 1],
        boundary + '--',
        `${fileBaseName}: multipart form data must end with ${boundary}--: ${
             JSON.stringify(formDataText)
           }`);

    const asValue = expectedEncodedBaseName.replace(/\r\n?|\n/g, "\r\n");
    const asName = asValue.replace(/[\r\n"]/g, encodeURIComponent);
    const asFilename = expectedEncodedBaseName.replace(/[\r\n"]/g, encodeURIComponent);

    // The response body from echo-content-escaped.py has controls and non-ASCII
    // bytes escaped, so any caller-provided field that might contain such bytes
    // must be passed to `escapeString`, after any other expected
    // transformations.
    const expectedText = [
      boundary,
      'Content-Disposition: form-data; name="_charset_"',
      '',
      formEncoding,
      boundary,
      'Content-Disposition: form-data; name="filename"',
      '',
      // Unlike for names and filenames, multipart/form-data values don't escape
      // \r\n linebreaks, and when they're read from an iframe they become \n.
      escapeString(asValue).replace(/\r\n/g, "\n"),
      boundary,
      `Content-Disposition: form-data; name="${escapeString(asName)}"`,
      '',
      'filename',
      boundary,
      `Content-Disposition: form-data; name="file"; ` +
          `filename="${escapeString(asFilename)}"`,
      'Content-Type: text/plain',
      '',
      escapeString(kTestFallbackUtf8),
      boundary + '--',
    ].join('\n');

    assert_true(
        formDataText.startsWith(expectedText),
        `Unexpected multipart-shaped form data received:\n${
             formDataText
           }\nExpected:\n${expectedText}`);
  }, `Upload ${fileBaseName} (${fileNameSource}) in ${formEncoding} form`);
};
