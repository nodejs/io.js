// Copyright 2008 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// This file is a concatenation of the number-tostring and
// to-precision mjsunit tests where the mjsunit assert code has been
// removed.

// ----------------------------------------------------------------------
// toString
assertEquals('NaN', (NaN).toString());
assertEquals('Infinity', (1 / 0).toString());
assertEquals('-Infinity', (-1 / 0).toString());
assertEquals('0', (0).toString());
assertEquals('9', (9).toString());
assertEquals('90', (90).toString());
assertEquals('90.12', (90.12).toString());
assertEquals('0.1', (0.1).toString());
assertEquals('0.01', (0.01).toString());
assertEquals('0.0123', (0.0123).toString());
assertEquals('111111111111111110000', (111111111111111111111).toString());
assertEquals('1.1111111111111111e+21', (1111111111111111111111).toString());
assertEquals('1.1111111111111111e+22', (11111111111111111111111).toString());
assertEquals('0.00001', (0.00001).toString());
assertEquals('0.000001', (0.000001).toString());
assertEquals('1e-7', (0.0000001).toString());
assertEquals('1.2e-7', (0.00000012).toString());
assertEquals('1.23e-7', (0.000000123).toString());
assertEquals('1e-8', (0.00000001).toString());
assertEquals('1.2e-8', (0.000000012).toString());
assertEquals('1.23e-8', (0.0000000123).toString());

assertEquals('0', (-0).toString());
assertEquals('-9', (-9).toString());
assertEquals('-90', (-90).toString());
assertEquals('-90.12', (-90.12).toString());
assertEquals('-0.1', (-0.1).toString());
assertEquals('-0.01', (-0.01).toString());
assertEquals('-0.0123', (-0.0123).toString());
assertEquals('-111111111111111110000', (-111111111111111111111).toString());
assertEquals('-1.1111111111111111e+21', (-1111111111111111111111).toString());
assertEquals('-1.1111111111111111e+22', (-11111111111111111111111).toString());
assertEquals('-0.00001', (-0.00001).toString());
assertEquals('-0.000001', (-0.000001).toString());
assertEquals('-1e-7', (-0.0000001).toString());
assertEquals('-1.2e-7', (-0.00000012).toString());
assertEquals('-1.23e-7', (-0.000000123).toString());
assertEquals('-1e-8', (-0.00000001).toString());
assertEquals('-1.2e-8', (-0.000000012).toString());
assertEquals('-1.23e-8', (-0.0000000123).toString());

assertEquals('NaN', (NaN).toString(16));
assertEquals('Infinity', (1 / 0).toString(16));
assertEquals('-Infinity', (-1 / 0).toString(16));
assertEquals('0', (0).toString(16));
assertEquals('9', (9).toString(16));
assertEquals('5a', (90).toString(16));
assertEquals('5a.1eb851eb852', (90.12).toString(16));
assertEquals('0.1999999999999a', (0.1).toString(16));
assertEquals('0.028f5c28f5c28f6', (0.01).toString(16));
assertEquals('0.032617c1bda511a', (0.0123).toString(16));
assertEquals('605f9f6dd18bc8000', (111111111111111111111).toString(16));
assertEquals('3c3bc3a4a2f75c0000', (1111111111111111111111).toString(16));
assertEquals('25a55a46e5da9a00000', (11111111111111111111111).toString(16));
assertEquals('0.0000a7c5ac471b4788', (0.00001).toString(16));
assertEquals('0.000010c6f7a0b5ed8d', (0.000001).toString(16));
assertEquals('0.000001ad7f29abcaf48', (0.0000001).toString(16));
assertEquals('0.000002036565348d256', (0.00000012).toString(16));
assertEquals('0.0000021047ee22aa466', (0.000000123).toString(16));
assertEquals('0.0000002af31dc4611874', (0.00000001).toString(16));
assertEquals('0.000000338a23b87483be', (0.000000012).toString(16));
assertEquals('0.00000034d3fe36aaa0a2', (0.0000000123).toString(16));

