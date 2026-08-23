#include "cminus/Token.h"

namespace cminus {

const char *tokenKindName(TokenKind kind) {
  switch (kind) {
  case TokenKind::EndOfFile:    return "EOF";
  case TokenKind::Error:        return "ERROR";
  case TokenKind::KwElse:       return "ELSE";
  case TokenKind::KwIf:         return "IF";
  case TokenKind::KwInt:        return "INT";
  case TokenKind::KwReturn:     return "RETURN";
  case TokenKind::KwVoid:       return "VOID";
  case TokenKind::KwWhile:      return "WHILE";
  case TokenKind::Identifier:   return "ID";
  case TokenKind::Number:       return "NUM";
  case TokenKind::Plus:         return "PLUS";
  case TokenKind::Minus:        return "MINUS";
  case TokenKind::Star:         return "TIMES";
  case TokenKind::Slash:        return "OVER";
  case TokenKind::Less:         return "LT";
  case TokenKind::LessEqual:    return "LEQ";
  case TokenKind::Greater:      return "GT";
  case TokenKind::GreaterEqual: return "GEQ";
  case TokenKind::EqualEqual:   return "EQ";
  case TokenKind::BangEqual:    return "NEQ";
  case TokenKind::Assign:       return "ASSIGN";
  case TokenKind::Semicolon:    return "SEMI";
  case TokenKind::Comma:        return "COMMA";
  case TokenKind::LParen:       return "LPAREN";
  case TokenKind::RParen:       return "RPAREN";
  case TokenKind::LBracket:     return "LBRACKET";
  case TokenKind::RBracket:     return "RBRACKET";
  case TokenKind::LBrace:       return "LBRACE";
  case TokenKind::RBrace:       return "RBRACE";
  }
  return "<invalid>";
}

const char *tokenKindSpelling(TokenKind kind) {
  switch (kind) {
  case TokenKind::EndOfFile:    return "end of file";
  case TokenKind::Error:        return "invalid token";
  case TokenKind::KwElse:       return "else";
  case TokenKind::KwIf:         return "if";
  case TokenKind::KwInt:        return "int";
  case TokenKind::KwReturn:     return "return";
  case TokenKind::KwVoid:       return "void";
  case TokenKind::KwWhile:      return "while";
  case TokenKind::Identifier:   return "identifier";
  case TokenKind::Number:       return "number";
  case TokenKind::Plus:         return "+";
  case TokenKind::Minus:        return "-";
  case TokenKind::Star:         return "*";
  case TokenKind::Slash:        return "/";
  case TokenKind::Less:         return "<";
  case TokenKind::LessEqual:    return "<=";
  case TokenKind::Greater:      return ">";
  case TokenKind::GreaterEqual: return ">=";
  case TokenKind::EqualEqual:   return "==";
  case TokenKind::BangEqual:    return "!=";
  case TokenKind::Assign:       return "=";
  case TokenKind::Semicolon:    return ";";
  case TokenKind::Comma:        return ",";
  case TokenKind::LParen:       return "(";
  case TokenKind::RParen:       return ")";
  case TokenKind::LBracket:     return "[";
  case TokenKind::RBracket:     return "]";
  case TokenKind::LBrace:       return "{";
  case TokenKind::RBrace:       return "}";
  }
  return "<invalid>";
}

} // namespace cminus
