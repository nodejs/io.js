// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --harmony-import-assertions

var result1;
var result2;
var result3;
var result4;
var result5;
var result6;
var result7;
var result8;
var result9;
var result10;
import('modules-skip-1.json', null).then(
    () => assertUnreachable('Should have failed due to non-object parameter'),
    error => result1 = error.message);
import('modules-skip-1.json', 7).then(
    () => assertUnreachable('Should have failed due to non-object parameter'),
    error => result2 = error.message);
import('modules-skip-1.json', 'string').then(
        () => assertUnreachable('Should have failed due to non-object parameter'),
        error => result3 = error.message);
import('modules-skip-1.json', { assert: null}).then(
    () => assertUnreachable('Should have failed due to bad assert object'),
    error => result4 = error.message);
import('modules-skip-1.json', { assert: 7}).then(
    () => assertUnreachable('Should have failed due to bad assert object'),
    error => result5 = error.message);
import('modules-skip-1.json', { assert: 'string'}).then(
    () => assertUnreachable('Should have failed due to bad assert object'),
    error => result6 = error.message);
import('modules-skip-1.json', { assert: { a: null }}).then(
    () => assertUnreachable('Should have failed due to bad assert object'),
    error => result7 = error.message);
import('modules-skip-1.json', { assert: { a: undefined }}).then(
    () => assertUnreachable('Should have failed due to bad assertion value'),
    error => result8 = error.message);
import('modules-skip-1.json', { assert: { a: 7 }}).then(
    () => assertUnreachable('Should have failed due to bad assertion value'),
    error => result9 = error.message);
    import('modules-skip-1.json', { assert: { a: { } }}).then(
        () => assertUnreachable('Should have failed due to bad assertion value'),
        error => result10 = error.message);

%PerformMicrotaskCheckpoint();

const argumentNotObjectError = 'The second argument to import() must be an object';
const assertOptionNotObjectError = 'The \'assert\' option must be an object';
const attributeValueNotStringError = 'Import assertion value must be a string';

assertEquals(argumentNotObjectError, result1);
assertEquals(argumentNotObjectError, result2);
assertEquals(argumentNotObjectError, result3);
assertEquals(assertOptionNotObjectError, result4);
assertEquals(assertOptionNotObjectError, result5);
assertEquals(assertOptionNotObjectError, result6);
assertEquals(attributeValueNotStringError, result7);
assertEquals(attributeValueNotStringError, result8);
assertEquals(attributeValueNotStringError, result9);
assertEquals(attributeValueNotStringError, result10);
