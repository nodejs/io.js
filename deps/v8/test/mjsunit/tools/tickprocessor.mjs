// Copyright 2009 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// Flags: --enable-os-system
// Resources: test/mjsunit/tools/tickprocessor-test-func-info.log
// Resources: test/mjsunit/tools/tickprocessor-test-func-info.log.symbols.json
// Resources: test/mjsunit/tools/tickprocessor-test-large.default
// Resources: test/mjsunit/tools/tickprocessor-test-large.js
// Resources: test/mjsunit/tools/tickprocessor-test-large.log
// Resources: test/mjsunit/tools/tickprocessor-test-large.log.symbols.json
// Resources: test/mjsunit/tools/tickprocessor-test.default
// Resources: test/mjsunit/tools/tickprocessor-test.func-info
// Resources: test/mjsunit/tools/tickprocessor-test.gc-state
// Resources: test/mjsunit/tools/tickprocessor-test.ignore-unknown
// Resources: test/mjsunit/tools/tickprocessor-test.log
// Resources: test/mjsunit/tools/tickprocessor-test.log.symbols.json
// Resources: test/mjsunit/tools/tickprocessor-test.only-summary
// Resources: test/mjsunit/tools/tickprocessor-test.separate-sparkplug-handlers
// Resources: test/mjsunit/tools/tickprocessor-test.separate-bytecodes
// Resources: test/mjsunit/tools/tickprocessor-test.separate-ic

import {
  TickProcessor, ArgumentsProcessor, LinuxCppEntriesProvider,
  MacOSCppEntriesProvider, WindowsCppEntriesProvider
} from "../../../tools/tickprocessor.mjs";

(function testArgumentsProcessor() {
  var p_default = new ArgumentsProcessor([]);
  assertTrue(p_default.parse());
  assertEquals(p_default.getDefaultResults(), p_default.result());

  var p_logFile = new ArgumentsProcessor(['logfile.log']);
  assertTrue(p_logFile.parse());
  assertEquals('logfile.log', p_logFile.result().logFileName);

  var p_platformAndLog = new ArgumentsProcessor(['--windows', 'winlog.log']);
  assertTrue(p_platformAndLog.parse());
  assertEquals('windows', p_platformAndLog.result().platform);
  assertEquals('winlog.log', p_platformAndLog.result().logFileName);

  var p_flags = new ArgumentsProcessor(['--gc', '--separate-ic=true']);
  assertTrue(p_flags.parse());
  assertEquals(TickProcessor.VmStates.GC, p_flags.result().stateFilter);
  assertTrue(p_flags.result().separateIc);

  var p_flags = new ArgumentsProcessor(['--gc', '--separate-ic=false']);
  assertTrue(p_flags.parse());
  assertEquals(TickProcessor.VmStates.GC, p_flags.result().stateFilter);
  assertFalse(p_flags.result().separateIc);

  var p_nmAndLog = new ArgumentsProcessor(['--nm=mn', 'nmlog.log']);
  assertTrue(p_nmAndLog.parse());
  assertEquals('mn', p_nmAndLog.result().nm);
  assertEquals('nmlog.log', p_nmAndLog.result().logFileName);

  var p_bad = new ArgumentsProcessor(['--unknown', 'badlog.log']);
  assertFalse(p_bad.parse());
})();


