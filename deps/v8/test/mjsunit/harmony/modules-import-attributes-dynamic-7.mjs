// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --harmony-import-attributes

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
import('modules-skip-1.json', { with: null}).then(
    () => assertUnreachable('Should have failed due to bad with object'),
    error => result4 = error.message);
import('modules-skip-1.json', { with: 7}).then(
    () => assertUnreachable('Should have failed due to bad with object'),
    error => result5 = error.message);
import('modules-skip-1.json', { with: 'string'}).then(
    () => assertUnreachable('Should have failed due to bad with object'),
    error => result6 = error.message);
import('modules-skip-1.json', { with: { a: null }}).then(
    () => assertUnreachable('Should have failed due to bad with object'),
    error => result7 = error.message);
import('modules-skip-1.json', { with: { a: undefined }}).then(
    () => assertUnreachable('Should have failed due to bad assertion value'),
    error => result8 = error.message);
import('modules-skip-1.json', { with: { a: 7 }}).then(
    () => assertUnreachable('Should have failed due to bad assertion value'),
    error => result9 = error.message);
    import('modules-skip-1.json', { with: { a: { } }}).then(
        () => assertUnreachable('Should have failed due to bad assertion value'),
        error => result10 = error.message);

%PerformMicrotaskCheckpoint();

const argumentNotObjectError = 'The second argument to import() must be an object';
const attributesOptionNotObjectError = 'The \'assert\' option must be an object';
const attributeValueNotStringError = 'Import assertion value must be a string';

assertEquals(argumentNotObjectError, result1);
assertEquals(argumentNotObjectError, result2);
assertEquals(argumentNotObjectError, result3);
assertEquals(attributesOptionNotObjectError, result4);
assertEquals(attributesOptionNotObjectError, result5);
assertEquals(attributesOptionNotObjectError, result6);
assertEquals(attributeValueNotStringError, result7);
assertEquals(attributeValueNotStringError, result8);
assertEquals(attributeValueNotStringError, result9);
assertEquals(attributeValueNotStringError, result10);
