#include "jit.h"

#ifdef _WIN32
#include <llvm/ExecutionEngine/Orc/Shared/WrapperFunctionUtils.h>
#endif

extern "C" {

// GDB and LLDB support debugging of JIT-compiled code by observing calls to __jit_debug_register_code()
// by putting a breakpoint on it, and retrieving the debug info through __jit_debug_descriptor.
// On Linux it suffices for these symbols not to be stripped out, while for Windows a .pdb has to contain
// their information. LLVM defines them, but we don't want a huge .pdb with all LLVM source code's debug
// info. By forward-declaring them here it suffices to compile this file with /Zi.

struct jit_descriptor;

extern struct jit_descriptor __jit_debug_descriptor;
extern JIT_DLL_EXPORT void __jit_debug_register_code();

#ifdef _WIN32
extern JIT_DLL_EXPORT llvm::orc::shared::CWrapperFunctionResult
llvm_orc_registerJITLoaderGDBWrapper(const char *Data, uint64_t Size);
extern JIT_DLL_EXPORT llvm::orc::shared::CWrapperFunctionResult
llvm_orc_registerJITLoaderGDBAllocAction(const char *Data, size_t Size);
#endif

}

#define STR_(x) #x
#define STR(x) STR_(x)

int main(int argc, char *argv[]) {

    JIT::main_llvm_init main_init(argc, const_cast<const char**>(argv));
    
    JIT jit;
    
    llvm::outs() << "invoking [ " STR(CLANG_EXE) " jit_code.c -emit-llvm -O0 -g3 -Xclang -triple -Xclang " STR(jit_target_triple) " -S -o tmp.ll" " ]\n";
    
    system(STR(CLANG_EXE) " jit_code.c -emit-llvm -O0 -g3 -Xclang -triple -Xclang " STR(jit_target_triple) " -S -o tmp.ll");
    
    jit.add_IR_module("tmp.ll");
    
    int (*main_func)(void) = jit.lookup_as_pointer<int(void)>("j");
   
    int res = main_func();
    llvm::outs() << "j() = " << res << "\n";
    
    return 0;
}