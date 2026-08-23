# cmc — a C-Minus compiler

C-Minus (C−) is the teaching language from K. Louden, *Compiler Construction:
Principles and Practice*, Appendix A. This is a from-scratch compiler for it:
a hand-written scanner and recursive-descent parser feeding an LLVM IR backend.

- [docs/spec/c-minus.md](docs/spec/c-minus.md) — the language specification
- [docs/spec/analysis.md](docs/spec/analysis.md) — FIRST/FOLLOW tables, conflict
  analysis, the LL(1) rewrite of the grammar, and the semantic-check list

## Design choices

| Decision | Choice |
| :- | :- |
| Implementation language | C++17 |
| Scanner | Hand-written DFA — no flex |
| Parser | Hand-written recursive descent — no bison |
| Backend | LLVM IR (textual `.ll`), then `llc`/`clang` for machine code |

The grammar is LALR(1) apart from the dangling-else ambiguity, so a generated
parser would have worked; writing it by hand buys much better error messages
and makes the left-recursion removal explicit.

## Building

Requires a C++17 compiler, CMake ≥ 3.20 and (for the backend) LLVM development
headers. On Ubuntu:

```bash
sudo apt update && sudo apt install -y build-essential cmake ninja-build clang llvm llvm-dev libzstd-dev zlib1g-dev
```

Then:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

## Running

```bash
build/cmc --dump-ast examples/gcd.cm
```

```
usage: cmc [options] <file.cm>

Options:
  --dump-tokens   Print the token stream and stop
  --dump-ast      Print the parse tree and stop
  --color         Force colored diagnostics
  --no-color      Disable colored diagnostics
  -h, --help      Show this message
```

Each `--dump-*` flag stops the pipeline right after the stage it prints.

## Tests

Golden-file tests. Each suite runs the compiler under one set of flags and
compares the combined stdout+stderr, plus the exit status, against
`<name>.expected`:

| Suite | Flags |
| :- | :- |
| `tests/lex/` | `--dump-tokens` |
| `tests/parse/` | `--dump-ast` |

```bash
CMC=build/cmc tests/run_tests.sh            # check
CMC=build/cmc tests/run_tests.sh --update   # re-record the expected output
```

`ctest --test-dir build` runs the same suite.

## Layout

```
include/cminus/   public headers
src/              implementation
docs/spec/        language spec and grammar analysis
examples/         the two sample programs from the spec
tests/lex/        scanner golden-file tests
tests/parse/      parser golden-file tests
```

## Status

| Stage | State |
| :- | :- |
| Scanner (DFA, spec §1) | done |
| Diagnostics (caret rendering, error recovery) | done |
| AST + recursive-descent parser (spec §2) | done |
| Semantic analysis, symbol table (spec §3) | not started |
| LLVM IR generation | not started |
