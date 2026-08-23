#ifndef CMINUS_SOURCELOCATION_H
#define CMINUS_SOURCELOCATION_H

namespace cminus {

/// A 1-based position in the source file.
struct SourceLocation {
  int line = 0;
  int column = 0;

  bool isValid() const { return line > 0; }
};

} // namespace cminus

#endif // CMINUS_SOURCELOCATION_H