assertEquals('0', (-0).toString(16));
assertEquals('-9', (-9).toString(16));
assertEquals('-5a', (-90).toString(16));
assertEquals('-5a.1eb851eb852', (-90.12).toString(16));
assertEquals('-0.1999999999999a', (-0.1).toString(16));
assertEquals('-0.028f5c28f5c28f6', (-0.01).toString(16));
assertEquals('-0.032617c1bda511a', (-0.0123).toString(16));
assertEquals('-605f9f6dd18bc8000', (-111111111111111111111).toString(16));
assertEquals('-3c3bc3a4a2f75c0000', (-1111111111111111111111).toString(16));
assertEquals('-25a55a46e5da9a00000', (-11111111111111111111111).toString(16));
assertEquals('-0.0000a7c5ac471b4788', (-0.00001).toString(16));
assertEquals('-0.000010c6f7a0b5ed8d', (-0.000001).toString(16));
assertEquals('-0.000001ad7f29abcaf48', (-0.0000001).toString(16));
assertEquals('-0.000002036565348d256', (-0.00000012).toString(16));
assertEquals('-0.0000021047ee22aa466', (-0.000000123).toString(16));
assertEquals('-0.0000002af31dc4611874', (-0.00000001).toString(16));
assertEquals('-0.000000338a23b87483be', (-0.000000012).toString(16));
assertEquals('-0.00000034d3fe36aaa0a2', (-0.0000000123).toString(16));

assertEquals('32', (2, 32).toString());
assertEquals('ffffffff', (Math.pow(2, 32) - 1).toString(16));
assertEquals(
    '11111111111111111111111111111111', (Math.pow(2, 32) - 1).toString(2));
assertEquals('5yc1z', (10000007).toString(36));
assertEquals('0', (0).toString(36));
assertEquals('0', (0).toString(16));
assertEquals('0', (0).toString(10));
assertEquals('0', (0).toString(8));
assertEquals('0', (0).toString(2));
assertEquals('100000', (2, 32).toString(2));
assertEquals(
    '100000000000000000000000000000001', (Math.pow(2, 32) + 1).toString(2));
assertEquals('100000000000080', (0x100000000000081).toString(16));
assertEquals('1000000000000100', (-(-'0x1000000000000081')).toString(16));
assertEquals(
    '100000000000000000000000000000000000000000000000010000000',
    (0x100000000000081).toString(2));
assertEquals(
    '-11111111111111111111111111111111', (-(Math.pow(2, 32) - 1)).toString(2));
assertEquals('-5yc1z', (-10000007).toString(36));
assertEquals(
    '-100000000000000000000000000000000', (-Math.pow(2, 32)).toString(2));
assertEquals(
    '-100000000000000000000000000000001', (-(Math.pow(2, 32) + 1)).toString(2));
assertEquals('-100000000000080', (-0x100000000000081).toString(16));
assertEquals(
    '-100000000000000000000000000000000000000000000000010000000',
    (-0x100000000000081).toString(2));
assertEquals('1000', (1000).toString());
assertEquals('0.00001', (0.00001).toString());
assertEquals('1000000000000000100', (1000000000000000128).toString());
assertEquals('1e+21', (1000000000000000012800).toString());
assertEquals('-1e+21', (-1000000000000000012800).toString());
assertEquals('1e-7', (0.0000001).toString());
assertEquals('-1e-7', (-0.0000001).toString());
assertEquals('1.0000000000000001e+21', (1000000000000000128000).toString());
assertEquals('0.000001', (0.000001).toString());
assertEquals('1e-7', (0.0000001).toString());
assertEquals('8.8', (8.5).toString(16));
assertEquals('-8.8', (-8.5).toString(16));

// ----------------------------------------------------------------------
// toFixed
assertEquals('NaN', (NaN).toFixed(2));
assertEquals('Infinity', (1 / 0).toFixed(2));
assertEquals('-Infinity', (-1 / 0).toFixed(2));

assertEquals('1.1111111111111111e+21', (1111111111111111111111).toFixed(8));
assertEquals('0.1', (0.1).toFixed(1));
assertEquals('0.10', (0.1).toFixed(2));
assertEquals('0.100', (0.1).toFixed(3));
assertEquals('0.01', (0.01).toFixed(2));
assertEquals('0.010', (0.01).toFixed(3));
assertEquals('0.0100', (0.01).toFixed(4));
assertEquals('0.00', (0.001).toFixed(2));
assertEquals('0.001', (0.001).toFixed(3));
assertEquals('0.0010', (0.001).toFixed(4));
assertEquals('1.0000', (1).toFixed(4));
assertEquals('1.0', (1).toFixed(1));
assertEquals('1', (1).toFixed(0));
assertEquals('12', (12).toFixed(0));
assertEquals('1', (1.1).toFixed(0));
assertEquals('12', (12.1).toFixed(0));
assertEquals('1', (1.12).toFixed(0));
assertEquals('12', (12.12).toFixed(0));
assertEquals('0.0000006', (0.0000006).toFixed(7));
assertEquals('0.00000006', (0.00000006).toFixed(8));
assertEquals('0.000000060', (0.00000006).toFixed(9));
assertEquals('0.0000000600', (0.00000006).toFixed(10));
assertEquals('0', (0).toFixed(0));
assertEquals('0.0', (0).toFixed(1));
assertEquals('0.00', (0).toFixed(2));

