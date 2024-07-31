"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.ByteStream = void 0;
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
class StreamError extends Error {
}
class ByteStream {
    constructor(buffer) {
        this.start = 0;
        if (buffer) {
            this.buf = buffer;
            this.view = Buffer.from(buffer);
        }
        else {
            this.buf = new ArrayBuffer(0);
            this.view = Buffer.from(this.buf);
        }
    }
    get buffer() {
        return this.view.subarray(0, this.start);
    }
    get length() {
        return this.view.byteLength;
    }
    get position() {
        return this.start;
    }
    seek(position) {
        this.start = position;
    }
    // Returns a Buffer containing the specified number of bytes starting at the
    // given start position.
    slice(start, len) {
        const end = start + len;
        if (end > this.length) {
            throw new StreamError('request past end of buffer');
        }
        return this.view.subarray(start, end);
    }
    appendChar(char) {
        this.ensureCapacity(1);
        this.view[this.start] = char;
        this.start += 1;
    }
    appendUint16(num) {
        this.ensureCapacity(2);
        const value = new Uint16Array([num]);
        const view = new Uint8Array(value.buffer);
        this.view[this.start] = view[1];
        this.view[this.start + 1] = view[0];
        this.start += 2;
    }
    appendUint24(num) {
        this.ensureCapacity(3);
        const value = new Uint32Array([num]);
        const view = new Uint8Array(value.buffer);
        this.view[this.start] = view[2];
        this.view[this.start + 1] = view[1];
        this.view[this.start + 2] = view[0];
        this.start += 3;
    }
    appendView(view) {
        this.ensureCapacity(view.length);
        this.view.set(view, this.start);
        this.start += view.length;
    }
    getBlock(size) {
        if (size <= 0) {
            return Buffer.alloc(0);
        }
        if (this.start + size > this.view.length) {
            throw new Error('request past end of buffer');
        }
        const result = this.view.subarray(this.start, this.start + size);
        this.start += size;
        return result;
    }
    getUint8() {
        return this.getBlock(1)[0];
    }
    getUint16() {
        const block = this.getBlock(2);
        return (block[0] << 8) | block[1];
    }
    ensureCapacity(size) {
        if (this.start + size > this.view.byteLength) {
            const blockSize = ByteStream.BLOCK_SIZE + (size > ByteStream.BLOCK_SIZE ? size : 0);
            this.realloc(this.view.byteLength + blockSize);
        }
    }
    realloc(size) {
        const newArray = new ArrayBuffer(size);
        const newView = Buffer.from(newArray);
        // Copy the old buffer into the new one
        newView.set(this.view);
        this.buf = newArray;
        this.view = newView;
    }
}
exports.ByteStream = ByteStream;
ByteStream.BLOCK_SIZE = 1024;