await (async function testUnixCppEntriesProvider() {
  var oldLoadSymbols = LinuxCppEntriesProvider.prototype.loadSymbols;

  // shell executable
  LinuxCppEntriesProvider.prototype.loadSymbols = function(libName) {
    this.symbols = [[
      '         U operator delete[](void*)@@GLIBCXX_3.4',
      '08049790 T _init',
      '08049f50 T _start',
      '08139150 00000b4b t v8::internal::Runtime_StringReplaceRegExpWithString(v8::internal::Arguments)',
      '08139ca0 000003f1 T v8::internal::Runtime::GetElementOrCharAt(v8::internal::Handle<v8::internal::Object>, unsigned int)',
      '0813a0b0 00000855 t v8::internal::Runtime_DebugGetPropertyDetails(v8::internal::Arguments)',
      '0818b220 00000036 W v8::internal::RegExpMacroAssembler::CheckPosition(int, v8::internal::Label*)',
      '         w __gmon_start__',
      '081f08a0 00000004 B stdout\n'
    ].join('\n'), ''];
  };

  var shell_prov = new LinuxCppEntriesProvider();
  var shell_syms = [];
  await shell_prov.parseVmSymbols('shell', 0x08048000, 0x081ee000, 0,
      (...params) => shell_syms.push(params));
  assertEquals(
      [['_init', 0x08049790, 0x08049f50],
       ['_start', 0x08049f50, 0x08139150],
       ['v8::internal::Runtime_StringReplaceRegExpWithString(v8::internal::Arguments)', 0x08139150, 0x08139150 + 0xb4b],
       ['v8::internal::Runtime::GetElementOrCharAt(v8::internal::Handle<v8::internal::Object>, unsigned int)', 0x08139ca0, 0x08139ca0 + 0x3f1],
       ['v8::internal::Runtime_DebugGetPropertyDetails(v8::internal::Arguments)', 0x0813a0b0, 0x0813a0b0 + 0x855],
       ['v8::internal::RegExpMacroAssembler::CheckPosition(int, v8::internal::Label*)', 0x0818b220, 0x0818b220 + 0x36]],
      shell_syms);
  // With BigInts
  const useBigIntsArgs = [undefined, undefined, undefined, undefined, true]
  var shell_prov = new LinuxCppEntriesProvider(...useBigIntsArgs);
  var shell_syms = [];
  await shell_prov.parseVmSymbols('shell', 0x08048000n, 0x081ee000n, 0n,
      (...params) => shell_syms.push(params));
  assertEquals(
      [['_init', 0x08049790n, 0x08049f50n],
       ['_start', 0x08049f50n, 0x08139150n],
       ['v8::internal::Runtime_StringReplaceRegExpWithString(v8::internal::Arguments)', 0x08139150n, 0x08139150n + 0xb4bn],
       ['v8::internal::Runtime::GetElementOrCharAt(v8::internal::Handle<v8::internal::Object>, unsigned int)', 0x08139ca0n, 0x08139ca0n + 0x3f1n],
       ['v8::internal::Runtime_DebugGetPropertyDetails(v8::internal::Arguments)', 0x0813a0b0n, 0x0813a0b0n + 0x855n],
       ['v8::internal::RegExpMacroAssembler::CheckPosition(int, v8::internal::Label*)', 0x0818b220n, 0x0818b220n + 0x36n]],
      shell_syms);


  // libc library
  LinuxCppEntriesProvider.prototype.loadSymbols = function(libName) {
    this.symbols = [[
        '000162a0 00000005 T __libc_init_first',
        '0002a5f0 0000002d T __isnan',
        '0002a5f0 0000002d W isnan',
        '0002aaa0 0000000d W scalblnf',
        '0002aaa0 0000000d W scalbnf',
        '0011a340 00000048 T __libc_thread_freeres',
        '00128860 00000024 R _itoa_lower_digits\n'].join('\n'), ''];
  };
  var libc_prov = new LinuxCppEntriesProvider();
  var libc_syms = [];
  await libc_prov.parseVmSymbols('libc', 0xf7c5c000, 0xf7da5000, 0,
      (...params) => libc_syms.push(params));
  var libc_ref_syms = [['__libc_init_first', 0x000162a0, 0x000162a0 + 0x5],
       ['__isnan', 0x0002a5f0, 0x0002a5f0 + 0x2d],
       ['scalblnf', 0x0002aaa0, 0x0002aaa0 + 0xd],
       ['__libc_thread_freeres', 0x0011a340, 0x0011a340 + 0x48]];
  for (var i = 0; i < libc_ref_syms.length; ++i) {
    libc_ref_syms[i][1] += 0xf7c5c000;
    libc_ref_syms[i][2] += 0xf7c5c000;
  }
  assertEquals(libc_ref_syms, libc_syms);

  // Android library with zero length duplicates.
  LinuxCppEntriesProvider.prototype.loadSymbols = function(libName) {
    this.symbols = [[
      '00000000013a1088 0000000000000224 t v8::internal::interpreter::BytecodeGenerator::BytecodeGenerator(v8::internal::UnoptimizedCompilationInfo*)',
      '00000000013a1088 0000000000000224 t v8::internal::interpreter::BytecodeGenerator::BytecodeGenerator(v8::internal::UnoptimizedCompilationInfo*)',
      '00000000013a12ac t $x.4',
      '00000000013a12ac 00000000000000d0 t v8::internal::interpreter::BytecodeGenerator::FinalizeBytecode(v8::internal::Isolate*, v8::internal::Handle<v8::internal::Script>)',
      '00000000013a137c t $x.5',
      '00000000013a137c 0000000000000528 t v8::internal::interpreter::BytecodeGenerator::AllocateDeferredConstants(v8::internal::Isolate*, v8::internal::Handle<v8::internal::Script>)',
      '00000000013a1578 N $d.46',
      '00000000013a18a4 t $x.6',
      '00000000013a18a4 0000000000000 t v8::internal::interpreter::BytecodeGenerator::GlobalDeclarationsBuilder::AllocateDeclarations(v8::internal::UnoptimizedCompilationInfo*, v8::internal::Handle<v8::internal::Script>, v8::internal::Isolate*)',
      '00000000013a19e0 t $x.7',
      '00000000013a19e0 0000000000000244 t v8::internal::interpreter::BytecodeGenerator::GenerateBytecode(unsigned long)',
      '00000000013a1a88 N $d.7',
      '00000000013a1ac8 N $d.5',
      '00000000013a1af8 N $d.35',
      '00000000013a1c24 t $x.8',
      '00000000013a1c24 000000000000009c t v8::internal::interpreter::BytecodeGenerator::ContextScope::ContextScope(v8::internal::interpreter::BytecodeGenerator*, v8::internal::Scope*)\n',
    ].join('\n'), ''];
  };
  var android_prov = new LinuxCppEntriesProvider();
  var android_syms = [];
  await android_prov.parseVmSymbols('libmonochrome', 0xf7c5c000, 0xf9c5c000, 0,
      (...params) => android_syms.push(params));
  var android_ref_syms = [
       ['v8::internal::interpreter::BytecodeGenerator::BytecodeGenerator(v8::internal::UnoptimizedCompilationInfo*)', 0x013a1088, 0x013a1088 + 0x224],
       ['v8::internal::interpreter::BytecodeGenerator::FinalizeBytecode(v8::internal::Isolate*, v8::internal::Handle<v8::internal::Script>)', 0x013a12ac, 0x013a12ac + 0xd0],
       ['v8::internal::interpreter::BytecodeGenerator::AllocateDeferredConstants(v8::internal::Isolate*, v8::internal::Handle<v8::internal::Script>)', 0x013a137c, 0x013a137c + 0x528],
       ['v8::internal::interpreter::BytecodeGenerator::GlobalDeclarationsBuilder::AllocateDeclarations(v8::internal::UnoptimizedCompilationInfo*, v8::internal::Handle<v8::internal::Script>, v8::internal::Isolate*)', 0x013a18a4, 0x013a18a4 + 0x13c],
       ['v8::internal::interpreter::BytecodeGenerator::GenerateBytecode(unsigned long)', 0x013a19e0, 0x013a19e0 + 0x244],
       ['v8::internal::interpreter::BytecodeGenerator::ContextScope::ContextScope(v8::internal::interpreter::BytecodeGenerator*, v8::internal::Scope*)', 0x013a1c24, 0x013a1c24 + 0x9c],
  ];
  for (var i = 0; i < android_ref_syms.length; ++i) {
    android_ref_syms[i][1] += 0xf7c5c000;
    android_ref_syms[i][2] += 0xf7c5c000;
  }
  assertEquals(android_ref_syms, android_syms);
  // With BigInts
  var android_prov = new LinuxCppEntriesProvider(...useBigIntsArgs);
  var android_syms = [];
  await android_prov.parseVmSymbols('libmonochrome', 0xf7c5c000n, 0xf9c5c000n, 0n,
      (...params) => android_syms.push(params));
  var android_ref_syms_bigints =  android_ref_syms.map(entry => [
    entry[0], BigInt(entry[1]), BigInt(entry[2])
  ])
  assertEquals(android_ref_syms_bigints, android_syms);

  LinuxCppEntriesProvider.prototype.loadSymbols = oldLoadSymbols;
})();