assertEquals('-1.1111111111111111e+21', (-1111111111111111111111).toFixed(8));
assertEquals('-0.1', (-0.1).toFixed(1));
assertEquals('-0.10', (-0.1).toFixed(2));
assertEquals('-0.100', (-0.1).toFixed(3));
assertEquals('-0.01', (-0.01).toFixed(2));
assertEquals('-0.010', (-0.01).toFixed(3));
assertEquals('-0.0100', (-0.01).toFixed(4));
assertEquals('-0.00', (-0.001).toFixed(2));
assertEquals('-0.001', (-0.001).toFixed(3));
assertEquals('-0.0010', (-0.001).toFixed(4));
assertEquals('-1.0000', (-1).toFixed(4));
assertEquals('-1.0', (-1).toFixed(1));
assertEquals('-1', (-1).toFixed(0));
assertEquals('-1', (-1.1).toFixed(0));
assertEquals('-12', (-12.1).toFixed(0));
assertEquals('-1', (-1.12).toFixed(0));
assertEquals('-12', (-12.12).toFixed(0));
assertEquals('-0.0000006', (-0.0000006).toFixed(7));
assertEquals('-0.00000006', (-0.00000006).toFixed(8));
assertEquals('-0.000000060', (-0.00000006).toFixed(9));
assertEquals('-0.0000000600', (-0.00000006).toFixed(10));
assertEquals('0', (-0).toFixed(0));
assertEquals('0.0', (-0).toFixed(1));
assertEquals('0.00', (-0).toFixed(2));

assertEquals('1000', (1000).toFixed());
assertEquals('0', (0.00001).toFixed());
assertEquals('0.00001', (0.00001).toFixed(5));
assertEquals('0.00000000000000000010', (0.0000000000000000001).toFixed(20));
assertEquals('0.00001000000000000', (0.00001).toFixed(17));
assertEquals('1.00000000000000000', (1).toFixed(17));
assertEquals('1000000000000000128', (1000000000000000128).toFixed());
assertEquals('100000000000000128.0', (100000000000000128).toFixed(1));
assertEquals('10000000000000128.00', (10000000000000128).toFixed(2));
assertEquals(
    '10000000000000128.00000000000000000000', (10000000000000128).toFixed(20));
assertEquals('0', (0).toFixed());
((-42).toFixed(3));
assertEquals('-1000000000000000128', (-1000000000000000128).toFixed());
assertEquals('-0.00000000000000000010', (-0.0000000000000000001).toFixed(20));
assertEquals('0.12312312312312299889', (0.123123123123123).toFixed(20));
// Test that we round up even when the last digit generated is even.
// dtoa does not do this in its original form.
assertEquals('1', (0.5).toFixed(0));
assertEquals('-1', (-0.5).toFixed(0));
assertEquals('1.3', (1.25).toFixed(1));
// This is bizare, but Spidermonkey and KJS behave the same.
assertEquals('234.2040', (234.20405).toFixed(4));
assertEquals('234.2041', (234.2040506).toFixed(4));

// ----------------------------------------------------------------------
// toExponential
assertEquals('1e+0', (1).toExponential());
assertEquals('1.1e+1', (11).toExponential());
assertEquals('1.12e+2', (112).toExponential());
assertEquals('1e+0', (1).toExponential(0));
assertEquals('1e+1', (11).toExponential(0));
assertEquals('1e+2', (112).toExponential(0));
assertEquals('1.0e+0', (1).toExponential(1));
assertEquals('1.1e+1', (11).toExponential(1));
assertEquals('1.1e+2', (112).toExponential(1));
assertEquals('1.00e+0', (1).toExponential(2));
assertEquals('1.10e+1', (11).toExponential(2));
assertEquals('1.12e+2', (112).toExponential(2));
assertEquals('1.000e+0', (1).toExponential(3));
assertEquals('1.100e+1', (11).toExponential(3));
assertEquals('1.120e+2', (112).toExponential(3));
assertEquals('1e-1', (0.1).toExponential());
assertEquals('1.1e-1', (0.11).toExponential());
assertEquals('1.12e-1', (0.112).toExponential());
assertEquals('1e-1', (0.1).toExponential(0));
assertEquals('1e-1', (0.11).toExponential(0));
assertEquals('1e-1', (0.112).toExponential(0));
assertEquals('1.0e-1', (0.1).toExponential(1));
assertEquals('1.1e-1', (0.11).toExponential(1));
assertEquals('1.1e-1', (0.112).toExponential(1));
assertEquals('1.00e-1', (0.1).toExponential(2));
assertEquals('1.10e-1', (0.11).toExponential(2));
assertEquals('1.12e-1', (0.112).toExponential(2));
assertEquals('1.000e-1', (0.1).toExponential(3));
assertEquals('1.100e-1', (0.11).toExponential(3));
assertEquals('1.120e-1', (0.112).toExponential(3));

