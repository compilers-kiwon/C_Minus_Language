# meta-cminus

A Yocto layer that builds the C- compiler for the build host, its runtime for
the target, and an example program compiled into an image.

| Recipe | Runs on | Purpose |
| :- | :- | :- |
| `cmc-native` | build host | the compiler; `DEPENDS = "llvm-native"` |
| `cminus-runtime` | target | `libcminus_rt.a`, linked into compiled programs |
| `cminus-hello` | target | a `.cm` source turned into a binary in the image |

## Why three recipes

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

## Requirements

LLVM 21 or newer, so openembedded-core from `wrynose` onwards.  `walnascar`
carries LLVM 20.1 and the recipe will fail its version floor, which is why
`LAYERSERIES_COMPAT` names only the series that can work.

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

Built end to end against openembedded-core master (`blacksail`) with LLVM
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

The image was built for `qemux86-64` only. The `qemuarm64` one stops inside
openembedded-core rather than here: `procps` 4.0.7 fails to link for aarch64
against the GCC 16 that master currently carries.

Four things only the real build could find, in the order they appeared:

| Symptom | Cause |
| :- | :- |
| `do_unpack` fatal | `S = "${WORKDIR}/git"` is now rejected; oe-core sets `S` itself |
| `find_package(LLVM)` in a runtime-only build | `SRCREV` pinned a commit older than the CMake split |
| QA: no `GNU_HASH` | `${LDFLAGS}` was not passed to the driver, only `${CC}` |
| `DEPENDS = "cmc-native"` unresolved | `inherit native` alone leaves `PN` as `cmc`; the file needs the `-native` suffix |

The last one showed up in variable expansion, the rest only when tasks ran.

## Bumping the revision

`SRCREV` pins the revision that was verified rather than a moving tag. To move
it, set it to a pushed commit that contains `CMINUS_BUILD_COMPILER`,
`CMINUS_BUILD_RUNTIME` and `LICENSE`, and rerun `bitbake cminus-hello`.

`PV` describes the pinned revision, not the branch tip, so it moves with
`SRCREV` and not with a release. The pin currently sits between v0.1.2 and
v0.1.3, which changed nothing either recipe builds; chasing the newer tag
would advertise an upgrade over identical source and rebuild it to prove it.
