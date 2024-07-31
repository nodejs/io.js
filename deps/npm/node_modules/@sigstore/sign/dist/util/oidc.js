"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.extractJWTSubject = void 0;
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
const core_1 = require("@sigstore/core");
function extractJWTSubject(jwt) {
    const parts = jwt.split('.', 3);
    const payload = JSON.parse(core_1.encoding.base64Decode(parts[1]));
    switch (payload.iss) {
        case 'https://accounts.google.com':
        case 'https://oauth2.sigstore.dev/auth':
            return payload.email;
        default:
            return payload.sub;
    }
}
exports.extractJWTSubject = extractJWTSubject;
