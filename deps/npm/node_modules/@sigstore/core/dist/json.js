"use strict";
/*
Copyright 2023 The Sigstore Authors.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/
Object.defineProperty(exports, "__esModule", { value: true });
exports.canonicalize = void 0;
// JSON canonicalization per https://github.com/cyberphone/json-canonicalization
// eslint-disable-next-line @typescript-eslint/no-explicit-any
function canonicalize(object) {
    let buffer = '';
    if (object === null || typeof object !== 'object' || object.toJSON != null) {
        // Primitives or toJSONable objects
        buffer += JSON.stringify(object);
    }
    else if (Array.isArray(object)) {
        // Array - maintain element order
        buffer += '[';
        let first = true;
        object.forEach((element) => {
            if (!first) {
                buffer += ',';
            }
            first = false;
            // recursive call
            buffer += canonicalize(element);
        });
        buffer += ']';
    }
    else {
        // Object - Sort properties before serializing
        buffer += '{';
        let first = true;
        Object.keys(object)
            .sort()
            .forEach((property) => {
            if (!first) {
                buffer += ',';
            }
            first = false;
            buffer += JSON.stringify(property);
            buffer += ':';
            // recursive call
            buffer += canonicalize(object[property]);
        });
        buffer += '}';
    }
    return buffer;
}
exports.canonicalize = canonicalize;