await (async function testMacOSCppEntriesProvider() {
  var oldLoadSymbols = MacOSCppEntriesProvider.prototype.loadSymbols;

  // shell executable
  MacOSCppEntriesProvider.prototype.loadSymbols = function(libName) {
    this.symbols = [[
      '         operator delete[]',
      '00001000 __mh_execute_header',
      '00001b00 start',
      '00001b40 dyld_stub_binding_helper',
      '0011b710 v8::internal::RegExpMacroAssembler::CheckPosition',
      '00134250 v8::internal::Runtime_StringReplaceRegExpWithString',
      '00137220 v8::internal::Runtime::GetElementOrCharAt',
      '00137400 v8::internal::Runtime_DebugGetPropertyDetails\n'
    ].join('\n'), ''];
  };
  var shell_prov = new MacOSCppEntriesProvider();
  var shell_syms = [];
  await shell_prov.parseVmSymbols('shell', 0x00001c00, 0x00163256, 0x100,
      (...params) => shell_syms.push(params));
  assertEquals(
      [['start', 0x00001c00, 0x00001c40],
       ['dyld_stub_binding_helper', 0x00001c40, 0x0011b810],
       ['v8::internal::RegExpMacroAssembler::CheckPosition', 0x0011b810, 0x00134350],
       ['v8::internal::Runtime_StringReplaceRegExpWithString', 0x00134350, 0x00137320],
       ['v8::internal::Runtime::GetElementOrCharAt', 0x00137320, 0x00137500],
       ['v8::internal::Runtime_DebugGetPropertyDetails', 0x00137500, 0x00163256]],
      shell_syms);

  // stdc++ library
  MacOSCppEntriesProvider.prototype.loadSymbols = function(libName) {
    this.symbols = [[
        '0000107a __gnu_cxx::balloc::__mini_vector<std::pair<__gnu_cxx::bitmap_allocator<char>::_Alloc_block*, __gnu_cxx::bitmap_allocator<char>::_Alloc_block*> >::__mini_vector',
        '0002c410 std::basic_streambuf<char, std::char_traits<char> >::pubseekoff',
        '0002c488 std::basic_streambuf<char, std::char_traits<char> >::pubseekpos',
        '000466aa ___cxa_pure_virtual\n'].join('\n'), ''];
  };
  var stdc_prov = new MacOSCppEntriesProvider();
  var stdc_syms = [];
  await stdc_prov.parseVmSymbols('stdc++', 0x95728fb4, 0x95770005, 0,
      (...params) => stdc_syms.push(params));
  var stdc_ref_syms = [['__gnu_cxx::balloc::__mini_vector<std::pair<__gnu_cxx::bitmap_allocator<char>::_Alloc_block*, __gnu_cxx::bitmap_allocator<char>::_Alloc_block*> >::__mini_vector', 0x0000107a, 0x0002c410],
       ['std::basic_streambuf<char, std::char_traits<char> >::pubseekoff', 0x0002c410, 0x0002c488],
       ['std::basic_streambuf<char, std::char_traits<char> >::pubseekpos', 0x0002c488, 0x000466aa],
       ['___cxa_pure_virtual', 0x000466aa, 0x95770005 - 0x95728fb4]];
  for (var i = 0; i < stdc_ref_syms.length; ++i) {
    stdc_ref_syms[i][1] += 0x95728fb4;
    stdc_ref_syms[i][2] += 0x95728fb4;
  }
  assertEquals(stdc_ref_syms, stdc_syms);
  // With BigInts
  const useBigIntsArgs = [undefined, undefined, undefined, undefined, true];
  var stdc_prov = new MacOSCppEntriesProvider(...useBigIntsArgs);
  var stdc_syms = [];
  await stdc_prov.parseVmSymbols('stdc++', 0x95728fb4n, 0x95770005n, 0n,
      (...params) => stdc_syms.push(params));
  var stdc_ref_syms_bigints = stdc_ref_syms.map(entry => [
    entry[0], BigInt(entry[1]), BigInt(entry[2])
  ])
  assertEquals(stdc_ref_syms_bigints, stdc_syms);

  MacOSCppEntriesProvider.prototype.loadSymbols = oldLoadSymbols;
})();


