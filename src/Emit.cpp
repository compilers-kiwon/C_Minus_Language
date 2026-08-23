#include "cminus/Emit.h"

#include <memory>
#include <system_error>

#include "llvm/Analysis/CGSCCPassManager.h"
#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/Triple.h"

namespace cminus {

bool optimizeModule(llvm::Module &module, unsigned level, std::string &error) {
  error.clear();
  if (level == 0)
    return true;

  llvm::OptimizationLevel opt = llvm::OptimizationLevel::O2;
  if (level == 1)
    opt = llvm::OptimizationLevel::O1;
  else if (level >= 3)
    opt = llvm::OptimizationLevel::O3;

  llvm::LoopAnalysisManager loopAnalyses;
  llvm::FunctionAnalysisManager functionAnalyses;
  llvm::CGSCCAnalysisManager cgsccAnalyses;
  llvm::ModuleAnalysisManager moduleAnalyses;

  llvm::PassBuilder builder;
  builder.registerModuleAnalyses(moduleAnalyses);
  builder.registerCGSCCAnalyses(cgsccAnalyses);
  builder.registerFunctionAnalyses(functionAnalyses);
  builder.registerLoopAnalyses(loopAnalyses);
  builder.crossRegisterProxies(loopAnalyses, functionAnalyses, cgsccAnalyses,
                               moduleAnalyses);

  llvm::ModulePassManager passes = builder.buildPerModuleDefaultPipeline(opt);
  passes.run(module, moduleAnalyses);
  return true;
}

bool writeIR(llvm::Module &module, const std::string &path,
             std::string &error) {
  error.clear();
  if (path == "-") {
    module.print(llvm::outs(), nullptr);
    return true;
  }

  std::error_code code;
  llvm::raw_fd_ostream out(path, code, llvm::sys::fs::OF_Text);
  if (code) {
    error = "cannot write '" + path + "': " + code.message();
    return false;
  }
  module.print(out, nullptr);
  return true;
}

bool writeObjectFile(llvm::Module &module, const std::string &path,
                     std::string &error) {
  error.clear();

  llvm::InitializeNativeTarget();
  llvm::InitializeNativeTargetAsmPrinter();

  const llvm::Triple triple(llvm::sys::getDefaultTargetTriple());
  std::string lookupError;
  const llvm::Target *target =
      llvm::TargetRegistry::lookupTarget(triple, lookupError);
  if (!target) {
    error = "no back end for " + triple.str() + ": " + lookupError;
    return false;
  }

  llvm::TargetOptions options;
  std::unique_ptr<llvm::TargetMachine> machine(target->createTargetMachine(
      triple, "generic", "", options, llvm::Reloc::PIC_));
  if (!machine) {
    error = "cannot create a target machine for " + triple.str();
    return false;
  }

  // Only the object file is host-specific; textual IR is left portable.
  module.setDataLayout(machine->createDataLayout());
  module.setTargetTriple(triple);

  std::error_code code;
  llvm::raw_fd_ostream out(path, code, llvm::sys::fs::OF_None);
  if (code) {
    error = "cannot write '" + path + "': " + code.message();
    return false;
  }

  llvm::legacy::PassManager passes;
  if (machine->addPassesToEmitFile(passes, out, nullptr,
                                   llvm::CodeGenFileType::ObjectFile)) {
    error = "the target cannot emit an object file";
    return false;
  }
  passes.run(module);
  out.flush();
  return true;
}

} // namespace cminus
