#include <cstdio>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"

#include "cminus/AST.h"
#include "cminus/Diagnostic.h"
#include "cminus/Emit.h"
#include "cminus/IRGen.h"
#include "cminus/Lexer.h"
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

/// The last stage the driver should run. Each --dump-* flag stops the
/// pipeline right after the stage it prints, so a broken later stage cannot
/// hide the output of an earlier one.
enum class Stage { Scan, Parse, Analyze, Emit };

struct Options {
  std::string inputPath;
  std::string outputPath; // empty means "pick a default"
  Stage stopAfter = Stage::Emit;
  bool dumpTokens = false;
  bool dumpAST = false;
  bool dumpSymbols = false;
  bool emitObject = false;
  unsigned optLevel = 0;
  bool indexChecks = true;
  bool useColor = true;
  bool colorForced = false;
};

void printUsage(std::ostream &os, const char *argv0) {
  os << "usage: " << argv0 << " [options] <file.cm>\n"
     << "\n"
     << "Output:\n"
     << "  --emit-llvm       Write textual LLVM IR (the default)\n"
     << "  -c                Write a native object file\n"
     << "  -o <file>         Write to <file> ('-' means standard output)\n"
     << "  -O0 -O1 -O2 -O3   Optimization level (default -O0)\n"
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
      opts.emitObject = false;
    } else if (arg == "-c") {
      opts.emitObject = true;
    } else if (arg == "-o") {
      if (i + 1 >= argc) {
        std::cerr << "cmc: error: '-o' needs a file name\n";
        return 2;
      }
      opts.outputPath = argv[++i];
    } else if (arg == "-O0" || arg == "-O1" || arg == "-O2" || arg == "-O3") {
      opts.optLevel = static_cast<unsigned>(arg[2] - '0');
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

  // --- generate
  llvm::LLVMContext context;
  cminus::IRGenOptions irOptions;
  irOptions.moduleName = opts.inputPath;
  irOptions.indexChecks = opts.indexChecks;

  std::unique_ptr<llvm::Module> module =
      cminus::generateIR(*program, context, diags, irOptions);
  if (!module || diags.hasErrors())
    return finish(diags, opts.useColor);

  std::string error;
  if (!cminus::optimizeModule(*module, opts.optLevel, error)) {
    std::cerr << "cmc: error: " << error << '\n';
    return 1;
  }

  std::string output = opts.outputPath;
  if (output.empty())
    output = opts.emitObject ? replaceExtension(opts.inputPath, ".o") : "-";

  const bool written = opts.emitObject
                           ? cminus::writeObjectFile(*module, output, error)
                           : cminus::writeIR(*module, output, error);
  if (!written) {
    std::cerr << "cmc: error: " << error << '\n';
    return 1;
  }

  return finish(diags, opts.useColor);
}