assertEquals('-1e+0', (-1).toExponential());
assertEquals('-1.1e+1', (-11).toExponential());
assertEquals('-1.12e+2', (-112).toExponential());
assertEquals('-1e+0', (-1).toExponential(0));
assertEquals('-1e+1', (-11).toExponential(0));
assertEquals('-1e+2', (-112).toExponential(0));
assertEquals('-1.0e+0', (-1).toExponential(1));
assertEquals('-1.1e+1', (-11).toExponential(1));
assertEquals('-1.1e+2', (-112).toExponential(1));
assertEquals('-1.00e+0', (-1).toExponential(2));
assertEquals('-1.10e+1', (-11).toExponential(2));
assertEquals('-1.12e+2', (-112).toExponential(2));
assertEquals('-1.000e+0', (-1).toExponential(3));
assertEquals('-1.100e+1', (-11).toExponential(3));
assertEquals('-1.120e+2', (-112).toExponential(3));
assertEquals('-1e-1', (-0.1).toExponential());
assertEquals('-1.1e-1', (-0.11).toExponential());
assertEquals('-1.12e-1', (-0.112).toExponential());
assertEquals('-1e-1', (-0.1).toExponential(0));
assertEquals('-1e-1', (-0.11).toExponential(0));
assertEquals('-1e-1', (-0.112).toExponential(0));
assertEquals('-1.0e-1', (-0.1).toExponential(1));
assertEquals('-1.1e-1', (-0.11).toExponential(1));
assertEquals('-1.1e-1', (-0.112).toExponential(1));
assertEquals('-1.00e-1', (-0.1).toExponential(2));
assertEquals('-1.10e-1', (-0.11).toExponential(2));
assertEquals('-1.12e-1', (-0.112).toExponential(2));
assertEquals('-1.000e-1', (-0.1).toExponential(3));
assertEquals('-1.100e-1', (-0.11).toExponential(3));
assertEquals('-1.120e-1', (-0.112).toExponential(3));

assertEquals('NaN', (NaN).toExponential(2));
assertEquals('Infinity', (Infinity).toExponential(2));
assertEquals('-Infinity', (-Infinity).toExponential(2));
assertEquals('1e+0', (1).toExponential(0));
assertEquals('0e+0', (0).toExponential());
assertEquals('0.00e+0', (0).toExponential(2));
assertEquals('1e+1', (11.2356).toExponential(0));
assertEquals('1.1236e+1', (11.2356).toExponential(4));
assertEquals('1.1236e-4', (0.000112356).toExponential(4));
assertEquals('-1.1236e-4', (-0.000112356).toExponential(4));
assertEquals('1.12356e-4', (0.000112356).toExponential());
assertEquals('-1.12356e-4', (-0.000112356).toExponential());

