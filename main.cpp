#include "jit.h"

#define STR_(x) #x
#define STR(x) STR_(x)

#if defined (_WIN32)
	// Windows defaults to using the COFF binary format (aka. "msvc" in the target triple).
	// Override it to use the ELF format to support DWARF debug info, but keep using the
	// Microsoft calling convention (see also https://llvm.org/docs/DebuggingJITedCode.html).
	#define target_triple "x86_64-pc-windows-elf"
#else
	#define target_triple LLVM_DEFAULT_TARGET_TRIPLE
#endif

int main(int argc, char *argv[]) {

    JIT::main_llvm_init main_init(argc, const_cast<const char**>(argv));
    
    JIT jit;
    
    system(STR(CLANG_EXE) " jit_code.c -emit-llvm -O0 -g3 -triple " STR(target_triple) " -S -o tmp.ll");
    
    jit.add_IR_module("tmp.ll");
    
    int (*main_func)(void) = jit.lookup_as_pointer<int(void)>("j");
   
    int res = main_func();
    llvm::outs() << "j() = " << res << "\n";
    
    return 0;
}