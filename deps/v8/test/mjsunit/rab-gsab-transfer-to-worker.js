// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

(function TransferArrayBuffer() {
  function workerCode() {
    onmessage = function({data:msg}) {
      const ab = msg.data;
      postMessage(ab.byteLength + ' ' + ab.maxByteLength);
      postMessage(ab.resizable + ' ' + ab.growable);
    }
  }

  const worker = new Worker(workerCode, {type: 'function'});

  const ab = new ArrayBuffer(16);
  worker.postMessage({data: ab}, [ab]);
  assertEquals('16 16', worker.getMessage());
  assertEquals('false undefined', worker.getMessage());

  const rab = new ArrayBuffer(16, {maxByteLength: 1024});
  worker.postMessage({data: rab}, [rab]);
  assertEquals('16 1024', worker.getMessage());
  assertEquals('true undefined', worker.getMessage());

  const sab = new SharedArrayBuffer(16);
  worker.postMessage({data: sab});
  assertEquals('16 16', worker.getMessage());
  assertEquals('undefined false', worker.getMessage());

  const gsab = new SharedArrayBuffer(16, {maxByteLength: 1024});
  worker.postMessage({data: gsab});
  assertEquals('16 1024', worker.getMessage());
  assertEquals('undefined true', worker.getMessage());
})();

(function TransferLengthTrackingRabBackedTypedArray() {
  function workerCode() {
    onmessage = function({data:msg}) {
      postMessage(msg.data.length);
      msg.data.buffer.resize(150);
      postMessage(msg.data.length);
    }
  }

  const worker = new Worker(workerCode, {type: 'function'});
  const rab = new ArrayBuffer(16, {maxByteLength: 1024});
  const ta = new Uint8Array(rab);
  worker.postMessage({data: ta}, [rab]);
  assertEquals(16, worker.getMessage());
  assertEquals(150, worker.getMessage());
})();

(function TransferLengthTrackingGsabBackedTypedArray() {
  function workerCode() {
    onmessage = function({data:msg}) {
      postMessage(msg.data.length);
      msg.data.buffer.grow(150);
      postMessage(msg.data.length);
    }
  }

  const worker = new Worker(workerCode, {type: 'function'});
  const gsab = new SharedArrayBuffer(16, {maxByteLength: 1024});
  const ta = new Uint8Array(gsab);
  worker.postMessage({data: ta});
  assertEquals(16, worker.getMessage());
  assertEquals(150, worker.getMessage());
})();

(function TransferFixedLengthRabBackedTypedArray() {
  function workerCode() {
    onmessage = function({data:msg}) {
      const ta = msg.data;
      postMessage(`${ta.length} ${ta[0]} ${ta[1]} ${ta[2]}`);
      ta.buffer.resize(2);
      postMessage(`${ta.length}`);
    }
  }

  const worker = new Worker(workerCode, {type: 'function'});
  const rab = new ArrayBuffer(16, {maxByteLength: 1024});
  const ta = new Uint8Array(rab, 0, 10);
  ta[0] = 30;
  ta[1] = 11;
  ta[2] = 22;
  worker.postMessage({data: ta}, [rab]);
  assertEquals('10 30 11 22', worker.getMessage());
  assertEquals('0', worker.getMessage());
})();

(function TransferOutOfBoundsFixedLengthTypedArray() {
  function workerCode() {
  }

  const worker = new Worker(workerCode, {type: 'function'});
  const rab = new ArrayBuffer(16, {maxByteLength: 1024});
  const ta = new Uint8Array(rab, 0, 10);
  rab.resize(0);
  assertThrows(() => { worker.postMessage({data: ta}, [rab]) });
})();

