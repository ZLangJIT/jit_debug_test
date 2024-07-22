#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/ExecutionEngine/Orc/Mangling.h>
#ifdef _WIN32
#include <llvm/ExecutionEngine/Orc/Shared/WrapperFunctionUtils.h>
#endif
#include <llvm/ExecutionEngine/Orc/ThreadSafeModule.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/InitLLVM.h>

// https://github.com/NVIDIA/warp/blob/main/warp/native/clang/clang.cpp

#ifdef _WIN32
	// Windows defaults to using the COFF binary format (aka. "msvc" in the target triple).
	// Override it to use the ELF format to support DWARF debug info, but keep using the
	// Microsoft calling convention (see also https://llvm.org/docs/DebuggingJITedCode.html).
	#define jit_target_triple "x86_64-pc-windows-elf"
	
	// symbols must be explicitly exported as dllexport in windows
	#define JIT_DLL_EXPORT __declspec(dllexport)
#else
	#define jit_target_triple LLVM_DEFAULT_TARGET_TRIPLE
	#define JIT_DLL_EXPORT
#endif

extern "C" {

// GDB and LLDB support debugging of JIT-compiled code by observing calls to __jit_debug_register_code()
// by putting a breakpoint on it, and retrieving the debug info through __jit_debug_descriptor.
// On Linux it suffices for these symbols not to be stripped out, while for Windows a .pdb has to contain
// their information. LLVM defines them, but we don't want a huge .pdb with all LLVM source code's debug
// info. By forward-declaring them here it suffices to compile this file with /Zi.

struct jit_descriptor __jit_debug_descriptor;
JIT_DLL_EXPORT void __jit_debug_register_code();

#ifdef _WIN32
JIT_DLL_EXPORT llvm::orc::shared::CWrapperFunctionResult
llvm_orc_registerJITLoaderGDBWrapper(const char *Data, uint64_t Size);
JIT_DLL_EXPORT llvm::orc::shared::CWrapperFunctionResult
llvm_orc_registerJITLoaderGDBAllocAction(const char *Data, size_t Size);
#endif

}

class JIT {
    std::unique_ptr<llvm::orc::LLJIT> jit;

    public:

    JIT();

    struct main_llvm_init final {
        std::unique_ptr<llvm::InitLLVM> X;
        std::unique_ptr<const char *> Xprog;
        main_llvm_init(int argc, const char * argv[]);
        inline main_llvm_init() {
            Xprog = std::make_unique<const char*>("/null");
            main_llvm_init(1, Xprog.get());
        }
    };

    void add_IR_module(llvm::orc::ThreadSafeModule && module);
    void add_IR_module(llvm::StringRef name);

    llvm::Expected<llvm::orc::ExecutorAddr> lookup(llvm::StringRef symbol);

    template <typename T>
    inline auto lookup_as_pointer(llvm::StringRef symbol) {
        return lookup(symbol)->toPtr<T>();
    }

    void run_static_initializer();
    void run_static_deinitializer();

    void dump(llvm::raw_ostream & os);
};