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

The recipes parse, and `bitbake -n` resolves the full task graph for all three
against openembedded-core master. They have not been built end to end: doing so
compiles LLVM from source, which takes hours.
