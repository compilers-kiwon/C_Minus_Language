# meta-cminus

A Yocto layer that builds the C- compiler for the build host, its runtime for
the target, and an example program compiled into an image.

| Recipe | Runs on | Purpose |
| :- | :- | :- |
| `cmc-native` | build host | the compiler; `DEPENDS = "llvm-native"`, plus `mlir-native` for `--use-mlir` |
| `mlir-native` | build host | MLIR, which openembedded-core's `llvm` recipe does not build |
| `cminus-runtime` | target | `libcminus_rt.a`, linked into compiled programs |
| `cminus-hello` | target | a `.cm` source turned into a binary in the image |

## Why these recipes

Everything in Yocto is cross-compiled, which splits this project in two. `cmc`
runs where the build runs, so it is native and needs LLVM. `libcminus_rt.a` is
linked into programs for the device, so it is a target recipe built with the
target's toolchain and must not drag LLVM in. The upstream CMakeLists has
`CMINUS_BUILD_COMPILER` and `CMINUS_BUILD_RUNTIME` for exactly this split.

`cminus-hello` is the part worth reading. Its `do_compile` expands to:

```
cmc --target x86_64-oe-linux \
    --cc "x86_64-oe-linux-gcc -m64 -march=x86-64-v3 -fstack-protector-strong \
          -O2 -D_FORTIFY_SOURCE=2 ... --sysroot=.../recipe-sysroot" \
    --runtime .../recipe-sysroot/usr/lib/libcminus_rt.a \
    -O2 .../sources/hello.cm -o .../build/cminus-hello
```

`${CC}` is a command with eight arguments, not a program name. That is why
`cmc --cc` accepts a whole command line: a cross driver without its sysroot is
useless, and no build system hands one over any other way.

`cmc-native` builds the MLIR path (`--use-mlir`) by default, through
`PACKAGECONFIG[mlir]`. openembedded-core builds LLVM without MLIR and no layer
in the OpenEmbedded index has an MLIR recipe, so `mlir-native` builds the
`mlir` directory of the same `llvm-project` checkout standalone against
`llvm-native`, the way openembedded-core's `lld` recipe does. It is the
longest task in the layer (30 minutes at `-j 3`); to leave it out:

```
PACKAGECONFIG:remove:pn-cmc-native = "mlir"
```

## Requirements

LLVM 21 or newer. `walnascar` carries 20.1, so the recipe configures and then
fails its version floor; every older series fails the same way.

`LAYERSERIES_COMPAT` names `blacksail` alone, which is narrower than that floor
and deliberately so. `wrynose` carries LLVM 22 and would very likely build, but
nothing here has been run against it, and this layer declares what was verified
rather than what ought to work. Add it once a build says so.

## Use

[BUILDING.md](BUILDING.md) has the whole thing from an empty machine: host
packages, the locale BitBake insists on, which repositories to clone now that
poky's master is dormant, and the `local.conf` settings. Once a build
directory exists:

```bash
bitbake-layers add-layer /path/to/C_Minus_Language/meta-cminus
bitbake cmc-native          # the compiler
bitbake cminus-hello        # a C- program built for the target
```

To put the example in an image:

```
IMAGE_INSTALL:append = " cminus-hello"
```

## Status

With the current pin, against openembedded-core master `b62d5e6` and LLVM
23.1.3, [scripts/check-examples.sh](scripts/check-examples.sh) builds every
program in `tests/exec` with the `cmc` this layer builds and runs it under
user-mode qemu, through the LLVM path and both MLIR generators, at `-O0` and
`-O2`:

| `MACHINE` | Target | LLVM path | `--use-mlir=std` | `--use-mlir=cminus` |
| :- | :- | :- | :- | :- |
| `qemux86` | `i686-oe-linux` | 10/10 | 10/10 | 10/10 |
| `qemux86-64` | `x86_64-oe-linux` | 10/10 | 10/10 | 10/10 |
| `qemuarm64` | `aarch64-oe-linux` | 10/10 | 10/10 | 10/10 |
| `qemuriscv64` | `riscv64-oe-linux` | 10/10 | 10/10 | 10/10 |

The script fails if any program's output or exit status differs from its
`.expected` file; it was checked to report a deliberately wrong one.

Rebuilt for `qemux86-64` against openembedded-core master `b62d5e6`
(`blacksail`, 2026-10-29) with LLVM 23.1.3: 1414 tasks, all succeeded, and
`cminus-hello` prints 6 under `qemu-x86_64`. LLVM 23 is what made this
rebuild necessary; see the fifth row of the table below.

