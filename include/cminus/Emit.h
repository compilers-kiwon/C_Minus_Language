#ifndef CMINUS_EMIT_H
#define CMINUS_EMIT_H

#include <memory>
#include <string>

namespace llvm {
class Module;
} // namespace llvm

namespace cminus {

/// Which machine to generate code for, and the details a triple cannot carry.
struct TargetSpec {
  /// Empty means the host.
  std::string triple;
  /// Empty means "generic".
  std::string cpu;
  /// LLVM feature list such as "+m,+a,+f,+d,+c". Empty means a default
  /// suitable for the triple.
  std::string features;
  /// Target ABI such as "lp64d". Empty means a default suitable for the
  /// triple.
  std::string abi;
  unsigned optLevel = 0;
};

/// The machine code is being produced for.
///
/// This has to exist before IR generation, not just before object emission:
/// the data layout tells the generator how wide a pointer is, and stamping it
/// on the module afterwards would be too late for the decisions that depend
/// on it.
class Target {
public:
  /// Resolve a target. Returns null and fills `error` when the triple names a
  /// back end LLVM was not built with.
  static std::unique_ptr<Target> create(const TargetSpec &spec,
                                        std::string &error);
  ~Target();

  Target(const Target &) = delete;
  Target &operator=(const Target &) = delete;

  /// The normalized triple, which may differ from what was asked for.
  const std::string &triple() const;
  /// Textual data layout, to stamp on the module before generating IR.
  const std::string &dataLayout() const;
  /// What was actually used, once the defaults were filled in.
  const std::string &cpu() const;
  const std::string &features() const;
  const std::string &abi() const;

  bool writeObjectFile(llvm::Module &module, const std::string &path,
                       std::string &error) const;

private:
  struct Impl;
  explicit Target(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};

/// The triple LLVM was configured for on this machine.
std::string hostTriple();

/// Run LLVM's standard optimization pipeline. Level 0 leaves the module alone.
bool optimizeModule(llvm::Module &module, unsigned level, std::string &error);

/// Write textual LLVM IR. A path of "-" means standard output.
bool writeIR(llvm::Module &module, const std::string &path, std::string &error);

} // namespace cminus

#endif // CMINUS_EMIT_H