await (async function testWindowsCppEntriesProvider() {
  var oldLoadSymbols = WindowsCppEntriesProvider.prototype.loadSymbols;

  WindowsCppEntriesProvider.prototype.loadSymbols = function(libName) {
    this.symbols = [
      ' Start         Length     Name                   Class',
      ' 0001:00000000 000ac902H .text                   INSTRUCTION_STREAM',
      ' 0001:000ac910 000005e2H .text$yc                INSTRUCTION_STREAM',
      '  Address         Publics by Value              Rva+Base       Lib:Object',
      ' 0000:00000000       __except_list              00000000     <absolute>',
      ' 0001:00000000       ?ReadFile@@YA?AV?$Handle@VString@v8@@@v8@@PBD@Z 00401000 f   shell.obj',
      ' 0001:000000a0       ?Print@@YA?AV?$Handle@VValue@v8@@@v8@@ABVArguments@2@@Z 004010a0 f   shell.obj',
      ' 0001:00001230       ??1UTF8Buffer@internal@v8@@QAE@XZ 00402230 f   v8_snapshot:scanner.obj',
      ' 0001:00001230       ??1Utf8Value@String@v8@@QAE@XZ 00402230 f   v8_snapshot:api.obj',
      ' 0001:000954ba       __fclose_nolock            004964ba f   LIBCMT:fclose.obj',
      ' 0002:00000000       __imp__SetThreadPriority@8 004af000     kernel32:KERNEL32.dll',
      ' 0003:00000418       ?in_use_list_@PreallocatedStorage@internal@v8@@0V123@A 00544418     v8_snapshot:allocation.obj',
      ' Static symbols',
      ' 0001:00000b70       ?DefaultFatalErrorHandler@v8@@YAXPBD0@Z 00401b70 f   v8_snapshot:api.obj',
      ' 0001:000010b0       ?EnsureInitialized@v8@@YAXPBD@Z 004020b0 f   v8_snapshot:api.obj',
      ' 0001:000ad17b       ??__Fnomem@?5???2@YAPAXI@Z@YAXXZ 004ae17b f   LIBCMT:new.obj'
    ].join('\r\n');
  };
  var shell_prov = new WindowsCppEntriesProvider();
  var shell_syms = [];
  await shell_prov.parseVmSymbols('shell.exe', 0x00400000, 0x0057c000, 0,
      (...params) => shell_syms.push(params));
  assertEquals(
      [['ReadFile', 0x00401000, 0x004010a0],
       ['Print', 0x004010a0, 0x00402230],
       ['v8::String::?1Utf8Value', 0x00402230, 0x004964ba],
       ['v8::DefaultFatalErrorHandler', 0x00401b70, 0x004020b0],
       ['v8::EnsureInitialized', 0x004020b0, 0x0057c000]],
      shell_syms);
  // With BigInts
  const useBigIntsArgs = [undefined, undefined, undefined, undefined, true];
  var shell_prov = new WindowsCppEntriesProvider(...useBigIntsArgs);
  var shell_syms = [];
  await shell_prov.parseVmSymbols('shell.exe', 0x00400000n, 0x0057c000n, 0n,
      (...params) => shell_syms.push(params));
  assertEquals(
      [['ReadFile', 0x00401000n, 0x004010a0n],
       ['Print', 0x004010a0n, 0x00402230n],
       ['v8::String::?1Utf8Value', 0x00402230n, 0x004964ban],
       ['v8::DefaultFatalErrorHandler', 0x00401b70n, 0x004020b0n],
       ['v8::EnsureInitialized', 0x004020b0n, 0x0057c000n]],
      shell_syms);

  WindowsCppEntriesProvider.prototype.loadSymbols = oldLoadSymbols;
})();


