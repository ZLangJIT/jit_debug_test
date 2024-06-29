#include <llvm/ExecutionEngine/Orc/ThreadSafeModule.h>
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/ExecutionEngine/Orc/Mangling.h>
#include <llvm/Support/InitLLVM.h>
#include <llvm/Support/CommandLine.h>

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