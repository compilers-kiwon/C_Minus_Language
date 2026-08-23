#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "llvm/ADT/SmallString.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/FileSystem.h"

#include "cminus/AST.h"
#include "cminus/Diagnostic.h"
#include "cminus/Emit.h"
#include "cminus/IRGen.h"
#include "cminus/Lexer.h"
#include "cminus/Link.h"
#include "cminus/Parser.h"
#include "cminus/SemanticAnalyzer.h"
#include "cminus/Token.h"

#if defined(_WIN32)
#include <io.h>
#define CMINUS_ISATTY(fd) _isatty(fd)
#define CMINUS_STDERR_FD 2
#else
#include <unistd.h>
#define CMINUS_ISATTY(fd) isatty(fd)
#define CMINUS_STDERR_FD STDERR_FILENO
#endif

namespace {

/// A symbol that lives in this executable, so that LLVM can work out the path
/// of the running binary and look for the runtime next to it.
int mainExecutableAnchor = 0;

/// The last stage the driver should run. Each --dump-* flag stops the
/// pipeline right after the stage it prints, so a broken later stage cannot
/// hide the output of an earlier one.
enum class Stage { Scan, Parse, Analyze, Emit };

/// What the compiler should leave behind.
enum class OutputKind { Executable, Object, LLVMIR };

struct Options {
  std::string inputPath;
  std::string outputPath; // empty means "pick a default"
  std::string targetTriple; // empty means the host
  Stage stopAfter = Stage::Emit;
  OutputKind output = OutputKind::Executable;
  bool dumpTokens = false;
  bool dumpAST = false;
  bool dumpSymbols = false;
  unsigned optLevel = 0;
  bool indexChecks = true;
  bool saveTemps = false;
  bool verbose = false;
  std::string driver;
  std::string useLinker;
  bool haveUseLinker = false;
  std::string runtimePath;
  bool useColor = true;
  bool colorForced = false;
};

void printUsage(std::ostream &os, const char *argv0) {
  os << "usage: " << argv0 << " [options] <file.cm>\n"
     << "\n"
     << "By default the program is compiled and linked into an executable.\n"
     << "\n"
     << "Output:\n"
     << "  -c                Compile only; write an object file\n"
     << "  --emit-llvm       Write textual LLVM IR instead of linking\n"
     << "  -o <file>         Output path (default a.out, or <input>.o with -c;\n"
     << "                    '-' means standard output)\n"
     << "  -O0 -O1 -O2 -O3   Optimization level (default -O0)\n"
     << "\n"
     << "Cross compilation:\n"
     << "  --target <triple> Generate code for <triple> instead of the host,\n"
     << "                    e.g. aarch64-linux-gnu or riscv64-linux-gnu\n"
     << "\n"
     << "Linking:\n"
     << "  --cc <command>    C driver used to link. May carry arguments, as a\n"
     << "                    cross toolchain needs its sysroot (default\n"
     << "                    $CMINUS_CC, else cc, clang or gcc)\n"
     << "  --use-ld=<name>   Pass -fuse-ld=<name> to the driver; lld is used\n"
     << "                    by default when installed and --cc was not given\n"
     << "  --runtime <file>  Path to libcminus_rt.a, which must be built for\n"
     << "                    the target (default $CMINUS_RUNTIME, else next to\n"
     << "                    the compiler)\n"
     << "  -save-temps       Keep the intermediate object file\n"
     << "  -v                Print the link command\n"
     << "\n"
     << "Stages:\n"
     << "  --dump-tokens     Print the token stream and stop\n"
     << "  --dump-ast        Print the parse tree and stop\n"
     << "  --dump-symbols    Print the symbol table and stop\n"
     << "\n"
     << "Other:\n"
     << "  -fno-index-check  Omit the negative-subscript checks\n"
     << "  --color           Force colored diagnostics\n"
     << "  --no-color        Disable colored diagnostics\n"
     << "  -h, --help        Show this message\n";
}

bool readFile(const std::string &path, std::string &out) {
  std::ifstream in(path, std::ios::binary);
  if (!in)
    return false;
  std::ostringstream buf;
  buf << in.rdbuf();
  out = buf.str();
  return true;
}

bool environmentIsSet(const char *name) {
  const char *value = std::getenv(name);
  return value != nullptr && *value != '\0';
}

/// `dir/prog.cm` with extension `.o` becomes `dir/prog.o`.
std::string replaceExtension(const std::string &path, const char *extension) {
  const std::size_t slash = path.find_last_of("/\\");
  const std::size_t dot = path.find_last_of('.');
  const bool hasExtension =
      dot != std::string::npos && (slash == std::string::npos || dot > slash);
  return (hasExtension ? path.substr(0, dot) : path) + extension;
}

void dumpTokens(const std::vector<cminus::Token> &tokens) {
  for (const cminus::Token &tok : tokens) {
    std::string detail;
    if (tok.kind == cminus::TokenKind::Number)
      detail = tok.lexeme + "  (= " + std::to_string(tok.value) + ")";
    else if (tok.kind != cminus::TokenKind::EndOfFile)
      detail = tok.lexeme;

    const char *name = cminus::tokenKindName(tok.kind);
    if (detail.empty())
      std::printf("%4d:%-4d %s\n", tok.loc.line, tok.loc.column, name);
    else
      std::printf("%4d:%-4d %-10s %s\n", tok.loc.line, tok.loc.column, name,
                  detail.c_str());
  }
}

/// Flush every diagnostic collected so far and report whether to stop.
int finish(const cminus::DiagnosticEngine &diags, bool useColor) {
  // Dumps go to stdout and diagnostics to stderr. Flushing here keeps the two
  // in a fixed order when both are redirected to the same place, which the
  // golden-file tests rely on.
  std::cout.flush();
  diags.print(std::cerr, useColor);
  diags.printSummary(std::cerr, useColor);
  return diags.hasErrors() ? 1 : 0;
}

/// Compile to an object file and hand it, plus the runtime, to the linker.
bool buildExecutable(llvm::Module &module, const cminus::Target &target,
                     const Options &opts, const char *argv0,
                     const std::string &output, std::string &error) {
  // With -save-temps the object lands where -c would have put it; otherwise
  // it is a temporary that is removed once the link succeeds.
  std::string objectPath;
  bool temporary = false;
  if (opts.saveTemps) {
    objectPath = replaceExtension(opts.inputPath, ".o");
  } else {
    llvm::SmallString<128> path;
    if (std::error_code code =
            llvm::sys::fs::createTemporaryFile("cmc", "o", path)) {
      error = "cannot create a temporary object file: " + code.message();
      return false;
    }
    objectPath = std::string(path);
    temporary = true;
  }

  if (!target.writeObjectFile(module, objectPath, error)) {
    if (temporary)
      llvm::sys::fs::remove(objectPath);
    return false;
  }

  cminus::LinkOptions link;
  link.objectPath = objectPath;
  link.outputPath = output;
  link.runtimePath = opts.runtimePath.empty()
                         ? cminus::findRuntime(argv0, &mainExecutableAnchor)
                         : opts.runtimePath;
  link.driver = opts.driver;
  link.useLinker = opts.useLinker;
  link.haveUseLinker = opts.haveUseLinker;
  link.verbose = opts.verbose;

  const bool linked = cminus::linkExecutable(link, error);
  if (temporary)
    llvm::sys::fs::remove(objectPath);
  return linked;
}

/// Linking for another machine needs a driver and a runtime built for it.
/// The ones found automatically belong to the host and would fail with an
/// object-format mismatch that explains nothing, so say so first.
///
/// `triple` is what the user asked for rather than LLVM's normalized form:
/// toolchain packages are named after the former, so `aarch64-linux-gnu-gcc`
/// exists while `aarch64-unknown-linux-gnu-gcc` does not.
bool checkCrossLinkInputs(const Options &opts, const std::string &triple,
                          std::string &error) {
  if (opts.driver.empty() && !environmentIsSet("CMINUS_CC")) {
    error = "linking for '" + triple +
            "' needs a matching C driver; pass --cc \"" + triple +
            "-gcc --sysroot=<path>\", or use -c and link separately";
    return false;
  }
  if (opts.runtimePath.empty() && !environmentIsSet("CMINUS_RUNTIME")) {
    error = "linking for '" + triple +
            "' needs a runtime built for it; pass --runtime <path to "
            "libcminus_rt.a>, or use -c and link separately";
    return false;
  }
  return true;
}

} // namespace

