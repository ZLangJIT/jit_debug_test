#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/ExecutionEngine/Orc/Mangling.h>
#include <llvm/ExecutionEngine/Orc/ThreadSafeModule.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/InitLLVM.h>

// https://github.com/NVIDIA/warp/blob/main/warp/native/clang/clang.cpp

#ifdef _WIN32
	// Windows defaults to using the COFF binary format (aka. "msvc" in the target triple).
	// Override it to use the ELF format to support DWARF debug info, but keep using the
	// Microsoft calling convention (see also https://llvm.org/docs/DebuggingJITedCode.html).
	#define jit_target_triple "x86_64-pc-windows-coff"
#else
	#define jit_target_triple LLVM_DEFAULT_TARGET_TRIPLE
#endif

#ifdef _WIN32
	// symbols must be explicitly exported as dllexport in windows
	#define JIT_DLL_EXPORT __declspec(dllexport)
#else
	#define JIT_DLL_EXPORT
#endif

#if _WIN32
#include <stdio.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <libloaderapi.h>

#define jit_ps(x) \
  printf( \
    "&%s == 0x%p\n", \
    #x, GetProcAddress(GetModuleHandle(nullptr), #x) \
  )

#else
#define jit_ps(x)
#endif


class JIT {
    std::unique_ptr<llvm::orc::LLJIT> jit;

    public:

    JIT();
    JIT(bool jitlink);

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