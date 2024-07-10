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

// https://github.com/NVIDIA/warp/blob/main/warp/native/clang/clang.cpp

#if defined (_WIN32)
	// Windows defaults to using the COFF binary format (aka. "msvc" in the target triple).
	// Override it to use the ELF format to support DWARF debug info, but keep using the
	// Microsoft calling convention (see also https://llvm.org/docs/DebuggingJITedCode.html).
	#define target_triple "x86_64-pc-windows-elf"
#else
	#define target_triple LLVM_DEFAULT_TARGET_TRIPLE
#endif

extern "C" {

// GDB and LLDB support debugging of JIT-compiled code by observing calls to __jit_debug_register_code()
// by putting a breakpoint on it, and retrieving the debug info through __jit_debug_descriptor.
// On Linux it suffices for these symbols not to be stripped out, while for Windows a .pdb has to contain
// their information. LLVM defines them, but we don't want a huge .pdb with all LLVM source code's debug
// info. By forward-declaring them here it suffices to compile this file with /Zi.

extern struct jit_descriptor __jit_debug_descriptor;
extern void __jit_debug_register_code();

}

std::unique_ptr<llvm::orc::LLJIT> build_jit() {
    std::string error;
    const llvm::Target* target = llvm::TargetRegistry::lookupTarget(target_triple, error);
    
    auto JTMB = ExitOnErr(llvm::orc::JITTargetMachineBuilder(target_triple));
    
    // Retrieve host CPU name and sub-target features and add them to builder.
    // codegen opt level are kept to default values.
    llvm::StringMap<bool> FeatureMap;
    llvm::sys::getHostCPUFeatures(FeatureMap);
    for (auto &Feature : FeatureMap)
        JTMB.getFeatures().AddFeature(Feature.first(), Feature.second);
 
    JTMB.setCPU(std::string(llvm::sys::getHostCPUName()));
    
    // Position Independent Code(
    JTMB.setRelocationModel(llvm::Reloc::PIC_);
    
    // Don't make assumptions about displacement sizes
    JTMB.setCodeModel(llvm::CodeModel::Large);

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