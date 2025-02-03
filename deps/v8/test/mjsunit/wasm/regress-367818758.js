// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax

var kWasmH0 = 0;
var kWasmH1 = 0x61;
var kWasmH2 = 0x73;
var kWasmH3 = 0x6d;
var kWasmV0 = 0x1;
var kWasmV1 = 0;
var kWasmV2 = 0;
var kWasmV3 = 0;
let kTypeSectionCode = 1;        // Function signature declarations
let kFunctionSectionCode = 3;    // Function declarations
let kExportSectionCode = 7;      // Exports
let kCodeSectionCode = 10;       // Function code
let kWasmFunctionTypeForm = 0x60;
let kWasmStructTypeForm = 0x5f;
let kNoSuperType = 0xFFFFFFFF;
let kWasmI32 = 0x7f;
let kWasmExternRef = -0x11;
let kLeb128Mask = 0x7f;
let kExternalFunction = 0;
function makeSig(params, results) {
  return {params: params, results: results};
}
const kWasmOpcodes = {
  'End': 0x0b,
  'I32Const': 0x41,
};
function defineWasmOpcode(name, value) {
  Object.defineProperty(globalThis, name, {value: value});
}
for (let name in kWasmOpcodes) {
  defineWasmOpcode(`kExpr${name}`, kWasmOpcodes[name]);
}
const kPrefixOpcodes = {
  'GC': 0xfb,
};
for (let prefix in kPrefixOpcodes) {
  defineWasmOpcode(`k${prefix}Prefix`, kPrefixOpcodes[prefix]);
}
let kExprStructNew = 0x00;
let kExprExternConvertAny = 0x1b;
class Binary {
  constructor() {
    this.length = 0;
    this.buffer = new Uint8Array(8192);
  }
  trunc_buffer() {
    return new Uint8Array(this.buffer.buffer, 0, this.length);
  }
  emit_u8(val) {
    this.buffer[this.length++] = val;
  }
  emit_leb_u(val) {
      let v = val & 0xff;
        this.buffer[this.length++] = v;
  }
  emit_u32v(val) {
    this.emit_leb_u(val);
  }
  emit_bytes(data) {
    this.buffer.set(data, this.length);
    this.length += data.length;
  }
  emit_string(string) {
    let string_utf8 = string;
    this.emit_u32v(string_utf8.length);
    for (let i = 0; i < string_utf8.length; i++) {
      this.emit_u8(string_utf8.charCodeAt(i));
    }
  }
  emit_type(type) {
      this.emit_u8(type >= 0 ? type : type & kLeb128Mask);
  }
  emit_header() {
    this.emit_bytes([
      kWasmH0, kWasmH1, kWasmH2, kWasmH3, kWasmV0, kWasmV1, kWasmV2, kWasmV3
    ]);
  }
  emit_section(section_code, content_generator) {
    this.emit_u8(section_code);
    const section = new Binary;
    content_generator(section);
    this.emit_u32v(section.length);
    this.emit_bytes(section.trunc_buffer());
  }
}
class WasmFunctionBuilder {
  constructor(module, name, type_index, arg_names) {
    this.module = module;
    this.name = name;
    this.type_index = type_index;
  }
  exportAs(name) {
    this.module.addExport(name, this.index);
  }
  exportFunc() {
    this.exportAs(this.name);
    return this;
  }
  addBody(body) {
    this.body = body.concat([kExprEnd]);
  }
}
function makeField(type, mutability) {
  return {type: type, mutability: mutability};
}
class WasmStruct {
  constructor(fields) {
    this.fields = fields;
  }
}
class WasmModuleBuilder {
  constructor() {
    this.types = [];
    this.exports = [];
    this.functions = [];
  }
  addType(type, supertype_idx = kNoSuperType, is_final = true,
      is_shared = false) {
    var type_copy = {params: type.params, results: type.results,
                     is_final: is_final, is_shared: is_shared,
                     supertype: supertype_idx};
    this.types.push(type_copy);
    return this.types.length - 1;
  }
  addStruct(fields = kNoSuperType = false, is_shared = false) {
    this.types.push(new WasmStruct(fields));
  }
  addFunction(name, type, arg_names) {
    let type_index =typeof type == 'number' ? type : this.addType(type);
    let func = new WasmFunctionBuilder(this, name, type_index);
    this.functions.push(func);
    return func;
  }
  addExport(name, index) {
    this.exports.push({name: name, kind: kExternalFunction, index: index});
  }
  toBuffer() {
    let binary = new Binary;
    let wasm = this;
    binary.emit_header();
      binary.emit_section(kTypeSectionCode, section => {
        let length_with_groups = wasm.types.length;
        section.emit_u32v(length_with_groups);
        for (let i = 0; i < wasm.types.length; i++) {
          let type = wasm.types[i];
          if (type instanceof WasmStruct) {
            section.emit_u8(kWasmStructTypeForm);
            section.emit_u32v(type.fields.length);
            for (let field of type.fields) {
              section.emit_type(field.type);
              section.emit_u8();
            }
          } else {
            section.emit_u8(kWasmFunctionTypeForm);
            section.emit_u32v();
            section.emit_u32v(type.results.length);
            for (let result of type.results) {
              section.emit_type(result);
            }
          }
        }
      });
      binary.emit_section(kFunctionSectionCode, section => {
        section.emit_u32v(wasm.functions.length);
        for (let func of wasm.functions) {
          section.emit_u32v(func.type_index);
        }
      });
    var exports_count = wasm.exports.length;
      binary.emit_section(kExportSectionCode, section => {
        section.emit_u32v(exports_count);
        for (let exp of wasm.exports) {
          section.emit_string(exp.name);
          section.emit_u8();
          section.emit_u32v();
        }
      });
      binary.emit_section(kCodeSectionCode, section => {
        section.emit_u32v(wasm.functions.length);
        for (let func of wasm.functions) {
            section.emit_u32v(func.body.length + 1);
            section.emit_u8();  // 0 locals.
          section.emit_bytes(func.body);
        }
      });
    return binary.trunc_buffer();
  }
  instantiate() {
    let module = this.toModule();
    let instance = new WebAssembly.Instance(module);
    return instance;
  }
  toModule() {
    return new WebAssembly.Module(this.toBuffer());
  }
}
let builder = new WasmModuleBuilder();
let struct_type = builder.addStruct([makeField(kWasmI32)]);
builder.addFunction('MakeStruct', makeSig([], [kWasmExternRef])).exportFunc()
       .addBody([kExprI32Const, 42, kGCPrefix, kExprStructNew, struct_type,
                 kGCPrefix, kExprExternConvertAny]);
let instance = builder.instantiate();
let evil_wasm_object = instance.exports.MakeStruct();
function evil_ctor(){
}
function evil_cast_jit(evil_o){
    global_collect_node_info = evil_o; // get nodeinfo from PropertyCellStore
    return evil_o instanceof evil_ctor;
}
evil_ctor.prototype = evil_wasm_object;
%PrepareFunctionForOptimization(evil_cast_jit);
evil_cast_jit(new evil_ctor());
evil_cast_jit(new evil_ctor());
%OptimizeFunctionOnNextCall(evil_cast_jit);
evil_cast_jit();
