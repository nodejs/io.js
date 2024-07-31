"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.TimestampAuthority = void 0;
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
const fetch_1 = require("./fetch");
class TimestampAuthority {
    constructor(options) {
        this.options = options;
    }
    async createTimestamp(request) {
        const { baseURL, timeout, retry } = this.options;
        const url = `${baseURL}/api/v1/timestamp`;
        const response = await (0, fetch_1.fetchWithRetry)(url, {
            headers: {
                'Content-Type': 'application/json',
            },
            body: JSON.stringify(request),
            timeout,
            retry,
        });
        return response.buffer();
    }
}
exports.TimestampAuthority = TimestampAuthority;
