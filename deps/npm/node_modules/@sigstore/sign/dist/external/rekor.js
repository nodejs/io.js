"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.Rekor = void 0;
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
/**
 * Rekor API client.
 */
class Rekor {
    constructor(options) {
        this.options = options;
    }
    /**
     * Create a new entry in the Rekor log.
     * @param propsedEntry {ProposedEntry} Data to create a new entry
     * @returns {Promise<Entry>} The created entry
     */
    async createEntry(propsedEntry) {
        const { baseURL, timeout, retry } = this.options;
        const url = `${baseURL}/api/v1/log/entries`;
        const response = await (0, fetch_1.fetchWithRetry)(url, {
            headers: {
                'Content-Type': 'application/json',
                Accept: 'application/json',
            },
            body: JSON.stringify(propsedEntry),
            timeout,
            retry,
        });
        const data = await response.json();
        return entryFromResponse(data);
    }
    /**
     * Get an entry from the Rekor log.
     * @param uuid {string} The UUID of the entry to retrieve
     * @returns {Promise<Entry>} The retrieved entry
     */
    async getEntry(uuid) {
        const { baseURL, timeout, retry } = this.options;
        const url = `${baseURL}/api/v1/log/entries/${uuid}`;
        const response = await (0, fetch_1.fetchWithRetry)(url, {
            method: 'GET',
            headers: {
                Accept: 'application/json',
            },
            timeout,
            retry,
        });
        const data = await response.json();
        return entryFromResponse(data);
    }
}
exports.Rekor = Rekor;
// Unpack the response from the Rekor API into a more convenient format.
function entryFromResponse(data) {
    const entries = Object.entries(data);
    if (entries.length != 1) {
        throw new Error('Received multiple entries in Rekor response');
    }
    // Grab UUID and entry data from the response
    const [uuid, entry] = entries[0];
    return {
        ...entry,
        uuid,
    };
}
