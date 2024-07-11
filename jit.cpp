#include <llvm/ADT/StringMap.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/ExecutionEngine/JITEventListener.h>
#include <llvm/ExecutionEngine/JITSymbol.h>
#include <llvm/ExecutionEngine/JITLink/JITLinkMemoryManager.h>
#include <llvm/ExecutionEngine/Orc/Debugging/DebuggerSupport.h>
#include <llvm/ExecutionEngine/Orc/Debugging/DebuggerSupportPlugin.h>
#include <llvm/ExecutionEngine/Orc/DebugObjectManagerPlugin.h>
#include <llvm/ExecutionEngine/Orc/EPCEHFrameRegistrar.h>
#include <llvm/ExecutionEngine/Orc/ExecutionUtils.h>
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/ExecutionEngine/Orc/RTDyldObjectLinkingLayer.h>
#include <llvm/ExecutionEngine/Orc/TargetProcess/JITLoaderGDB.h>
#include <llvm/ExecutionEngine/Orc/TargetProcess/RegisterEHFrames.h>
#include <llvm/ExecutionEngine/Orc/TargetProcess/TargetExecutionUtils.h>
#include <llvm/ExecutionEngine/Orc/ThreadSafeModule.h>
#include <llvm/ExecutionEngine/SectionMemoryManager.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/PrettyStackTrace.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/TargetSelect.h>

#include "jit.h"
#include <stdio.h>

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

    llvm::cl::ParseCommandLineOptions(argc, argv, "JIT");
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

extern llvm::orc::shared::CWrapperFunctionResult llvm_orc_registerJITLoaderGDBWrapper(const char *Data, size_t Size);
extern llvm::orc::shared::CWrapperFunctionResult llvm_orc_registerJITLoaderGDBAllocAction(const char *Data, size_t Size);
}

LLVM_ATTRIBUTE_USED void linkComponents() {
  llvm::errs() << (void *)&llvm_orc_registerEHFrameSectionWrapper
         << (void *)&llvm_orc_deregisterEHFrameSectionWrapper
         << (void *)&llvm_orc_registerJITLoaderGDBWrapper
         << (void *)&llvm_orc_registerJITLoaderGDBAllocAction;
}

std::unique_ptr<llvm::orc::LLJIT> build_jit() {
    
    auto JTMB = llvm::orc::JITTargetMachineBuilder(llvm::Triple(target_triple));
    
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
    std::unique_ptr<llvm::orc::LLJIT> jit = ExitOnErr(llvm::orc::LLJITBuilder()
        .setJITTargetMachineBuilder(std::move(JTMB))
        .setObjectLinkingLayerCreator([&](llvm::orc::ExecutionSession &ES, const llvm::Triple &TT) {
          
            auto EPC = ExitOnErr(llvm::orc::SelfExecutorProcessControl::Create(std::make_shared<llvm::orc::SymbolStringPool>()));
            
            auto ObjLinkingLayer = std::make_unique<llvm::orc::ObjectLinkingLayer>(ES, EPC->getMemMgr());
            
            ObjLinkingLayer->addPlugin(std::make_unique<llvm::orc::EHFrameRegistrationPlugin>(ES, ExitOnErr(llvm::orc::EPCEHFrameRegistrar::Create(ES))));
            
    if (TT.isOSBinFormatMachO())
        ObjLinkingLayer->addPlugin(ExitOnErr(llvm::orc::GDBJITDebugInfoRegistrationPlugin::Create(ES, JD, TM->getTargetTripl));
        
#ifndef _COMPILER_ASAN_ENABLED_
    else if (TT.isOSBinFormatELF())
        //EPCDebugObjectRegistrar doesn't take a JITDylib, so we have to directly provide the call address
        ObjLinkingLayer.addPlugin(std::make_unique<llvm::orc::DebugObjectManagerPlugin>(ES, std::make_unique<llvm::orc::EPCDebugObjectRegistrar>(ES, llvm::orc::ExecutorAddr::fromPtr(&llvm_orc_registerJITLoaderGDBWrapper))));
#endif
            
            // Register the event listener.
            //ObjLinkingLayer->registerJITEventListener(*llvm::JITEventListener::createGDBRegistrationListener());
            
            // Make sure the debug info sections aren't stripped.
            //ObjLinkingLayer->setProcessAllSections(true);

            llvm::outs() << "JIT ObjLinkingLayer created.\n";
            
            return ObjLinkingLayer;
        })
        .setPrePlatformSetup([](llvm::orc::LLJIT &J) {
            // Try to enable debugging of JIT'd code (only works with JITLink for
            // ELF and MachO).
            if (auto E = llvm::orc::enableDebuggerSupport(J)) {
              llvm::errs() << "JIT failed to enable debugger support, Debug Information may be unavailable for JIT compiled code.\nError: " << E << "\n";
            }
            return llvm::Error::success();
        })
        .create());
        
        llvm::outs() << "JIT created.\n";
        
        return jit;
}

JIT::JIT() : jit(build_jit()) {}

void JIT::add_IR_module(llvm::orc::ThreadSafeModule && module) {
    llvm::outs() << "JIT addIRModule being called.\n";
    ExitOnErr(jit->addIRModule(std::move(module)));
    llvm::outs() << "JIT addIRModule called.\n";
}

void JIT::add_IR_module(llvm::StringRef file_name) {
    auto Ctx = std::make_unique<llvm::LLVMContext>();
    std::unique_ptr<llvm::Module> M;
    {
        llvm::SMDiagnostic Err;
        M = llvm::parseIRFile(file_name, Err, *Ctx);
        if (!M) {
            Err.print("JIT IR Read error.\n", llvm::errs());
            return;
        }
    }
    
    auto JTMB = llvm::orc::JITTargetMachineBuilder(llvm::Triple(target_triple));
    
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
    
    auto expected = JTMB.createTargetMachine();
    
    if (!expected) {
        llvm::errs() << "JIT IR createTargetMachine error.\n";
        return;
    }
    
    M->setDataLayout((*expected)->createDataLayout());
    
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