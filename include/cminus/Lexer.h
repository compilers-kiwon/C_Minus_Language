#ifndef CMINUS_LEXER_H
#define CMINUS_LEXER_H

#include <cstddef>
#include <string>
#include <vector>

#include "cminus/Token.h"

namespace cminus {

class DiagnosticEngine;

/// Hand-written DFA scanner for C- (spec section 1).
///
/// Recovers from lexical errors by reporting them and continuing, so a single
/// run reports every bad character in the file rather than only the first.
class Lexer {
public:
  Lexer(std::string source, DiagnosticEngine &diags);

  /// Scan the next token. Returns EndOfFile repeatedly once the input is
  /// exhausted.
  Token next();

  /// Scan the whole input, including the terminating EndOfFile token.
  std::vector<Token> tokenize();

private:
  enum class State {
    Start,
    InIdentifier,
    InNumber,
    AfterSlash,   // saw '/': division, or the start of a comment
    InComment,    // inside /* ... */
    InCommentStar, // inside a comment, just saw '*'
    AfterLess,    // saw '<'
    AfterGreater, // saw '>'
    AfterAssign,  // saw '='
    AfterBang,    // saw '!'
  };

  char peek() const;
  bool atEnd() const { return pos_ >= source_.size(); }
  char advance();
  SourceLocation here() const { return SourceLocation{line_, column_}; }

  Token make(TokenKind kind, SourceLocation loc, std::string lexeme) const;

  std::string source_;
  DiagnosticEngine &diags_;
  std::size_t pos_ = 0;
  int line_ = 1;
  int column_ = 1;
};

} // namespace cminus

#endif // CMINUS_LEXER_H
