#include "cminus/Lexer.h"

#include <charconv>
#include <cstdio>
#include <utility>

#include "cminus/Diagnostic.h"

namespace cminus {

namespace {

// Spec 1.3: letter = a|..|z|A|..|Z. Deliberately not std::isalpha, which is
// locale-dependent and would accept characters C- does not have.
bool isLetter(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool isDigit(char c) { return c >= '0' && c <= '9'; }

// Spec 1.4: blanks, newlines and tabs only separate tokens.
bool isSpace(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' ||
         c == '\v';
}

/// The six reserved words of spec 1.1. C- is case-sensitive, so a simple
/// exact match is all that is needed.
TokenKind keywordOrIdentifier(const std::string &text) {
  if (text == "else")   return TokenKind::KwElse;
  if (text == "if")     return TokenKind::KwIf;
  if (text == "int")    return TokenKind::KwInt;
  if (text == "return") return TokenKind::KwReturn;
  if (text == "void")   return TokenKind::KwVoid;
  if (text == "while")  return TokenKind::KwWhile;
  return TokenKind::Identifier;
}

/// Printable rendering of a rejected byte, for the diagnostic text.
std::string describe(char c) {
  if (static_cast<unsigned char>(c) >= 0x20 &&
      static_cast<unsigned char>(c) < 0x7f)
    return std::string(1, c);
  char buf[8];
  std::snprintf(buf, sizeof(buf), "\\x%02x",
                static_cast<unsigned>(static_cast<unsigned char>(c)));
  return std::string(buf);
}

} // namespace

Lexer::Lexer(std::string source, DiagnosticEngine &diags)
    : source_(std::move(source)), diags_(diags) {}

char Lexer::peek() const { return atEnd() ? '\0' : source_[pos_]; }

char Lexer::advance() {
  if (atEnd())
    return '\0';
  const char c = source_[pos_++];
  if (c == '\n') {
    ++line_;
    column_ = 1;
  } else {
    ++column_;
  }
  return c;
}

Token Lexer::make(TokenKind kind, SourceLocation loc,
                  std::string lexeme) const {
  Token tok;
  tok.kind = kind;
  tok.loc = loc;
  tok.lexeme = std::move(lexeme);
  return tok;
}

Token Lexer::next() {
  State state = State::Start;
  SourceLocation start = here();
  std::string text;

  for (;;) {
    switch (state) {

    case State::Start: {
      start = here();
      text.clear();
      if (atEnd())
        return make(TokenKind::EndOfFile, start, "");

      const char c = peek();
      if (isLetter(c)) {
        text += advance();
        state = State::InIdentifier;
        break;
      }
      if (isDigit(c)) {
        text += advance();
        state = State::InNumber;
        break;
      }
      if (isSpace(c)) {
        advance();
        break;
      }

      advance();
      switch (c) {
      // Two-character operators need one more character before deciding
      // (maximal munch), so they get their own states.
      case '/': state = State::AfterSlash;   break;
      case '<': state = State::AfterLess;    break;
      case '>': state = State::AfterGreater; break;
      case '=': state = State::AfterAssign;  break;
      case '!': state = State::AfterBang;    break;

      case '+': return make(TokenKind::Plus, start, "+");
      case '-': return make(TokenKind::Minus, start, "-");
      case '*': return make(TokenKind::Star, start, "*");
      case ';': return make(TokenKind::Semicolon, start, ";");
      case ',': return make(TokenKind::Comma, start, ",");
      case '(': return make(TokenKind::LParen, start, "(");
      case ')': return make(TokenKind::RParen, start, ")");
      case '[': return make(TokenKind::LBracket, start, "[");
      case ']': return make(TokenKind::RBracket, start, "]");
      case '{': return make(TokenKind::LBrace, start, "{");
      case '}': return make(TokenKind::RBrace, start, "}");

      default:
        diags_.error(start, "invalid character '" + describe(c) +
                                "' in source file");
        return make(TokenKind::Error, start, std::string(1, c));
      }
      break;
    }

    // Spec 1.3: ID = letter letter*. A digit ends the identifier rather than
    // extending it, so "abc1" scans as ID(abc) NUM(1) -- not an error.
    case State::InIdentifier:
      if (isLetter(peek())) {
        text += advance();
        break;
      }
      return make(keywordOrIdentifier(text), start, text);

    // Spec 1.3: NUM = digit digit*. There is no unary minus in the grammar,
    // so every NUM is non-negative.
    case State::InNumber: {
      if (isDigit(peek())) {
        text += advance();
        break;
      }
      Token tok = make(TokenKind::Number, start, text);
      const char *first = text.data();
      const char *last = first + text.size();
      auto result = std::from_chars(first, last, tok.value);
      if (result.ec != std::errc{}) {
        diags_.error(start, "integer constant '" + text + "' is too large");
        tok.value = 0;
      }
      return tok;
    }

    // Spec 1.5: comments are /* ... */ and do not nest.
    case State::AfterSlash:
      if (peek() == '*') {
        advance();
        state = State::InComment;
        break;
      }
      return make(TokenKind::Slash, start, "/");

    case State::InComment:
      if (atEnd()) {
        diags_.error(start, "unterminated comment");
        return make(TokenKind::EndOfFile, here(), "");
      }
      if (advance() == '*')
        state = State::InCommentStar;
      break;

    case State::InCommentStar: {
      if (atEnd()) {
        diags_.error(start, "unterminated comment");
        return make(TokenKind::EndOfFile, here(), "");
      }
      const char c = advance();
      if (c == '/')
        state = State::Start; // comment closed; resume scanning
      else if (c != '*')
        state = State::InComment; // "**" stays one character from closing
      break;
    }

    case State::AfterLess:
      if (peek() == '=') {
        advance();
        return make(TokenKind::LessEqual, start, "<=");
      }
      return make(TokenKind::Less, start, "<");

    case State::AfterGreater:
      if (peek() == '=') {
        advance();
        return make(TokenKind::GreaterEqual, start, ">=");
      }
      return make(TokenKind::Greater, start, ">");

    case State::AfterAssign:
      if (peek() == '=') {
        advance();
        return make(TokenKind::EqualEqual, start, "==");
      }
      return make(TokenKind::Assign, start, "=");

    // '!' exists only as part of '!='; on its own it is a lexical error.
    case State::AfterBang:
      if (peek() == '=') {
        advance();
        return make(TokenKind::BangEqual, start, "!=");
      }
      diags_.error(start, "'!' is not an operator in C-; did you mean '!='?");
      return make(TokenKind::Error, start, "!");
    }
  }
}

std::vector<Token> Lexer::tokenize() {
  std::vector<Token> tokens;
  for (;;) {
    Token tok = next();
    const bool last = tok.is(TokenKind::EndOfFile);
    tokens.push_back(std::move(tok));
    if (last)
      return tokens;
  }
}

} // namespace cminus
