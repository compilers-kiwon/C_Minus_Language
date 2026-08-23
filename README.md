# cmc — a C-Minus compiler

[![CI](https://github.com/compilers-kiwon/C_Minus_Language/actions/workflows/ci.yml/badge.svg)](https://github.com/compilers-kiwon/C_Minus_Language/actions/workflows/ci.yml)

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
| Linking | A C driver (`cc`), told to use `lld` when it is installed |
| Cross compilation | `--target <triple>`, for any back end LLVM was built with |

The grammar is LALR(1) apart from the dangling-else ambiguity, so a generated
parser would have worked; writing it by hand buys much better error messages
and makes the left-recursion removal explicit.

## Building

Requires a C++17 compiler, CMake ≥ 3.20 and **LLVM 21 or newer** — the code
generator uses the Triple-based overloads of `TargetRegistry::lookupTarget`,
`TargetMachine::createTargetMachine` and `Module::setTargetTriple`, which
replaced the string ones in that release.

```bash
sudo apt install -y build-essential cmake ninja-build clang llvm llvm-dev
```

If the distribution is older than LLVM 21, install it from
[apt.llvm.org](https://apt.llvm.org) and point CMake at it with
`-DLLVM_DIR=/usr/lib/llvm-21/lib/cmake/llvm`.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

Two options exist for checking rather than developing:

| Option | Effect |
| :- | :- |
| `-DCMINUS_WERROR=ON` | Warnings become errors |
| `-DCMINUS_SANITIZERS=ON` | Build with ASan and UBSan, not recovering from a report |

## Running

`cmc` compiles and links, so one command produces a program:

```bash
build/cmc -O2 examples/gcd.cm -o gcd && echo "270 192" | ./gcd
```

`-c` stops at an object file and `--emit-llvm` writes textual IR to standard
output instead.

```
usage: cmc [options] <file.cm>

By default the program is compiled and linked into an executable.

Output:
  -c                Compile only; write an object file
  --emit-llvm       Write textual LLVM IR instead of linking
  -o <file>         Output path (default a.out, or <input>.o with -c;
                    '-' means standard output)
  -O0 -O1 -O2 -O3   Optimization level (default -O0)

Linking:
  --cc <command>    C driver used to link (default $CMINUS_CC, else
                    cc, clang or gcc)
  --use-ld=<name>   Pass -fuse-ld=<name> to the driver; the default
                    is lld when it is installed
  --runtime <file>  Path to libcminus_rt.a (default $CMINUS_RUNTIME,
                    else next to the compiler)
  -save-temps       Keep the intermediate object file
  -v                Print the link command

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

### Cross compilation

```bash
cmc --target aarch64-linux-gnu -c prog.cm -o prog.o
```

Any triple LLVM has a back end for works; `cmc` links every one of them in,
which accounts for most of its size. The target is resolved before IR
generation rather than at object-writing time, because the data layout decides
how wide a pointer is and therefore what type a `getelementptr` index takes --
`i64` on aarch64, `i32` on armv7.

Linking as well as compiling needs two more things, and `cmc` says so up front
rather than letting the linker fail with an object-format mismatch: a driver
for the target, and a runtime built for it.

```bash
aarch64-linux-gnu-gcc -O2 -c runtime/cminus_rt.c -o rt.o
aarch64-linux-gnu-ar rcs libcminus_rt-aarch64.a rt.o
cmc --target aarch64-linux-gnu --cc aarch64-linux-gnu-gcc --runtime libcminus_rt-aarch64.a -O2 prog.cm -o prog
qemu-aarch64 -L /usr/aarch64-linux-gnu ./prog
```

`--cc` takes a whole command rather than just a name, because a cross
toolchain is unusable without its sysroot. When it is given, `cmc` leaves the
command alone -- no `-fuse-ld=lld` gets added to a toolchain it knows nothing
about.

### Why the link goes through a C driver

LLVM ships LLD, but LLD replaces `ld`, not `cc`. Turning one object file into
a program on this machine takes a link line like:

```
ld -z relro --hash-style=gnu -m elf_x86_64 -pie
   -dynamic-linker /lib64/ld-linux-x86-64.so.2
   Scrt1.o crti.o /usr/lib/gcc/x86_64-linux-gnu/15/crtbeginS.o
   -L... (six paths) prog.o -lgcc -lgcc_s -lc ...
```

The crt objects, the search paths, the dynamic loader and the gcc version in
that path are the *driver's* knowledge, and they differ per distribution and
per architecture. A compiler can only skip the driver if it also ships a libc.
So `cmc` invokes `cc` and, when `ld.lld` is installed, adds `-fuse-ld=lld` so
that LLD does the actual linking.

The probe is for `ld.lld` under exactly that name, because that is what the
driver itself searches for once it is given `-fuse-ld=lld`; finding only a
versioned `ld.lld-21` and passing the flag anyway would just make the driver
fail. Whether LLD took part is visible in the binary rather than in the flag,
since it records itself and the system linker does not:

```
$ readelf -p .comment ./gcd
  [     1]  Linker: Ubuntu LLD 21.1.8
  [    1b]  GCC: (Ubuntu 15.2.0-16ubuntu1) 15.2.0
```

`--use-ld=bfd` forces the system linker instead. On Ubuntu, `apt install lld`
is enough to switch the default over.

The runtime archive is found without configuration: `$CMINUS_RUNTIME` first,
then next to the compiler (the build tree), then `../lib` (an unpacked
release).

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

End-to-end suite. Each `tests/exec/<name>.cm` is built, run on `<name>.in`,
and what it prints compared against `<name>.expected`. Every case is built
three ways and all three must agree: `-O0` and `-O2` linked by `cmc`, which
catches code generation that only happens to work unoptimized, and `-c`
followed by a separate link, which keeps that path honest:

```bash
CMC=build/cmc tests/run_exec_tests.sh
```

Cross suite. The same programs and the same expectations, built for another
architecture and run under an emulator: what a C- program prints does not
depend on the machine it runs on, so any difference is a code generation bug.
It skips itself when no cross toolchain is installed.

```bash
CMC=build/cmc tests/run_cross_tests.sh
CROSS_TARGET=riscv64-linux-gnu CROSS_EMU=qemu-riscv64 CMC=build/cmc tests/run_cross_tests.sh
```

`ctest --test-dir build` runs all three.

## Continuous integration

[`ci.yml`](.github/workflows/ci.yml) builds with gcc and clang, in Debug and
Release, with warnings as errors, and runs both suites in each. A fifth job
repeats them under ASan and UBSan. Reproduce any of it locally with the
options in the build table above.

[`release.yml`](.github/workflows/release.yml) reacts to a `v*` tag: it builds
Release, refuses to continue unless the tests pass, and publishes a tarball of
`bin/cmc`, `lib/libcminus_rt.a`, the runtime source, the examples and the spec.
LLVM is linked into the binary, so the archive only needs a matching glibc and
libstdc++.

## Layout

```
include/cminus/   public headers
src/              implementation
runtime/          input, output and the negative-subscript handler
.github/          CI and release workflows
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
| Cross compilation to any LLVM back end | done |
