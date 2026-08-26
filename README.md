# cmc — a C-Minus compiler

[![CI](https://github.com/compilers-kiwon/C_Minus_Language/actions/workflows/ci.yml/badge.svg)](https://github.com/compilers-kiwon/C_Minus_Language/actions/workflows/ci.yml)

C-Minus (C−) is the teaching language from K. Louden, *Compiler Construction:
Principles and Practice*, Appendix A. This is a from-scratch compiler for it:
a hand-written scanner and recursive-descent parser, a semantic pass, and an
LLVM back end.

- [docs/spec/c-minus.md](docs/spec/c-minus.md) — the language specification
- [docs/spec/analysis.md](docs/spec/analysis.md) — FIRST/FOLLOW tables, conflict
  analysis, the LL(1) rewrite of the grammar, and the semantic-check list

## Getting it

Three ways in, depending on what you want.

**A released binary.** Nothing to build; needs a glibc no older than Ubuntu
24.04's, since LLVM itself is linked in.

```bash
curl -sSLO https://github.com/compilers-kiwon/C_Minus_Language/releases/latest/download/cmc-v0.1.2-linux-x86_64.tar.gz
tar -xzf cmc-v0.1.2-linux-x86_64.tar.gz && cd cmc-v0.1.2-linux-x86_64
bin/cmc -O2 examples/gcd.cm -o gcd && echo "270 192" | ./gcd
```

**From source.** See [Building](#building) below for the LLVM 21 requirement.

```bash
git clone https://github.com/compilers-kiwon/C_Minus_Language.git
cd C_Minus_Language
cmake -S . -B build -G Ninja && cmake --build build && ctest --test-dir build
```

**Through Yocto**, to put a C- program in an embedded image:
[meta-cminus/BUILDING.md](meta-cminus/BUILDING.md).

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
| `-DCMINUS_BUILD_RUNTIME=OFF` | Compiler only; nothing is built for the target |
| `-DCMINUS_BUILD_COMPILER=OFF` | Runtime only, and LLVM is not needed at all |

The last two exist because the compiler and the runtime end up on different
machines as soon as anything is cross-compiled, and a build system that knows
that wants to build them separately.

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

Cross compilation:
  --target <triple> Generate code for <triple> instead of the host,
                    e.g. aarch64-linux-gnu or riscv64-linux-gnu
  --mcpu=<name>     Target CPU (default generic)
  --mattr=<list>    Target features, e.g. +m,+a,+f,+d,+c
  --target-abi=<n>  Target ABI, e.g. lp64d. Only some targets keep
                    this out of the triple; RISC-V is one

Linking:
  --cc <command>    C driver used to link. May carry arguments, as a
                    cross toolchain needs its sysroot (default
                    $CMINUS_CC, else cc, clang or gcc)
  --use-ld=<name>   Pass -fuse-ld=<name> to the driver; lld is used
                    by default when installed and --cc was not given
  --runtime <file>  Path to libcminus_rt.a, which must be built for
                    the target (default $CMINUS_RUNTIME, else next to
                    the compiler)
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

## Cross compilation

`--target` names the architecture to generate code for. Anything LLVM has a
back end for works, because `cmc` links all of them in:

```bash
cmc --target aarch64-linux-gnu   -c prog.cm -o prog.o
cmc --target riscv64-linux-gnu   -c prog.cm -o prog.o
cmc --target arm-linux-gnueabihf -c prog.cm -o prog.o
```

`llc --version` lists the registered targets. They are the same set `cmc`
carries, since both come from the LLVM it was built against.

### What linking needs

An object file needs nothing but `--target`. A *program* needs two things
`cmc` cannot supply and will not guess:

| | Why |
| :- | :- |
| a C driver for the target | It owns the link line: crt objects, libc, the dynamic loader. See [Why the link goes through a C driver](#why-the-link-goes-through-a-c-driver). |
| a runtime built for the target | `libcminus_rt.a` holds `input`, `output` and the subscript handler, and the copy beside `cmc` is the host's. |

Both are reported before the link is attempted, rather than surfacing later as
an object-format mismatch that explains nothing:

```
cmc: error: linking for 'aarch64-linux-gnu' needs a matching C driver;
     pass --cc "aarch64-linux-gnu-gcc --sysroot=<path>", or use -c and link separately
```

### A worked example

Building for aarch64 on an x86-64 host and running the result under emulation.
On Ubuntu:

```bash
sudo apt install -y gcc-aarch64-linux-gnu libc6-dev-arm64-cross qemu-user
```

The headers package matters: the compiler package only *recommends* it, so
`--no-install-recommends` leaves a gcc that cannot find `stdio.h`.

Build the runtime for the target, once:

```bash
aarch64-linux-gnu-gcc -O2 -c runtime/cminus_rt.c -o rt.o
aarch64-linux-gnu-ar rcs libcminus_rt-aarch64.a rt.o
```

Then compile, link and run:

```bash
cmc --target aarch64-linux-gnu --cc aarch64-linux-gnu-gcc --runtime libcminus_rt-aarch64.a -O2 examples/gcd.cm -o gcd
echo "270 192" | qemu-aarch64 -L /usr/aarch64-linux-gnu ./gcd
```

```
6
```

`-v` prints the link command, and `file ./gcd` says what came out
(`ELF 64-bit LSB pie executable, ARM aarch64`).

### What a triple does not say

A triple carries the float ABI for ARM -- that is what the `hf` in
`arm-linux-gnueabihf` means -- but RISC-V keeps it in a separate `-mabi`, so
LLVM falls back to soft float with no extensions. No distribution builds its
libc that way, and the linker says so:

```
riscv64-linux-gnu-ld.bfd: can't link soft-float modules with double-float modules
```

Linux RISC-V userspace is rv64gc/lp64d, so that is what `cmc` defaults to for
a `riscv64-*-linux-*` triple, matching clang. `--mattr=` and `--target-abi=`
override it where something else is wanted, and `-v` shows what was chosen:

```bash
cmc -v --target riscv64-linux-gnu -c prog.cm -o prog.o
```

```
target: riscv64-unknown-linux-gnu  cpu: generic  features: +m,+a,+f,+d,+c  abi: lp64d
```

### Other targets

Only the three names change. A cross gcc installs its sysroot at
`/usr/<triple>`, which is what the emulator's `-L` wants.

| Triple | Ubuntu package | Emulator |
| :- | :- | :- |
| `aarch64-linux-gnu` | `gcc-aarch64-linux-gnu` | `qemu-aarch64` |
| `arm-linux-gnueabihf` | `gcc-arm-linux-gnueabihf` | `qemu-arm` |
| `riscv64-linux-gnu` | `gcc-riscv64-linux-gnu` | `qemu-riscv64` |
| `powerpc64le-linux-gnu` | `gcc-powerpc64le-linux-gnu` | `qemu-ppc64le` |

### Under another build system

A build system that links for itself wants only the object:

```bash
cmc --target "$TARGET" -c prog.cm -o prog.o
```

If it should link too, `--cc` takes a command with arguments rather than a
program name, because a cross toolchain is unusable without its sysroot and a
whole command line is how `CC` is normally handed around:

```bash
cmc --target "$TARGET" --cc "$CC" --runtime "$SYSROOT/lib/libcminus_rt.a" prog.cm -o prog
```

`$CMINUS_CC` and `$CMINUS_RUNTIME` set the same two from the environment.
Naming the driver this way also stops `cmc` adding `-fuse-ld=lld` to a
toolchain it knows nothing about.

### Why the target is resolved before IR generation

Not at object-writing time, which is where it would be if `--target` were only
a code generation flag. The data layout says how wide a pointer is, and that
decides the type a `getelementptr` index takes, so the module has to carry it
before the first instruction is built:

```llvm
armv7    %a.elem = getelementptr i32, ptr %a.base, i32 %low2
aarch64  %a.elem = getelementptr i32, ptr %a.base, i64 %idx
```

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

## Yocto

[`meta-cminus/`](meta-cminus/) is a layer with three recipes:

| Recipe | Runs on | Purpose |
| :- | :- | :- |
| `cmc-native` | build host | the compiler; `DEPENDS = "llvm-native"` |
| `cminus-runtime` | target | `libcminus_rt.a`, linked into compiled programs |
| `cminus-hello` | target | a `.cm` source turned into a binary in the image |

```bash
bitbake-layers add-layer /path/to/C_Minus_Language/meta-cminus
bitbake cminus-hello
```

[meta-cminus/BUILDING.md](meta-cminus/BUILDING.md) covers it from an empty
machine, including the parts that are easy to get wrong: poky's master branch
is dormant so BitBake and openembedded-core are cloned separately, and BitBake
will not start without an `en_US.UTF-8` locale.

The split into three follows from Yocto cross-compiling everything: `cmc` runs
where the build runs, while the runtime is linked into programs for the device.
Needs LLVM 21, so openembedded-core from `wrynose` on.

Built end to end against openembedded-core master with LLVM 22.1.8, for both
`qemux86-64` and `qemuarm64`: the binaries run under emulation carrying the
distribution's hardening flags, and `core-image-minimal` for `qemux86-64`
lists `cminus-hello` in its rootfs manifest.
[meta-cminus/README.md](meta-cminus/README.md) has what the real build caught
that reading the recipes could not.

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

Cross suite. The same programs and the same expectations, built for other
architectures and run under an emulator: what a C- program prints does not
depend on the machine it runs on, so any difference is a code generation bug.
Three targets by default, each for a reason:

| Target | Covers |
| :- | :- |
| `aarch64-linux-gnu` | 64-bit, the common cross target |
| `arm-linux-gnueabihf` | 32-bit, and so the `i32` subscript index path |
| `riscv64-linux-gnu` | a different instruction set family |

```bash
CMC=build/cmc tests/run_cross_tests.sh
CROSS_TARGETS="riscv64-linux-gnu" CMC=build/cmc tests/run_cross_tests.sh
```

A target whose toolchain is absent is skipped, so a machine with only some of
them still runs what it can. `CROSS_REQUIRED=1` turns every skip into a
failure, which is what CI sets: a skip there would be indistinguishable from
a pass.

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
meta-cminus/      Yocto layer: recipes for the compiler, runtime and an example
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
| Yocto layer (built end to end) | done |
