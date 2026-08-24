#include "cminus/Emit.h"

#include <memory>
#include <system_error>
#include <utility>

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

namespace {

llvm::CodeGenOptLevel codeGenLevel(unsigned level) {
  switch (level) {
  case 0:  return llvm::CodeGenOptLevel::None;
  case 1:  return llvm::CodeGenOptLevel::Less;
  case 2:  return llvm::CodeGenOptLevel::Default;
  default: return llvm::CodeGenOptLevel::Aggressive;
  }
}

/// Register every back end LLVM was built with, not just the host's, so that
/// --target can name any of them.
void initializeTargets() {
  static const bool once = [] {
    llvm::InitializeAllTargetInfos();
    llvm::InitializeAllTargets();
    llvm::InitializeAllTargetMCs();
    llvm::InitializeAllAsmPrinters();
    return true;
  }();
  (void)once;
}

/// Fill in what a triple cannot say.
///
/// A triple carries the float ABI for ARM -- that is what `gnueabihf` means --
/// but RISC-V keeps it in a separate -mabi, so LLVM falls back to its bare
/// default of soft float with no extensions. No distribution builds its libc
/// that way, and the linker rejects the mixture outright:
///
///   can't link soft-float modules with double-float modules
///
/// Linux RISC-V userspace is rv64gc/lp64d, which is what clang defaults to
/// and what this matches.
void applyTripleDefaults(const llvm::Triple &triple, std::string &features,
                         std::string &abi) {
  if (!triple.isOSLinux())
    return;
  if (triple.isRISCV64()) {
    if (features.empty())
      features = "+m,+a,+f,+d,+c";
    if (abi.empty())
      abi = "lp64d";
  } else if (triple.isRISCV32()) {
    if (features.empty())
      features = "+m,+a,+f,+d,+c";
    if (abi.empty())
      abi = "ilp32d";
  }
}

} // namespace

struct Target::Impl {
  std::unique_ptr<llvm::TargetMachine> machine;
  std::string triple;
  std::string dataLayout;
  std::string cpu;
  std::string features;
  std::string abi;
};

Target::Target(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
Target::~Target() = default;

const std::string &Target::triple() const { return impl_->triple; }
const std::string &Target::dataLayout() const { return impl_->dataLayout; }
const std::string &Target::cpu() const { return impl_->cpu; }
const std::string &Target::features() const { return impl_->features; }
const std::string &Target::abi() const { return impl_->abi; }

std::unique_ptr<Target> Target::create(const TargetSpec &spec,
                                       std::string &error) {
  error.clear();
  initializeTargets();

  const llvm::Triple triple(spec.triple.empty()
                                ? llvm::sys::getDefaultTargetTriple()
                                : llvm::Triple::normalize(spec.triple));

  std::string lookupError;
  const llvm::Target *target =
      llvm::TargetRegistry::lookupTarget(triple, lookupError);
  if (!target) {
    error = "no back end for target '" + triple.str() + "': " + lookupError;
    return nullptr;
  }

  const std::string cpu = spec.cpu.empty() ? "generic" : spec.cpu;
  std::string features = spec.features;
  std::string abi = spec.abi;
  applyTripleDefaults(triple, features, abi);

  llvm::TargetOptions options;
  options.MCOptions.ABIName = abi;
  // Position-independent code, which is what every mainstream toolchain links
  // by default.
  std::unique_ptr<llvm::TargetMachine> machine(target->createTargetMachine(
      triple, cpu, features, options, llvm::Reloc::PIC_, std::nullopt,
      codeGenLevel(spec.optLevel)));
  if (!machine) {
    error = "cannot create a target machine for '" + triple.str() + "'";
    return nullptr;
  }

  auto impl = std::make_unique<Impl>();
  impl->triple = triple.str();
  impl->dataLayout = machine->createDataLayout().getStringRepresentation();
  impl->cpu = cpu;
  impl->features = features;
  impl->abi = abi;
  impl->machine = std::move(machine);
  return std::unique_ptr<Target>(new Target(std::move(impl)));
}

bool Target::writeObjectFile(llvm::Module &module, const std::string &path,
                             std::string &error) const {
  error.clear();

  std::error_code code;
  llvm::raw_fd_ostream out(path, code, llvm::sys::fs::OF_None);
  if (code) {
    error = "cannot write '" + path + "': " + code.message();
    return false;
  }

  llvm::legacy::PassManager passes;
  if (impl_->machine->addPassesToEmitFile(passes, out, nullptr,
                                          llvm::CodeGenFileType::ObjectFile)) {
    error = "the '" + impl_->triple + "' back end cannot emit an object file";
    return false;
  }
  passes.run(module);
  out.flush();
  return true;
}

std::string hostTriple() {
  return llvm::Triple::normalize(llvm::sys::getDefaultTargetTriple());
}

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

} // namespace cminus