// ----------------------------------------------------------------------
// toPrecision
assertEquals('NaN', (NaN).toPrecision(1));
assertEquals('Infinity', (Infinity).toPrecision(2));
assertEquals('-Infinity', (-Infinity).toPrecision(2));
assertEquals('0.000555000000000000', (0.000555).toPrecision(15));
assertEquals('5.55000000000000e-7', (0.000000555).toPrecision(15));
assertEquals('-5.55000000000000e-7', (-0.000000555).toPrecision(15));
assertEquals('1e+8', (123456789).toPrecision(1));
assertEquals('123456789', (123456789).toPrecision(9));
assertEquals('1.2345679e+8', (123456789).toPrecision(8));
assertEquals('1.234568e+8', (123456789).toPrecision(7));
assertEquals('-1.234568e+8', (-123456789).toPrecision(7));
assertEquals('-1.2e-9', (-.0000000012345).toPrecision(2));
assertEquals('-1.2e-8', (-.000000012345).toPrecision(2));
assertEquals('-1.2e-7', (-.00000012345).toPrecision(2));
assertEquals('-0.0000012', (-.0000012345).toPrecision(2));
assertEquals('-0.000012', (-.000012345).toPrecision(2));
assertEquals('-0.00012', (-.00012345).toPrecision(2));
assertEquals('-0.0012', (-.0012345).toPrecision(2));
assertEquals('-0.012', (-.012345).toPrecision(2));
assertEquals('-0.12', (-.12345).toPrecision(2));
assertEquals('-1.2', (-1.2345).toPrecision(2));
assertEquals('-12', (-12.345).toPrecision(2));
assertEquals('-1.2e+2', (-123.45).toPrecision(2));
assertEquals('-1.2e+3', (-1234.5).toPrecision(2));
assertEquals('-1.2e+4', (-12345).toPrecision(2));
assertEquals('-1.235e+4', (-12345.67).toPrecision(4));
assertEquals('-1.234e+4', Number(-12344.67).toPrecision(4));
// Test that we round up even when the last digit generated is even.
// dtoa does not do this in its original form.
assertEquals('1.3', (1.25).toPrecision(2));
assertEquals('1.4', (1.35).toPrecision(2));

// Test the exponential notation output.
assertEquals('1e+27', (1.2345e+27).toPrecision(1));
assertEquals('1.2e+27', (1.2345e+27).toPrecision(2));
assertEquals('1.23e+27', (1.2345e+27).toPrecision(3));
assertEquals('1.234e+27', (1.2345e+27).toPrecision(4));
assertEquals('1.2345e+27', (1.2345e+27).toPrecision(5));
assertEquals('1.23450e+27', (1.2345e+27).toPrecision(6));
assertEquals('1.234500e+27', (1.2345e+27).toPrecision(7));

assertEquals('-1e+27', (-1.2345e+27).toPrecision(1));
assertEquals('-1.2e+27', (-1.2345e+27).toPrecision(2));
assertEquals('-1.23e+27', (-1.2345e+27).toPrecision(3));
assertEquals('-1.234e+27', (-1.2345e+27).toPrecision(4));
assertEquals('-1.2345e+27', (-1.2345e+27).toPrecision(5));
assertEquals('-1.23450e+27', (-1.2345e+27).toPrecision(6));
assertEquals('-1.234500e+27', (-1.2345e+27).toPrecision(7));

// Test the fixed notation output.
assertEquals('7', (7).toPrecision(1));
assertEquals('7.0', (7).toPrecision(2));
assertEquals('7.00', (7).toPrecision(3));

assertEquals('-7', (-7).toPrecision(1));
assertEquals('-7.0', (-7).toPrecision(2));
assertEquals('-7.00', (-7).toPrecision(3));

assertEquals('9e+1', (91).toPrecision(1));
assertEquals('91', (91).toPrecision(2));
assertEquals('91.0', (91).toPrecision(3));
assertEquals('91.00', (91).toPrecision(4));

assertEquals('-9e+1', (-91).toPrecision(1));
assertEquals('-91', (-91).toPrecision(2));
assertEquals('-91.0', (-91).toPrecision(3));
assertEquals('-91.00', (-91).toPrecision(4));

assertEquals('9e+1', (91.1234).toPrecision(1));
assertEquals('91', (91.1234).toPrecision(2));
assertEquals('91.1', (91.1234).toPrecision(3));
assertEquals('91.12', (91.1234).toPrecision(4));
assertEquals('91.123', (91.1234).toPrecision(5));
assertEquals('91.1234', (91.1234).toPrecision(6));
assertEquals('91.12340', (91.1234).toPrecision(7));
assertEquals('91.123400', (91.1234).toPrecision(8));

assertEquals('-9e+1', (-91.1234).toPrecision(1));
assertEquals('-91', (-91.1234).toPrecision(2));
assertEquals('-91.1', (-91.1234).toPrecision(3));
assertEquals('-91.12', (-91.1234).toPrecision(4));
assertEquals('-91.123', (-91.1234).toPrecision(5));
assertEquals('-91.1234', (-91.1234).toPrecision(6));
assertEquals('-91.12340', (-91.1234).toPrecision(7));
assertEquals('-91.123400', (-91.1234).toPrecision(8));
