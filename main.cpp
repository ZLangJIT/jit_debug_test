#include "jit.h"

#ifdef _WIN32
#include <llvm/ExecutionEngine/Orc/Shared/WrapperFunctionUtils.h>
#endif

// GDB and LLDB support debugging of JIT-compiled code by observing calls to __jit_debug_register_code()
// by putting a breakpoint on it, and retrieving the debug info through __jit_debug_descriptor.
// On Linux it suffices for these symbols not to be stripped out, while for Windows a .pdb has to contain
// their information. LLVM defines them, but we don't want a huge .pdb with all LLVM source code's debug
// info. By forward-declaring them here it suffices to compile this file with /Zi.

// JIT_DLL_EXPORT requires complete types, no forward-declaring

extern "C" struct jit_descriptor;
extern "C" struct jit_descriptor __jit_debug_descriptor;

extern "C" JIT_DLL_EXPORT void __jit_debug_register_code();

#ifdef _WIN32

extern "C" JIT_DLL_EXPORT llvm::orc::shared::CWrapperFunctionResult
llvm_orc_registerJITLoaderGDBWrapper(const char *Data, uint64_t Size);

extern "C" JIT_DLL_EXPORT llvm::orc::shared::CWrapperFunctionResult
llvm_orc_registerJITLoaderGDBAllocAction(const char *Data, size_t Size);

#endif

#if false
//===- JITLoaderGDB.h - Register objects via GDB JIT interface -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/ExecutionEngine/Orc/TargetProcess/JITLoaderGDB.h"

#include "llvm/ExecutionEngine/JITSymbol.h"
#include "llvm/Support/BinaryStreamReader.h"
#include "llvm/Support/FormatVariadic.h"

#include <cstdint>
#include <mutex>
#include <utility>

#define DEBUG_TYPE "orc"

// First version as landed in August 2009
static constexpr uint32_t JitDescriptorVersion = 1;

extern "C" {

// We put information about the JITed function in this global, which the
// debugger reads.  Make sure to specify the version statically, because the
// debugger checks the version before we can set it during runtime.
struct jit_descriptor __jit_debug_descriptor = {JitDescriptorVersion, 0,
                                                nullptr, nullptr};

// Debuggers that implement the GDB JIT interface put a special breakpoint in
// this function.
LLVM_ATTRIBUTE_NOINLINE void __jit_debug_register_code() {
  // The noinline and the asm prevent calls to this function from being
  // optimized out.
#if !defined(_MSC_VER)
  asm volatile("" ::: "memory");
#endif
}
}

using namespace llvm;
using namespace llvm::orc;

#define LLVM_DEBUG(x) x

// Register debug object, return error message or null for success.
static void appendJITDebugDescriptor(const char *ObjAddr, size_t Size) {
  LLVM_DEBUG({
    dbgs() << "Adding debug object to GDB JIT interface "
           << formatv("([{0:x16} -- {1:x16}])",
                      reinterpret_cast<uintptr_t>(ObjAddr),
                      reinterpret_cast<uintptr_t>(ObjAddr + Size))
           << "\n";
  });

  jit_code_entry *E = new jit_code_entry;
  E->symfile_addr = ObjAddr;
  E->symfile_size = Size;
  E->prev_entry = nullptr;

  // Serialize rendezvous with the debugger as well as access to shared data.
  static std::mutex JITDebugLock;
  std::lock_guard<std::mutex> Lock(JITDebugLock);

  // Insert this entry at the head of the list.
  jit_code_entry *NextEntry = __jit_debug_descriptor.first_entry;
  E->next_entry = NextEntry;
  if (NextEntry) {
    NextEntry->prev_entry = E;
  }

  __jit_debug_descriptor.first_entry = E;
  __jit_debug_descriptor.relevant_entry = E;
  __jit_debug_descriptor.action_flag = JIT_REGISTER_FN;
}

extern "C" orc::shared::CWrapperFunctionResult
llvm_orc_registerJITLoaderGDBAllocAction(const char *Data, size_t Size) {
  using namespace orc::shared;
  return WrapperFunction<SPSError(SPSExecutorAddrRange, bool)>::handle(
             Data, Size,
             [](ExecutorAddrRange R, bool AutoRegisterCode) {
               appendJITDebugDescriptor(R.Start.toPtr<const char *>(),
                                        R.size());
               // Run into the rendezvous breakpoint.
               if (AutoRegisterCode)
                 __jit_debug_register_code();
               return Error::success();
             })
      .release();
}

extern "C" orc::shared::CWrapperFunctionResult
llvm_orc_registerJITLoaderGDBWrapper(const char *Data, uint64_t Size) {
  using namespace orc::shared;
  return WrapperFunction<SPSError(SPSExecutorAddrRange, bool)>::handle(
             Data, Size,
             [](ExecutorAddrRange R, bool AutoRegisterCode) {
               appendJITDebugDescriptor(R.Start.toPtr<const char *>(),
                                        R.size());
               // Run into the rendezvous breakpoint.
               if (AutoRegisterCode)
                 __jit_debug_register_code();
               return Error::success();
             })
      .release();
}
#endif

extern "C" JIT_DLL_EXPORT int main(int argc, char *argv[]);

#define STR_(x) #x
#define STR(x) STR_(x)


int main(int argc, char *argv[]) {

    JIT::main_llvm_init main_init(argc, const_cast<const char**>(argv));
    
    JIT jit = JIT(true);
    
    llvm::outs() << "invoking [ " STR(CLANG_EXE) " jit_code.c -emit-llvm -O0 -g3 -Xclang -triple -Xclang " STR(jit_target_triple) " -S -o tmp.ll" " ]\n";
    
    system(STR(CLANG_EXE) " jit_code.c -emit-llvm -O0 -g3 -Xclang -triple -Xclang " STR(jit_target_triple) " -S -o tmp.ll");
    
    jit.add_IR_module("tmp.ll");
    
    int (*main_func)(void) = jit.lookup_as_pointer<int(void)>("j");
   
    int res = main_func();
    llvm::outs() << "j() = " << res << "\n";
    
    return 0;
}