// http://code.google.com/p/v8/issues/detail?id=427
await (async function testWindowsProcessExeAndDllMapFile() {
  function exeSymbols(exeName) {
    return [
      ' 0000:00000000       ___ImageBase               00400000     <linker-defined>',
      ' 0001:00000780       ?RunMain@@YAHHQAPAD@Z      00401780 f   shell.obj',
      ' 0001:00000ac0       _main                      00401ac0 f   shell.obj',
      ''
    ].join('\r\n');
  }

  function dllSymbols(dllName) {
    return [
      ' 0000:00000000       ___ImageBase               01c30000     <linker-defined>',
      ' 0001:00000780       _DllMain@12                01c31780 f   libcmt:dllmain.obj',
      ' 0001:00000ac0       ___DllMainCRTStartup       01c31ac0 f   libcmt:dllcrt0.obj',
      ''
    ].join('\r\n');
  }

  var oldRead = read;

  read = exeSymbols;
  var exe_exe_syms = [];
  await (new WindowsCppEntriesProvider()).parseVmSymbols(
      'chrome.exe', 0x00400000, 0x00472000, 0,
      (...params) => exe_exe_syms.push(params));
  assertEquals(
      [['RunMain', 0x00401780, 0x00401ac0],
       ['_main', 0x00401ac0, 0x00472000]],
      exe_exe_syms, '.exe with .exe symbols');
  // With BigInts
  read = exeSymbols;
  const useBigIntsArgs = [undefined, undefined, undefined, undefined, true];
  var exe_exe_syms = [];
  await (new WindowsCppEntriesProvider(...useBigIntsArgs)).parseVmSymbols(
      'chrome.exe', 0x00400000n, 0x00472000n, 0n,
      (...params) => exe_exe_syms.push(params));
  assertEquals(
      [['RunMain', 0x00401780n, 0x00401ac0n],
       ['_main', 0x00401ac0n, 0x00472000n]],
      exe_exe_syms, '.exe with .exe symbols');

  read = dllSymbols;
  var exe_dll_syms = [];
  await (new WindowsCppEntriesProvider()).parseVmSymbols(
      'chrome.exe', 0x00400000, 0x00472000, 0,
      (...params) => exe_dll_syms.push(params));
  assertEquals(
      [],
      exe_dll_syms, '.exe with .dll symbols');

  read = dllSymbols;
  var dll_dll_syms = [];
  await (new WindowsCppEntriesProvider()).parseVmSymbols(
      'chrome.dll', 0x01c30000, 0x02b80000, 0,
      (...params) => dll_dll_syms.push(params));
  assertEquals(
      [['_DllMain@12', 0x01c31780, 0x01c31ac0],
       ['___DllMainCRTStartup', 0x01c31ac0, 0x02b80000]],
      dll_dll_syms, '.dll with .dll symbols');
  // With BigInts
  read = dllSymbols;
  var dll_dll_syms = [];
  await (new WindowsCppEntriesProvider(...useBigIntsArgs)).parseVmSymbols(
      'chrome.dll', 0x01c30000n, 0x02b80000n, 0n,
      (...params) => dll_dll_syms.push(params));
  assertEquals(
      [['_DllMain@12', 0x01c31780n, 0x01c31ac0n],
       ['___DllMainCRTStartup', 0x01c31ac0n, 0x02b80000n]],
      dll_dll_syms, '.dll with .dll symbols');

  read = exeSymbols;
  var dll_exe_syms = [];
  await (new WindowsCppEntriesProvider()).parseVmSymbols(
      'chrome.dll', 0x01c30000, 0x02b80000, 0,
      (...params) => dll_exe_syms.push(params));
  assertEquals(
      [],
      dll_exe_syms, '.dll with .exe symbols');

  read = oldRead;
})();


