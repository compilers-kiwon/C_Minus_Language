# cmc — a C-Minus compiler

C-Minus (C−) is the teaching language from K. Louden, *Compiler Construction:
Principles and Practice*, Appendix A. This is a from-scratch compiler for it:
a hand-written scanner and recursive-descent parser, a semantic pass, and an
LLVM back end.

- [docs/spec/c-minus.md](docs/spec/c-minus.md) — the language specification
- [docs/spec/analysis.md](docs/spec/analysis.md) — FIRST/FOLLOW tables, conflict
  analysis, the LL(1) rewrite of the grammar, and the semantic-check list

## Design choices

| Decision | Choice |
| :- | :- |
| Implementation language | C++17 |
| Scanner | Hand-written DFA — no flex |
| Parser | Hand-written recursive descent — no bison |
| Back end | LLVM IR, then LLVM's own code generator for object files |

The grammar is LALR(1) apart from the dangling-else ambiguity, so a generated
parser would have worked; writing it by hand buys much better error messages
and makes the left-recursion removal explicit.

## Building

Requires a C++17 compiler, CMake ≥ 3.20 and LLVM development headers.
On Ubuntu:

```bash
sudo apt install -y build-essential cmake ninja-build clang llvm llvm-dev
```

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

## Running

By default `cmc` writes textual LLVM IR to standard output:

```bash
build/cmc examples/gcd.cm
```

To produce a program, compile to an object file and link it against the
runtime, which supplies `input`, `output` and the negative-subscript handler:

```bash
build/cmc -O2 -c examples/gcd.cm -o gcd.o && clang gcd.o build/libcminus_rt.a -o gcd
```

```
usage: cmc [options] <file.cm>

Output:
  --emit-llvm       Write textual LLVM IR (the default)
  -c                Write a native object file
  -o <file>         Write to <file> ('-' means standard output)
  -O0 -O1 -O2 -O3   Optimization level (default -O0)

Stages:
  --dump-tokens     Print the token stream and stop
  --dump-ast        Print the parse tree and stop
  --dump-symbols    Print the symbol table and stop

Other:
  -fno-index-check  Omit the negative-subscript checks
  --color           Force colored diagnostics
  --no-color        Disable colored diagnostics
  -h, --help        Show this message
```

Each `--dump-*` flag stops the pipeline right after the stage it prints.

## How C- maps onto LLVM

| C- | LLVM |
| :- | :- |
| `int` | `i32` |
| `int x[10]` | `[10 x i32]`, as a global or an entry-block `alloca` |
| `int a[]` parameter | `ptr` — arrays are passed by reference (spec 3.3) |
| `int` parameter | `i32`, given a stack slot so it can be assigned to |
| comparison | `icmp` then `zext` to `i32`, since the result is 1 or 0 (spec 3.7) |
| condition | `icmp ne i32 %v, 0` |
| `/` | `sdiv` — truncating integer division (spec 3.8) |
| `void main(void)` | `define i32 @main()` returning 0, so a C runtime can start it |

Subscripts use a plain `getelementptr` rather than an `inbounds` one: spec 3.6
leaves the upper bound unchecked, and marking an out-of-range access
`inbounds` would turn it into undefined behaviour that the optimizer could
exploit. The negative half of the check *is* required by the spec, so each
subscript is guarded unless `-fno-index-check` is given.

## Tests

Golden-file suites. Each runs the compiler under one set of flags and
compares the combined stdout+stderr, plus the exit status, against
`<name>.expected`:

| Suite | Flags |
| :- | :- |
| `tests/lex/` | `--dump-tokens` |
| `tests/parse/` | `--dump-ast` |
| `tests/sema/` | `--dump-symbols` |

```bash
CMC=build/cmc tests/run_tests.sh            # check
CMC=build/cmc tests/run_tests.sh --update   # re-record the expected output
```

End-to-end suite. Each `tests/exec/<name>.cm` is compiled, linked, and run on
`<name>.in`; what it prints is compared against `<name>.out`. Every case runs
at both `-O0` and `-O2` and must agree, which catches code generation that
only happens to work unoptimized:

```bash
CMC=build/cmc tests/run_exec_tests.sh
```

`ctest --test-dir build` runs both.

## Layout

```
include/cminus/   public headers
src/              implementation
runtime/          input, output and the negative-subscript handler
docs/spec/        language spec and grammar analysis
examples/         the two sample programs from the spec
tests/lex/        scanner golden-file tests
tests/parse/      parser golden-file tests
tests/sema/       semantic-analysis golden-file tests
tests/exec/       end-to-end compile-link-run tests
```

## Status

| Stage | State |
| :- | :- |
| Scanner (DFA, spec §1) | done |
| Diagnostics (caret rendering, error recovery) | done |
| AST + recursive-descent parser (spec §2) | done |
| Symbol table + semantic analysis (spec §3) | done |
| LLVM IR generation, optimization and object output | done |
