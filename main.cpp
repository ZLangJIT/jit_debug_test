#include "jit.h"

int main(int argc, char *argv[]) {

    JIT::main_llvm_init main_init(argc, const_cast<const char**>(argv));
    
    JIT jit;
    
    system("clang-18 jit_code.c -emit-llvm -O0 -g3 -S -o tmp.ll");
    
    jit.add_IR_module("tmp.ll");
    
    int (*main_func)(void) = jit.lookup_as_pointer<int(void)>("j");
   
    int res = main_func();
    llvm::outs() << "j() = " << res << "\n";
    
    return 0;
}