lib/lexer.wasm: include-wasm/cjs-module-lexer.h src/lexer.c
	@mkdir -p lib
	clang --sysroot=/usr/share/wasi-sysroot -target wasm32-unknown-wasi src/lexer.c -I include-wasm -o lib/lexer.wasm -nostartfiles \
	-Wl,-z,stack-size=13312,--no-entry,--compress-relocations,--strip-all,--export=__heap_base,\
	--export=parseCJS,--export=sa,--export=e,--export=re,--export=es,--export=ee,--export=rre,--export=ree,--export=res,--export=ru,--export=us,--export=ue \
	-Wno-logical-op-parentheses -Wno-parentheses \
	-Oz

optimize: lib/lexer.wasm
	${WASM_OPT} -Oz lib/lexer.wasm -o lib/lexer.wasm

clean:
	rm lib/*
