WASM2WAT := ../wabt/bin/wasm2wat
WASM_OPT := ../binaryen/bin/wasm-opt

.PHONY: optimize clean

lib/lexer.wat: lib/lexer.wasm
	$(WASM2WAT) lib/lexer.wasm -o lib/lexer.wat

lib/lexer.wasm: include-wasm/cjs-module-lexer.h src/lexer.c | lib/
	node build/wasm.js --docker

lib/:
	@mkdir -p $@

clean:
	$(RM) lib/*