int main(int argc, char **argv) {
  Options opts;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "-h" || arg == "--help") {
      printUsage(std::cout, argv[0]);
      return 0;
    }
    if (arg == "--dump-tokens") {
      opts.dumpTokens = true;
      opts.stopAfter = Stage::Scan;
    } else if (arg == "--dump-ast") {
      opts.dumpAST = true;
      if (opts.stopAfter != Stage::Scan)
        opts.stopAfter = Stage::Parse;
    } else if (arg == "--dump-symbols") {
      opts.dumpSymbols = true;
      if (opts.stopAfter == Stage::Emit)
        opts.stopAfter = Stage::Analyze;
    } else if (arg == "--emit-llvm") {
      opts.output = OutputKind::LLVMIR;
    } else if (arg == "-c") {
      opts.output = OutputKind::Object;
    } else if (arg == "-o" || arg == "--cc" || arg == "--runtime" ||
               arg == "--target" || arg == "-target") {
      if (i + 1 >= argc) {
        std::cerr << "cmc: error: '" << arg << "' needs an argument\n";
        return 2;
      }
      const std::string value = argv[++i];
      if (arg == "-o")
        opts.outputPath = value;
      else if (arg == "--cc")
        opts.driver = value;
      else if (arg == "--runtime")
        opts.runtimePath = value;
      else
        opts.targetTriple = value;
    } else if (arg.rfind("--target=", 0) == 0) {
      opts.targetTriple = arg.substr(std::string("--target=").size());
    } else if (arg.rfind("--use-ld=", 0) == 0) {
      opts.useLinker = arg.substr(std::string("--use-ld=").size());
      opts.haveUseLinker = true;
    } else if (arg == "-O0" || arg == "-O1" || arg == "-O2" || arg == "-O3") {
      opts.optLevel = static_cast<unsigned>(arg[2] - '0');
    } else if (arg == "-save-temps") {
      opts.saveTemps = true;
    } else if (arg == "-v") {
      opts.verbose = true;
    } else if (arg == "-fno-index-check") {
      opts.indexChecks = false;
    } else if (arg == "--color") {
      opts.useColor = true;
      opts.colorForced = true;
    } else if (arg == "--no-color") {
      opts.useColor = false;
      opts.colorForced = true;
    } else if (!arg.empty() && arg[0] == '-' && arg != "-") {
      std::cerr << "cmc: error: unknown option '" << arg << "'\n";
      printUsage(std::cerr, argv[0]);
      return 2;
    } else if (opts.inputPath.empty()) {
      opts.inputPath = arg;
    } else {
      std::cerr << "cmc: error: more than one input file given\n";
      return 2;
    }
  }

  if (opts.inputPath.empty()) {
    std::cerr << "cmc: error: no input file\n";
    printUsage(std::cerr, argv[0]);
    return 2;
  }

  if (!opts.colorForced)
    opts.useColor = CMINUS_ISATTY(CMINUS_STDERR_FD) != 0;

  std::string source;
  if (!readFile(opts.inputPath, source)) {
    std::cerr << "cmc: error: cannot open '" << opts.inputPath << "'\n";
    return 2;
  }

  cminus::DiagnosticEngine diags(opts.inputPath, source);

  // --- scan
  cminus::Lexer lexer(source, diags);
  std::vector<cminus::Token> tokens = lexer.tokenize();
  if (opts.dumpTokens)
    dumpTokens(tokens);
  if (opts.stopAfter == Stage::Scan || diags.hasErrors())
    return finish(diags, opts.useColor);

  // --- parse
  cminus::Parser parser(std::move(tokens), diags);
  std::unique_ptr<cminus::Program> program = parser.parseProgram();
  if (opts.dumpAST && program)
    cminus::printAST(*program, std::cout);
  if (opts.stopAfter == Stage::Parse || diags.hasErrors() || !program)
    return finish(diags, opts.useColor);

  // --- analyze
  cminus::SemanticAnalyzer sema(diags);
  sema.analyze(*program);
  if (opts.dumpSymbols)
    sema.printSymbols(std::cout);
  if (opts.stopAfter == Stage::Analyze || diags.hasErrors())
    return finish(diags, opts.useColor);

  // --- resolve the target first: the data layout has to be on the module
  // before any IR is generated, because it decides how wide a pointer is.
  std::string error;
  std::unique_ptr<cminus::Target> target =
      cminus::Target::create(opts.targetTriple, opts.optLevel, error);
  if (!target) {
    std::cerr << "cmc: error: " << error << '\n';
    return 1;
  }
  const bool crossing = target->triple() != cminus::hostTriple();

  if (opts.output == OutputKind::Executable && crossing &&
      !checkCrossLinkInputs(opts, opts.targetTriple, error)) {
    std::cerr << "cmc: error: " << error << '\n';
    return 1;
  }

  // --- generate
  llvm::LLVMContext context;
  cminus::IRGenOptions irOptions;
  irOptions.moduleName = opts.inputPath;
  irOptions.indexChecks = opts.indexChecks;
  irOptions.targetTriple = target->triple();
  irOptions.dataLayout = target->dataLayout();

  std::unique_ptr<llvm::Module> module =
      cminus::generateIR(*program, context, diags, irOptions);
  if (!module || diags.hasErrors())
    return finish(diags, opts.useColor);

  if (!cminus::optimizeModule(*module, opts.optLevel, error)) {
    std::cerr << "cmc: error: " << error << '\n';
    return 1;
  }

  // --- write
  std::string output = opts.outputPath;
  if (output.empty()) {
    switch (opts.output) {
    case OutputKind::Executable: output = "a.out"; break;
    case OutputKind::Object:     output = replaceExtension(opts.inputPath, ".o"); break;
    case OutputKind::LLVMIR:     output = "-"; break;
    }
  }

  bool written = false;
  switch (opts.output) {
  case OutputKind::LLVMIR:
    written = cminus::writeIR(*module, output, error);
    break;
  case OutputKind::Object:
    written = target->writeObjectFile(*module, output, error);
    break;
  case OutputKind::Executable:
    written = buildExecutable(*module, *target, opts, argv[0], output, error);
    break;
  }
  if (!written) {
    std::cerr << "cmc: error: " << error << '\n';
    return 1;
  }

  return finish(diags, opts.useColor);
}