Before that, built end to end against openembedded-core master with LLVM
22.1.8, on two machines. 1035 tasks each, all succeeded:

| `MACHINE` | Result |
| :- | :- |
| `qemux86-64` | x86-64 binary, prints 6 under `qemu-x86_64` |
| `qemuarm64` | ARM aarch64 binary, prints 6 under `qemu-aarch64` |

Nothing in the recipes changed between the two: `cminus-hello` passes
`${TARGET_SYS}` to `cmc --target`, so the machine setting carries through.

The package is what `IMAGE_INSTALL` would install, and it is well formed:

```
-rwxr-xr-x root/root  67592  ./usr/bin/cminus-hello
Package: cminus-hello   Architecture: cortexa57   Depends: libc6 (>= 2.44+git...)
```

Stripped, with the symbols split into `-dbg`, and the libc dependency detected
automatically.

It reaches an image too. With `IMAGE_INSTALL:append = " cminus-hello"`,
`bitbake core-image-minimal` for `qemux86-64` puts it in the rootfs:

```
$ grep cminus-hello tmp/deploy/images/qemux86-64/*.manifest
core-image-minimal-qemux86-64.rootfs.manifest:cminus-hello x86-64-v3 1.0-r0
```

Rebuilt from an empty build directory against `b62d5e6` with LLVM 23.1.3 and
`cmc` built with its MLIR path: 5466 tasks, all succeeded. The binary in the
rootfs runs against the rootfs's own libc rather than the host's:

```
$ R=tmp/work/qemux86_64-oe-linux/core-image-minimal/1.0/rootfs
$ echo "270 192" | qemu-x86_64 -L $R -E LD_LIBRARY_PATH=/usr/lib:/lib $R/usr/bin/cminus-hello
6
```

The image was built for `qemux86-64` only. The `qemuarm64` one stops inside
openembedded-core rather than here: `procps` 4.0.7 fails to link for aarch64
against the GCC 16 that master currently carries.

Five things only the real build could find, in the order they appeared:

| Symptom | Cause |
| :- | :- |
| `do_unpack` fatal | `S = "${WORKDIR}/git"` is now rejected; oe-core sets `S` itself |
| `find_package(LLVM)` in a runtime-only build | `SRCREV` pinned a commit older than the CMake split |
| QA: no `GNU_HASH` | `${LDFLAGS}` was not passed to the driver, only `${CC}` |
| `DEPENDS = "cmc-native"` unresolved | `inherit native` alone leaves `PN` as `cmc`; the file needs the `-native` suffix |
| `cmc` aborts in `cminus-hello:do_compile` | LLVM 23's `BasicBlock::getTerminator()` asserts on a block without one, which `cmc` used to test for |

The fourth one showed up in variable expansion, the rest only when tasks ran.
The CI job only parses the layer, so it cannot catch any of them.

`mlir-native` took five more, each a property of building MLIR standalone
against an installed LLVM 23 rather than in-tree; the recipe comments say
which line answers which:

| Symptom | Cause |
| :- | :- |
| QA: `LIC_FILES_CHKSUM` mismatch | `mlir/LICENSE.TXT` leaves out the legacy NCSA licence that `llvm/LICENSE.TXT` has |
| `BuiltinLocationAttributes.h.inc: No such file` | the `mlir-irdl-to-cpp` object does not wait for the generated headers |
| `cc1plus` killed | out of memory at `-j 6` on 15 GB; MLIR needs `-j 3` there |
| `cannot find -lMLIR` | `MLIR_LINK_MLIR_DYLIB` follows `llvm-native`'s `LLVM_LINK_LLVM_DYLIB=ON`, but a standalone build makes no `libMLIR.so` |
| `'mlir-tblgen' … missing` | without `LLVM_BUILD_UTILS` it is not installed; LLVM 23 also stopped exporting it as a CMake target, which `cmc`'s CMakeLists now works around |

## Bumping the revision

`SRCREV` pins the revision that was verified rather than a moving tag. To move
it, set it to a pushed commit that contains `CMINUS_BUILD_COMPILER`,
`CMINUS_BUILD_RUNTIME` and `LICENSE`, and rerun `bitbake cminus-hello`.

`PV` describes the pinned revision, not the branch tip, so it moves with
`SRCREV` and not with a release. The pin currently sits past v0.1.3, on the
LLVM 23 fix and the MLIR path's cross compilation, so `PV` is `0.1.3+git`.
Both recipes share the pin, so that the compiler and the runtime it links in
come from one commit. After moving it, `scripts/check-examples.sh` is the
check that matters.
