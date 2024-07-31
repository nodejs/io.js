"use strict";
var __importDefault = (this && this.__importDefault) || function (mod) {
    return (mod && mod.__esModule) ? mod : { "default": mod };
};
Object.defineProperty(exports, "__esModule", { value: true });
exports.fetchWithRetry = void 0;
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
const http2_1 = require("http2");
const make_fetch_happen_1 = __importDefault(require("make-fetch-happen"));
const proc_log_1 = require("proc-log");
const promise_retry_1 = __importDefault(require("promise-retry"));
const util_1 = require("../util");
const error_1 = require("./error");
const { HTTP2_HEADER_LOCATION, HTTP2_HEADER_CONTENT_TYPE, HTTP2_HEADER_USER_AGENT, HTTP_STATUS_INTERNAL_SERVER_ERROR, HTTP_STATUS_TOO_MANY_REQUESTS, HTTP_STATUS_REQUEST_TIMEOUT, } = http2_1.constants;
async function fetchWithRetry(url, options) {
    return (0, promise_retry_1.default)(async (retry, attemptNum) => {
        const method = options.method || 'POST';
        const headers = {
            [HTTP2_HEADER_USER_AGENT]: util_1.ua.getUserAgent(),
            ...options.headers,
        };
        const response = await (0, make_fetch_happen_1.default)(url, {
            method,
            headers,
            body: options.body,
            timeout: options.timeout,
            retry: false, // We're handling retries ourselves
        }).catch((reason) => {
            proc_log_1.log.http('fetch', `${method} ${url} attempt ${attemptNum} failed with ${reason}`);
            return retry(reason);
        });
        if (response.ok) {
            return response;
        }
        else {
            const error = await errorFromResponse(response);
            proc_log_1.log.http('fetch', `${method} ${url} attempt ${attemptNum} failed with ${response.status}`);
            if (retryable(response.status)) {
                return retry(error);
            }
            else {
                throw error;
            }
        }
    }, retryOpts(options.retry));
}
exports.fetchWithRetry = fetchWithRetry;
// Translate a Response into an HTTPError instance. This will attempt to parse
// the response body for a message, but will default to the statusText if none
// is found.
const errorFromResponse = async (response) => {
    let message = response.statusText;
    const location = response.headers?.get(HTTP2_HEADER_LOCATION) || undefined;
    const contentType = response.headers?.get(HTTP2_HEADER_CONTENT_TYPE);
    // If response type is JSON, try to parse the body for a message
    if (contentType?.includes('application/json')) {
        try {
            const body = await response.json();
            message = body.message || message;
        }
        catch (e) {
            // ignore
        }
    }
    return new error_1.HTTPError({
        status: response.status,
        message: message,
        location: location,
    });
};
// Determine if a status code is retryable. This includes 5xx errors, 408, and
// 429.
const retryable = (status) => [HTTP_STATUS_REQUEST_TIMEOUT, HTTP_STATUS_TOO_MANY_REQUESTS].includes(status) || status >= HTTP_STATUS_INTERNAL_SERVER_ERROR;
// Normalize the retry options to the format expected by promise-retry
const retryOpts = (retry) => {
    if (typeof retry === 'boolean') {
        return { retries: retry ? 1 : 0 };
    }
    else if (typeof retry === 'number') {
        return { retries: retry };
    }
    else {
        return { retries: 0, ...retry };
    }
};
