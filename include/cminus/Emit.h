#ifndef CMINUS_EMIT_H
#define CMINUS_EMIT_H

#include <string>

namespace llvm {
class Module;
} // namespace llvm

namespace cminus {

/// Run LLVM's standard optimization pipeline. Level 0 leaves the module alone.
bool optimizeModule(llvm::Module &module, unsigned level, std::string &error);

/// Write textual LLVM IR. A path of "-" means standard output.
bool writeIR(llvm::Module &module, const std::string &path, std::string &error);

/// Write a relocatable object file for the host, which can then be linked
/// against the C- runtime.
bool writeObjectFile(llvm::Module &module, const std::string &path,
                     std::string &error);

} // namespace cminus

#endif // CMINUS_EMIT_H
