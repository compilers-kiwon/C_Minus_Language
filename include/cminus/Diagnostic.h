#ifndef CMINUS_DIAGNOSTIC_H
#define CMINUS_DIAGNOSTIC_H

#include <cstddef>
#include <iosfwd>
#include <string>
#include <vector>

#include "cminus/SourceLocation.h"

namespace cminus {

enum class DiagLevel { Error, Warning, Note };

struct Diagnostic {
  DiagLevel level = DiagLevel::Error;
  SourceLocation loc;
  std::string message;
};

/// Collects diagnostics and renders them in the familiar
/// `file:line:col: error: message` form with a source excerpt and a caret.
class DiagnosticEngine {
public:
  DiagnosticEngine(std::string fileName, std::string source);

  void error(SourceLocation loc, std::string message);
  void warning(SourceLocation loc, std::string message);
  void note(SourceLocation loc, std::string message);

  bool hasErrors() const { return errorCount_ > 0; }
  int errorCount() const { return errorCount_; }
  int warningCount() const { return warningCount_; }
  const std::vector<Diagnostic> &diagnostics() const { return diags_; }

  /// Render every diagnostic collected so far, in source order of arrival.
  void print(std::ostream &os, bool useColor) const;

  /// Render a one-line summary such as "2 errors generated." Returns false
  /// when there was nothing to summarize.
  bool printSummary(std::ostream &os, bool useColor) const;

private:
  void add(DiagLevel level, SourceLocation loc, std::string message);
  std::string lineText(int line) const;
  void render(std::ostream &os, const Diagnostic &d, bool useColor) const;

  std::string fileName_;
  std::string source_;
  std::vector<std::size_t> lineStarts_;
  std::vector<Diagnostic> diags_;
  int errorCount_ = 0;
  int warningCount_ = 0;
};

} // namespace cminus

#endif // CMINUS_DIAGNOSTIC_H