class CppEntriesProviderMock {
  constructor(filename, useBigIntAddresses=false) {
    this.isLoaded = false;
    this.symbols = JSON.parse(d8.file.read(filename));
    this.parseAddr = useBigIntAddresses ? BigInt : parseInt;
  }
  parseVmSymbols(name, startAddr, endAddr, slideAddr, symbolAdder) {
    if (this.isLoaded) return;
    this.isLoaded = true;
    for (let symbolInfo of this.symbols) {
      const [name, start, end] = symbolInfo;
      symbolAdder.call(null, name, this.parseAddr(start), this.parseAddr(end))
    }
  }
}


class PrintMonitor {
  constructor(outputOrFileName) {
    this.expectedOut = outputOrFileName;
    this.outputFile = undefined;
    if (typeof outputOrFileName == 'string') {
      this.expectedOut = this.loadExpectedOutput(outputOrFileName)
      this.outputFile = outputOrFileName;
    }
    let expectedOut = this.expectedOut;
    let outputPos = 0;
    let diffs = this.diffs = [];
    let realOut = this.realOut = [];
    let unexpectedOut = this.unexpectedOut = null;

    this.oldPrint = print;
    print = function(str) {
      const strSplit = str.split('\n');
      for (let i = 0; i < strSplit.length; ++i) {
        const s = strSplit[i];
        realOut.push(s);
        if (outputPos < expectedOut.length) {
          if (expectedOut[outputPos] != s) {
            diffs.push('line ' + outputPos + ': expected <' +
                      expectedOut[outputPos] + '> found <' + s + '>\n');
          }
          outputPos++;
        } else {
          unexpectedOut = true;
        }
      }
    };
  }

  loadExpectedOutput(fileName) {
    const output = d8.file.read(fileName);
    return output.split('\n');
  }

