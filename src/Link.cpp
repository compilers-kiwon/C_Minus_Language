#include "cminus/Link.h"

#include <cstdlib>
#include <string>
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

/// Drivers to try when the caller names none.
const char *const kDefaultDrivers[] = {"cc", "clang", "gcc"};

std::string environmentValue(const char *name) {
  const char *value = std::getenv(name);
  return value ? std::string(value) : std::string();
}

bool isFile(const llvm::Twine &path) {
  return llvm::sys::fs::is_regular_file(path);
}

/// Split a driver command into words.
///
/// A build system hands the C driver over as a command line rather than a
/// name -- a cross toolchain is useless without its sysroot, so
/// `aarch64-linux-gnu-gcc --sysroot=/path -mcpu=cortex-a53` arrives as one
/// string. Quotes are honoured so a path containing a space survives.
std::vector<std::string> splitCommand(const std::string &command) {
  std::vector<std::string> words;
  std::string current;
  bool inWord = false;
  char quote = '\0';

  for (const char c : command) {
    if (quote != '\0') {
      if (c == quote)
        quote = '\0';
      else
        current += c;
      continue;
    }
    if (c == '"' || c == '\'') {
      quote = c;
      inWord = true;
      continue;
    }
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
      if (inWord) {
        words.push_back(current);
        current.clear();
        inWord = false;
      }
      continue;
    }
    current += c;
    inWord = true;
  }
  if (inWord)
    words.push_back(current);
  return words;
}

} // namespace

std::string findRuntime(const char *argv0, void *mainAddr) {
  const std::string fromEnvironment = environmentValue("CMINUS_RUNTIME");
  if (!fromEnvironment.empty())
    return fromEnvironment;

  const std::string executable =
      llvm::sys::fs::getMainExecutable(argv0, mainAddr);
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

  // A driver the caller named -- on the command line or in the environment --
  // is taken at its word, arguments and all.
  std::vector<std::string> driverWords;
  bool namedByCaller = false;
  if (!options.driver.empty()) {
    driverWords = splitCommand(options.driver);
    namedByCaller = true;
  } else {
    const std::string fromEnvironment = environmentValue("CMINUS_CC");
    if (!fromEnvironment.empty()) {
      driverWords = splitCommand(fromEnvironment);
      namedByCaller = true;
    }
  }

  if (namedByCaller && driverWords.empty()) {
    error = "the C compiler driver was given as an empty command";
    return false;
  }

  if (driverWords.empty()) {
    for (const char *candidate : kDefaultDrivers) {
      if (llvm::sys::findProgramByName(candidate)) {
        driverWords.emplace_back(candidate);
        break;
      }
    }
    if (driverWords.empty()) {
      error = "no C compiler driver found on PATH (looked for cc, clang and "
              "gcc); name one with --cc or $CMINUS_CC, or use -c and link "
              "separately";
      return false;
    }
  }

  auto resolved = llvm::sys::findProgramByName(driverWords.front());
  if (!resolved) {
    error = "cannot find the C compiler driver '" + driverWords.front() + "'";
    return false;
  }
  const std::string driverPath = *resolved;

  if (options.runtimePath.empty()) {
    error = "cannot find " + std::string(kRuntimeName) +
            "; name it with --runtime or $CMINUS_RUNTIME";
    return false;
  }
  if (!isFile(options.runtimePath)) {
    error = "no runtime archive at '" + options.runtimePath + "'";
    return false;
  }

  // Prefer lld when it is installed, since it is part of the same LLVM
  // toolchain as the rest of the compiler. Not when the caller named the
  // driver, though: a cross toolchain was chosen deliberately and injecting a
  // linker into it would be presumptuous. --use-ld= overrides either way.
  std::string useLinkerFlag;
  if (options.haveUseLinker) {
    if (!options.useLinker.empty())
      useLinkerFlag = "-fuse-ld=" + options.useLinker;
  } else if (!namedByCaller && llvm::sys::findProgramByName("ld.lld")) {
    useLinkerFlag = "-fuse-ld=lld";
  }

  std::vector<llvm::StringRef> args;
  for (const std::string &word : driverWords)
    args.push_back(word);
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
    error = "'" + driverWords.front() + "' failed while linking (exit status " +
            std::to_string(status) + ")";
    return false;
  }
  return true;
}

} // namespace cminus
