// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Flags: --harmony-intl-duration-format

let h1 = {hours: 1};
let m2 = {minutes: 2};
let s3 = {seconds: 3};
let ms45 = {milliseconds: 45};

let df = new Intl.DurationFormat("en", {style: "digital"});
assertEquals("1:00:00", df.format(h1));
assertEquals(
    '[{"type":"integer","value":"1","unit":"hour"},'+
     '{"type":"literal","value":":"},'+
     '{"type":"integer","value":"00","unit":"minute"},'+
     '{"type":"literal","value":":"},'+
     '{"type":"integer","value":"00","unit":"second"}]',
     JSON.stringify(df.formatToParts(h1)));
assertEquals("0:02:00", df.format(m2));
assertEquals(
    '[{"type":"integer","value":"0","unit":"hour"},'+
     '{"type":"literal","value":":"},'+
     '{"type":"integer","value":"02","unit":"minute"},'+
     '{"type":"literal","value":":"},'+
     '{"type":"integer","value":"00","unit":"second"}]',
     JSON.stringify(df.formatToParts(m2)));
assertEquals("0:00:03", df.format(s3));
assertEquals(
    '[{"type":"integer","value":"0","unit":"hour"},'+
     '{"type":"literal","value":":"},'+
     '{"type":"integer","value":"00","unit":"minute"},'+
     '{"type":"literal","value":":"},'+
     '{"type":"integer","value":"03","unit":"second"}]',
     JSON.stringify(df.formatToParts(s3)));
assertEquals("0:00:00.045", df.format(ms45));
assertEquals(
    '[{"type":"integer","value":"0","unit":"hour"},'+
     '{"type":"literal","value":":"},'+
     '{"type":"integer","value":"00","unit":"minute"},'+
     '{"type":"literal","value":":"},'+
     '{"type":"integer","value":"00","unit":"second"},' +
     '{"type":"decimal","value":".","unit":"second"},' +
     '{"type":"fraction","value":"045","unit":"second"}]',
     JSON.stringify(df.formatToParts(ms45)));

// With 4 fractional digits
let dffd4 = new Intl.DurationFormat("en", {style: "digital", fractionalDigits: 4});
assertEquals("0:00:00.0450", dffd4.format(ms45));
assertEquals(
    '[{"type":"integer","value":"0","unit":"hour"},'+
     '{"type":"literal","value":":"},'+
     '{"type":"integer","value":"00","unit":"minute"},'+
     '{"type":"literal","value":":"},'+
     '{"type":"integer","value":"00","unit":"second"},'+
     '{"type":"decimal","value":".","unit":"second"},'+
     '{"type":"fraction","value":"0450","unit":"second"}]',
     JSON.stringify(dffd4.formatToParts(ms45)));

// Danish locale use . as separator
let dadf = new Intl.DurationFormat("da", {style: "digital"});
assertEquals("1.00.00", dadf.format(h1));
assertEquals(
    '[{"type":"integer","value":"1","unit":"hour"},'+
     '{"type":"literal","value":"."},'+
     '{"type":"integer","value":"00","unit":"minute"},'+
     '{"type":"literal","value":"."},'+
     '{"type":"integer","value":"00","unit":"second"}]',
     JSON.stringify(dadf.formatToParts(h1)));
assertEquals("0.02.00", dadf.format(m2));
assertEquals(
    '[{"type":"integer","value":"0","unit":"hour"},'+
     '{"type":"literal","value":"."},'+
     '{"type":"integer","value":"02","unit":"minute"},'+
     '{"type":"literal","value":"."},'+
     '{"type":"integer","value":"00","unit":"second"}]',
     JSON.stringify(dadf.formatToParts(m2)));
assertEquals("0.00.03", dadf.format(s3));
assertEquals(
    '[{"type":"integer","value":"0","unit":"hour"},'+
     '{"type":"literal","value":"."},'+
     '{"type":"integer","value":"00","unit":"minute"},'+
     '{"type":"literal","value":"."},'+
     '{"type":"integer","value":"03","unit":"second"}]',
     JSON.stringify(dadf.formatToParts(s3)));

let dadffd4 = new Intl.DurationFormat("da", {style: "digital", fractionalDigits: 4});
assertEquals("0.00.00,0450", dadffd4.format(ms45));
assertEquals(
    '[{"type":"integer","value":"0","unit":"hour"},'+
     '{"type":"literal","value":"."},'+
     '{"type":"integer","value":"00","unit":"minute"},'+
     '{"type":"literal","value":"."},'+
     '{"type":"integer","value":"00","unit":"second"},'+
     '{"type":"decimal","value":",","unit":"second"},'+
     '{"type":"fraction","value":"0450","unit":"second"}]',
     JSON.stringify(dadffd4.formatToParts(ms45)));

let autodf = new Intl.DurationFormat("en", {style: "digital", hoursDisplay: "auto", minutesDisplay: "auto", secondsDisplay: "auto"});
assertEquals("1", autodf.format(h1));
assertEquals("02", autodf.format(m2));
assertEquals("03", autodf.format(s3));

let autodf2 = new Intl.DurationFormat("en", {style: "digital", hoursDisplay: "auto", secondsDisplay: "auto"});
assertEquals("1:00", autodf2.format(h1));
assertEquals("02", autodf2.format(m2));
assertEquals("00:03", autodf2.format(s3));
