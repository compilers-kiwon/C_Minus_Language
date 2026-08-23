#include "cminus/Diagnostic.h"

#include <ostream>
#include <utility>

namespace cminus {

namespace {

const char *const kReset = "\033[0m";
const char *const kBold = "\033[1m";
const char *const kRed = "\033[1;31m";
const char *const kMagenta = "\033[1;35m";
const char *const kCyan = "\033[1;36m";
const char *const kGreen = "\033[1;32m";

const char *levelText(DiagLevel level) {
  switch (level) {
  case DiagLevel::Error:   return "error";
  case DiagLevel::Warning: return "warning";
  case DiagLevel::Note:    return "note";
  }
  return "error";
}

const char *levelColor(DiagLevel level) {
  switch (level) {
  case DiagLevel::Error:   return kRed;
  case DiagLevel::Warning: return kMagenta;
  case DiagLevel::Note:    return kCyan;
  }
  return kRed;
}

} // namespace

DiagnosticEngine::DiagnosticEngine(std::string fileName, std::string source)
    : fileName_(std::move(fileName)), source_(std::move(source)) {
  lineStarts_.push_back(0);
  for (std::size_t i = 0; i < source_.size(); ++i)
    if (source_[i] == '\n')
      lineStarts_.push_back(i + 1);
}

void DiagnosticEngine::add(DiagLevel level, SourceLocation loc,
                           std::string message) {
  diags_.push_back(Diagnostic{level, loc, std::move(message)});
  if (level == DiagLevel::Error)
    ++errorCount_;
  else if (level == DiagLevel::Warning)
    ++warningCount_;
}

void DiagnosticEngine::error(SourceLocation loc, std::string message) {
  add(DiagLevel::Error, loc, std::move(message));
}

void DiagnosticEngine::warning(SourceLocation loc, std::string message) {
  add(DiagLevel::Warning, loc, std::move(message));
}

void DiagnosticEngine::note(SourceLocation loc, std::string message) {
  add(DiagLevel::Note, loc, std::move(message));
}

std::string DiagnosticEngine::lineText(int line) const {
  if (line < 1 || static_cast<std::size_t>(line) > lineStarts_.size())
    return {};
  std::size_t begin = lineStarts_[static_cast<std::size_t>(line) - 1];
  std::size_t end = source_.find('\n', begin);
  if (end == std::string::npos)
    end = source_.size();
  // Trim a trailing carriage return so CRLF files line up with the caret.
  if (end > begin && source_[end - 1] == '\r')
    --end;
  return source_.substr(begin, end - begin);
}

void DiagnosticEngine::render(std::ostream &os, const Diagnostic &d,
                              bool useColor) const {
  const char *color = useColor ? levelColor(d.level) : "";
  const char *bold = useColor ? kBold : "";
  const char *reset = useColor ? kReset : "";

  os << bold << fileName_;
  if (d.loc.isValid())
    os << ':' << d.loc.line << ':' << d.loc.column;
  os << ": " << reset;
  os << color << levelText(d.level) << ": " << reset;
  os << bold << d.message << reset << '\n';

  if (!d.loc.isValid())
    return;

  const std::string text = lineText(d.loc.line);
  if (text.empty() && d.loc.column <= 1)
    return;

  os << "  " << text << '\n' << "  ";
  // Copy tabs through so the caret stays under the right character.
  for (int i = 0; i + 1 < d.loc.column; ++i)
    os << (static_cast<std::size_t>(i) < text.size() && text[static_cast<std::size_t>(i)] == '\t'
               ? '\t'
               : ' ');
  os << color << '^' << reset << '\n';
}

void DiagnosticEngine::print(std::ostream &os, bool useColor) const {
  for (const Diagnostic &d : diags_)
    render(os, d, useColor);
}

bool DiagnosticEngine::printSummary(std::ostream &os, bool useColor) const {
  if (errorCount_ == 0 && warningCount_ == 0)
    return false;
  const char *bold = useColor ? kBold : "";
  const char *reset = useColor ? kReset : "";
  os << bold;
  if (warningCount_ > 0) {
    os << warningCount_ << (warningCount_ == 1 ? " warning" : " warnings");
    if (errorCount_ > 0)
      os << " and ";
  }
  if (errorCount_ > 0)
    os << errorCount_ << (errorCount_ == 1 ? " error" : " errors");
  os << " generated." << reset << '\n';
  (void)kGreen;
  return true;
}

} // namespace cminus