 finish() {
    print = this.oldPrint;
    if (this.diffs.length == 0 && this.unexpectedOut == null) return;
    console.log("===== actual output: =====");
    console.log(this.realOut.join('\n'));
    console.log("===== expected output: =====");
    if (this.outputFile) {
      console.log("===== File: " + this.outputFile + " =====");
    }
    console.log(this.expectedOut.join('\n'));
    if (this.diffs.length > 0) {
      this.diffs.forEach(line => console.log(line))
      assertEquals([], this.diffs);
    }
    assertNull(this.unexpectedOut);
  }
}

await (async function testProcessing() {
  const testData = {
    'Default': [
      'tickprocessor-test.log', 'tickprocessor-test.default',
      ['--separate-ic=false']],
    'SeparateBytecodes': [
      'tickprocessor-test.log', 'tickprocessor-test.separate-bytecodes',
      ['--separate-ic=false', '--separate-bytecodes']],
    'SeparateSparkplugHandlers': [
      'tickprocessor-test.log', 'tickprocessor-test.separate-sparkplug-handlers',
      ['--separate-ic=false', '--separate-sparkplug-handlers']],
    'SeparateIc': [
      'tickprocessor-test.log', 'tickprocessor-test.separate-ic',
      ['--separate-ic=true']],
    'IgnoreUnknown': [
      'tickprocessor-test.log', 'tickprocessor-test.ignore-unknown',
      ['--separate-ic=false', '--ignore-unknown']],
    'GcState': [
      'tickprocessor-test.log', 'tickprocessor-test.gc-state', ['-g']],
    'OnlySummary': [
      'tickprocessor-test.log', 'tickprocessor-test.only-summary',
      ['--separate-ic=false', '--only-summary']],
    'FunctionInfo': [
      'tickprocessor-test-func-info.log', 'tickprocessor-test.func-info',
      ['']
    ],
    'DefaultLarge': [
      'tickprocessor-test-large.log', 'tickprocessor-test-large.default'],
  };
  for (var testName in testData) {
    await testTickProcessor(testName, ...testData[testName]);
  }
})();

async function testTickProcessor(testName, logInput, refOutput, args=[]) {
  console.log('=== testProcessing-' + testName + ' ===');
  await testTickProcessorBasic(logInput, refOutput, args);
  // Using BigInt address should not affect the output.
  console.log('=== testProcessing-' + testName + '-bigint-addresses ===');
  await testTickProcessorBasic(
      logInput, refOutput, [...args, '--use-bigint-addresses']);
}

async function testTickProcessorBasic(
      logInput, refOutput, args=[]) {
  // /foo/bar/tickprocessor.mjs => /foo/bar/
  const dir = import.meta.url.split("/").slice(0, -1).join('/') + '/';
  const params = ArgumentsProcessor.process(args);
  await testExpectations(dir, logInput, refOutput, params);
  // TODO(cbruni): enable again after it works on bots
  // await testEndToEnd(dir, 'tickprocessor-test-large.js', refOutput,  params);
}

async function testExpectations(dir, logInput, refOutput, params) {
  const symbolsFile = dir + logInput + '.symbols.json';
  const cppEntries = new CppEntriesProviderMock(
      symbolsFile, params.useBigIntAddresses);
  const tickProcessor = TickProcessor.fromParams(params, cppEntries);
  const printMonitor = new PrintMonitor(dir + refOutput);
  await tickProcessor.processLogFileInTest(dir + logInput);
  tickProcessor.printStatistics();
  printMonitor.finish();
};

async function testEndToEnd(dir, sourceFile, ignoredRefOutput, params) {
  // This test only works on linux.
  if (!os?.system) return;
  if (os.name !== 'linux' && os.name !== 'macos') return;
  params.platform = os.name;
  const tmpLogFile= `/var/tmp/${Date.now()}.v8.log`
  const result = os.system(
    os.d8Path, ['--prof', `--logfile=${tmpLogFile}`, dir + sourceFile]);

  const tickProcessor = TickProcessor.fromParams(params);
  // We will not always get the same ticks due to timing on bots,
  // hence we cannot properly compare output expectations.
  // Let's just use a dummy file and only test whether we don't throw.
  const printMonitor = new PrintMonitor(dir + ignoredRefOutput);
  await tickProcessor.processLogFileInTest(tmpLogFile);
  tickProcessor.printStatistics();
}
