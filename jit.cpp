#include <llvm/ADT/StringRef.h>
#include <llvm/ExecutionEngine/Orc/ThreadSafeModule.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/ADT/StringMap.h>
#include <llvm/ExecutionEngine/JITEventListener.h>
#include <llvm/ExecutionEngine/JITLink/JITLinkMemoryManager.h>
#include <llvm/ExecutionEngine/Orc/ExecutionUtils.h>
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/ExecutionEngine/Orc/RTDyldObjectLinkingLayer.h>
#include <llvm/ExecutionEngine/Orc/TargetProcess/TargetExecutionUtils.h>
#include <llvm/ExecutionEngine/SectionMemoryManager.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/PrettyStackTrace.h>

#include <llvm/IRReader/IRReader.h>

#include <llvm/MC/TargetRegistry.h>
#include <llvm/TargetParser/Host.h>

#include "jit.h"

llvm::ExitOnError ExitOnErr;

JIT::main_llvm_init::main_llvm_init(int argc, const char *argv[]) {
    // Initialize LLVM.
    X = std::make_unique<llvm::InitLLVM>(argc, argv);
    llvm::EnablePrettyStackTrace();

    llvm::InitializeAllTargetInfos();
    llvm::InitializeAllTargets();
    llvm::InitializeAllTargetMCs();
    llvm::InitializeAllAsmPrinters();
    llvm::InitializeAllAsmParsers();
    llvm::InitializeAllDisassemblers();

    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    llvm::InitializeNativeTargetAsmParser();
    llvm::InitializeNativeTargetDisassembler();
    llvm::InitializeAllTargetMCAs();

    // Register the Target and CPU printer for --version.
    llvm::cl::AddExtraVersionPrinter(llvm::sys::printDefaultTargetAndDetectedCPU);
    // Register the target printer for --version.
    llvm::cl::AddExtraVersionPrinter(llvm::TargetRegistry::printRegisteredTargetsForVersion);

    //llvm::cl::ParseCommandLineOptions(argc, argv, "JIT");
    ExitOnErr.setBanner(std::string(argv[0]) + ": ");
}

std::unique_ptr<llvm::orc::LLJIT> build_jit() {
    // Detect the host and set code model to small.
    auto JTMB = ExitOnErr(llvm::orc::JITTargetMachineBuilder::detectHost());
    if (!JTMB.getTargetTriple().isOSLinux())
        llvm::errs() << "Warning: This JIT may not work for platforms other than Linux.\n";

    //
    // Jit compile with -O3
    //
    // this seems to have no effect
    //
    // JTMB.setCodeGenOptLevel(static_cast<llvm::CodeGenOptLevel>(3));

    // Create an LLJIT instance and use a custom object linking layer creator to
    // register the GDBRegistrationListener with our RTDyldObjectLinkingLayer.
    return ExitOnErr(llvm::orc::LLJITBuilder()
        .setJITTargetMachineBuilder(std::move(JTMB))
        .setObjectLinkingLayerCreator([&](llvm::orc::ExecutionSession &ES, const llvm::Triple &TT) {
            auto GetMemMgr = []() {
                return std::make_unique<llvm::SectionMemoryManager>();
            };
            auto ObjLinkingLayer = std::make_unique<llvm::orc::RTDyldObjectLinkingLayer>(ES, std::move(GetMemMgr));

            // Register the event listener.
            ObjLinkingLayer->registerJITEventListener(*llvm::JITEventListener::createGDBRegistrationListener());

            // Make sure the debug info sections aren't stripped.
            ObjLinkingLayer->setProcessAllSections(true);

            return ObjLinkingLayer;
        })
        .create());
}

JIT::JIT() : jit(build_jit()) {}

void JIT::add_IR_module(llvm::orc::ThreadSafeModule && module) {
    ExitOnErr(jit->addIRModule(std::move(module)));
}

void JIT::add_IR_module(llvm::StringRef file_name) {
    auto Ctx = std::make_unique<llvm::LLVMContext>();
    llvm::SMDiagnostic Err;
    std::unique_ptr<llvm::Module> M = llvm::parseIRFile(file_name, Err, *Ctx);
    if (!M) {
        Err.print("JIT IR Read error", llvm::errs());
        return;
    }
    add_IR_module(std::move(llvm::orc::ThreadSafeModule(std::move(M), std::move(Ctx))));
}

llvm::Expected<llvm::orc::ExecutorAddr> JIT::lookup(llvm::StringRef symbol) {
    return ExitOnErr(jit->lookup(symbol));
}

void JIT::run_static_initializer() {
    ExitOnErr(jit->initialize(jit->getMainJITDylib()));
}
void JIT::run_static_deinitializer() {
    ExitOnErr(jit->deinitialize(jit->getMainJITDylib()));
}

void JIT::dump(llvm::raw_ostream & os) {
        os << "--- JIT execution session dump START---\n";
        jit->getExecutionSession().dump(os);
        os << "--- JIT execution session dump END ---\n";
}