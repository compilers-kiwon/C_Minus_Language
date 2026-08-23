#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "cminus/Diagnostic.h"
#include "cminus/Lexer.h"
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

struct Options {
  std::string inputPath;
  bool dumpTokens = false;
  bool useColor = true;
  bool colorForced = false;
};

void printUsage(std::ostream &os, const char *argv0) {
  os << "usage: " << argv0 << " [options] <file.cm>\n"
     << "\n"
     << "Options:\n"
     << "  --dump-tokens   Print the token stream and stop\n"
     << "  --color         Force colored diagnostics\n"
     << "  --no-color      Disable colored diagnostics\n"
     << "  -h, --help      Show this message\n";
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
  const std::vector<cminus::Token> tokens = lexer.tokenize();
  if (opts.dumpTokens)
    dumpTokens(tokens);

  return finish(diags, opts.useColor);
}
