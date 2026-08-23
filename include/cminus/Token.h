#ifndef CMINUS_TOKEN_H
#define CMINUS_TOKEN_H

#include <string>

#include "cminus/SourceLocation.h"

namespace cminus {

/// Every terminal in the C- grammar, plus the two pseudo-tokens the scanner
/// needs. Section references are to docs/spec/c-minus.md.
enum class TokenKind {
  // Pseudo-tokens.
  EndOfFile,
  Error,

  // Keywords (1.1).
  KwElse,
  KwIf,
  KwInt,
  KwReturn,
  KwVoid,
  KwWhile,

  // Names and literals (1.3).
  Identifier,
  Number,

  // Arithmetic operators (rules 23, 25).
  Plus,
  Minus,
  Star,
  Slash,

  // Relational operators (rule 21).
  Less,
  LessEqual,
  Greater,
  GreaterEqual,
  EqualEqual,
  BangEqual,

  // Assignment (rule 18).
  Assign,

  // Punctuation.
  Semicolon,
  Comma,
  LParen,
  RParen,
  LBracket,
  RBracket,
  LBrace,
  RBrace,
};

/// Symbolic name used by --dump-tokens, e.g. "IF", "ID", "NUM", "LEQ".
const char *tokenKindName(TokenKind kind);

/// How the token looks in source, for use inside diagnostics. Identifier and
/// Number have no fixed spelling and render as "identifier" / "number".
const char *tokenKindSpelling(TokenKind kind);

struct Token {
  TokenKind kind = TokenKind::EndOfFile;
  /// Exact source text. Empty for EndOfFile.
  std::string lexeme;
  /// Numeric value; meaningful only when kind == Number.
  long long value = 0;
  SourceLocation loc;

  bool is(TokenKind k) const { return kind == k; }
  bool isNot(TokenKind k) const { return kind != k; }

  /// `int` or `void` (rule 5).
  bool isTypeSpecifier() const {
    return kind == TokenKind::KwInt || kind == TokenKind::KwVoid;
  }
  /// One of <= < > >= == != (rule 21).
  bool isRelop() const {
    switch (kind) {
    case TokenKind::Less:
    case TokenKind::LessEqual:
    case TokenKind::Greater:
    case TokenKind::GreaterEqual:
    case TokenKind::EqualEqual:
    case TokenKind::BangEqual:
      return true;
    default:
      return false;
    }
  }
  /// + or - (rule 23).
  bool isAddop() const {
    return kind == TokenKind::Plus || kind == TokenKind::Minus;
  }
  /// * or / (rule 25).
  bool isMulop() const {
    return kind == TokenKind::Star || kind == TokenKind::Slash;
  }
};

} // namespace cminus

#endif // CMINUS_TOKEN_H
