#include "cminus/Link.h"

#include <cstdlib>
#include <vector>

#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/raw_ostream.h"

namespace cminus {

namespace {

const char *const kRuntimeName = "libcminus_rt.a";

/// Drivers to try when the user names none.
const char *const kDefaultDrivers[] = {"cc", "clang", "gcc"};

std::string environmentValue(const char *name) {
  const char *value = std::getenv(name);
  return value ? std::string(value) : std::string();
}

bool isFile(const llvm::Twine &path) {
  return llvm::sys::fs::is_regular_file(path);
}

} // namespace

std::string findRuntime(const char *argv0, void *mainAddr) {
  const std::string fromEnvironment = environmentValue("CMINUS_RUNTIME");
  if (!fromEnvironment.empty())
    return fromEnvironment;

  const std::string executable = llvm::sys::fs::getMainExecutable(argv0, mainAddr);
  if (executable.empty())
    return {};

  const llvm::StringRef directory = llvm::sys::path::parent_path(executable);

  // Beside the compiler, which is where the build tree puts it.
  llvm::SmallString<128> beside(directory);
  llvm::sys::path::append(beside, kRuntimeName);
  if (isFile(beside))
    return std::string(beside);

  // ../lib, which is how an unpacked release is laid out.
  llvm::SmallString<128> inLib(directory);
  llvm::sys::path::append(inLib, "..", "lib", kRuntimeName);
  if (isFile(inLib))
    return std::string(inLib);

  return {};
}

bool linkExecutable(const LinkOptions &options, std::string &error) {
  error.clear();

  // Resolve the driver.
  std::string driverPath;
  std::string driverName = options.driver;
  if (driverName.empty())
    driverName = environmentValue("CMINUS_CC");

  if (!driverName.empty()) {
    if (auto found = llvm::sys::findProgramByName(driverName)) {
      driverPath = *found;
    } else {
      error = "cannot find the C compiler driver '" + driverName + "'";
      return false;
    }
  } else {
    for (const char *candidate : kDefaultDrivers) {
      if (auto found = llvm::sys::findProgramByName(candidate)) {
        driverName = candidate;
        driverPath = *found;
        break;
      }
    }
    if (driverPath.empty()) {
      error = "no C compiler driver found on PATH (looked for cc, clang and "
              "gcc); name one with --cc or $CMINUS_CC, or use -c and link "
              "separately";
      return false;
    }
  }

  if (options.runtimePath.empty()) {
    error = "cannot find " + std::string(kRuntimeName) +
            "; name it with --runtime or $CMINUS_RUNTIME";
    return false;
  }
  if (!isFile(options.runtimePath)) {
    error = "no runtime archive at '" + options.runtimePath + "'";
    return false;
  }

  // Prefer lld when it is installed: it is part of the same LLVM toolchain
  // the rest of the compiler is built on. The driver still assembles the link
  // line, because that is the part lld does not know how to do.
  std::string useLinkerFlag;
  if (options.haveUseLinker) {
    if (!options.useLinker.empty())
      useLinkerFlag = "-fuse-ld=" + options.useLinker;
  } else if (llvm::sys::findProgramByName("ld.lld")) {
    useLinkerFlag = "-fuse-ld=lld";
  }

  std::vector<llvm::StringRef> args;
  args.push_back(driverName);
  if (!useLinkerFlag.empty())
    args.push_back(useLinkerFlag);
  args.push_back(options.objectPath);
  args.push_back(options.runtimePath);
  args.push_back("-o");
  args.push_back(options.outputPath);

  if (options.verbose) {
    llvm::errs() << driverPath;
    for (std::size_t i = 1; i < args.size(); ++i)
      llvm::errs() << ' ' << args[i];
    llvm::errs() << '\n';
  }

  std::string executeError;
  bool executionFailed = false;
  const int status = llvm::sys::ExecuteAndWait(
      driverPath, args, /*Env=*/std::nullopt, /*Redirects=*/{},
      /*SecondsToWait=*/0, /*MemoryLimit=*/0, &executeError, &executionFailed);

  if (executionFailed) {
    error = "could not run '" + driverPath + "'";
    if (!executeError.empty())
      error += ": " + executeError;
    return false;
  }
  if (status != 0) {
    error = "'" + driverName + "' failed while linking (exit status " +
            std::to_string(status) + ")";
    return false;
  }
  return true;
}

} // namespace cminus