(function TransferGsabBackedFixedLengthTypedArray() {
  function workerCode() {
    onmessage = function({data:msg}) {
      const ta = msg.data;
      postMessage(`${ta.length} ${ta[0]} ${ta[1]} ${ta[2]}`);
      ta.buffer.grow(20);
      postMessage(`${ta.length} ${ta[0]} ${ta[1]} ${ta[2]}`);
    }
  }

  const worker = new Worker(workerCode, {type: 'function'});

  const gsab = new SharedArrayBuffer(16, {maxByteLength: 1024});
  const ta = new Uint8Array(gsab, 0, 10);
  ta[0] = 30;
  ta[1] = 11;
  ta[2] = 22;
  worker.postMessage({data: ta});
  assertEquals('10 30 11 22', worker.getMessage());
  assertEquals('10 30 11 22', worker.getMessage());
})();

(function TransferLengthTrackingDataView() {
  function workerCode() {
    onmessage = function({data:msg}) {
      const dv = msg.data;
      postMessage(dv.byteLength);
      dv.buffer.resize(150);
      postMessage(dv.byteLength);
    }
  }

  const worker = new Worker(workerCode, {type: 'function'});
  const rab = new ArrayBuffer(16, {maxByteLength: 1024});
  const dv = new DataView(rab);
  worker.postMessage({data: dv}, [rab]);
  assertEquals(16, worker.getMessage());
  assertEquals(150, worker.getMessage());
})();

(function TransferFixedLengthDataView() {
  function workerCode() {
    onmessage = function({data:msg}) {
      const dv = msg.data;
      postMessage(`${dv.byteLength} ${dv.getUint8(0)} ${dv.getUint8(1)}`);
      dv.buffer.resize(2);
      try {
        dv.byteLength;
      } catch(e) {
        postMessage('byteLength getter threw');
      }
    }
  }

  const worker = new Worker(workerCode, {type: 'function'});
  const rab = new ArrayBuffer(16, {maxByteLength: 1024});
  const ta = new Uint8Array(rab);
  ta[0] = 30;
  ta[1] = 11;
  worker.postMessage({data: new DataView(rab, 0, 10)}, [rab]);
  assertEquals('10 30 11', worker.getMessage());
  assertEquals('byteLength getter threw', worker.getMessage());
})();

(function TransferOutOfBoundsDataView1() {
  function workerCode() {}

  const worker = new Worker(workerCode, {type: 'function'});
  const rab = new ArrayBuffer(16, {maxByteLength: 1024});
  const dv = new Uint8Array(rab, 0, 10);
  rab.resize(0);
  assertThrows(() => { worker.postMessage({data: dv}, [rab]) });
})();

(function TransferOutOfBoundsDataView2() {
  function workerCode() {}

  const worker = new Worker(workerCode, {type: 'function'});
  const rab = new ArrayBuffer(16, {maxByteLength: 1024});
  const dv = new Uint8Array(rab, 2);
  rab.resize(1);
  assertThrows(() => { worker.postMessage({data: dv}, [rab]) });
})();

(function TransferZeroLengthDataView1() {
  function workerCode() {
    onmessage = function({data:msg}) {
      const dv = msg.data;
      postMessage(`${dv.byteLength}`);
    }
  }

  const worker = new Worker(workerCode, {type: 'function'});
  const rab = new ArrayBuffer(16, {maxByteLength: 1024});

  worker.postMessage({data: new DataView(rab, 16)}, [rab]);
  assertEquals('0', worker.getMessage());
})();

(function TransferZeroLengthDataView2() {
  function workerCode() {
    onmessage = function({data:msg}) {
      const dv = msg.data;
      postMessage(`${dv.byteLength}`);
    }
  }

  const worker = new Worker(workerCode, {type: 'function'});
  const rab = new ArrayBuffer(16, {maxByteLength: 1024});

  worker.postMessage({data: new DataView(rab, 16, 0)}, [rab]);
  assertEquals('0', worker.getMessage());
})();

(function TransferZeroLengthDataView3() {
  function workerCode() {
    onmessage = function({data:msg}) {
      const dv = msg.data;
      postMessage(`${dv.byteLength}`);
    }
  }

  const worker = new Worker(workerCode, {type: 'function'});
  const rab = new ArrayBuffer(16, {maxByteLength: 1024});

  worker.postMessage({data: new DataView(rab, 5, 0)}, [rab]);
  assertEquals('0', worker.getMessage());
})();
