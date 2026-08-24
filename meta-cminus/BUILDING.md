# Building meta-cminus from scratch

These are the steps that were actually run, on Ubuntu 26.04 with 28 cores.
Expect a couple of hours and roughly 20 GB the first time: LLVM is built from
source, and so is the cross toolchain.

## 1. Host packages

BitBake needs a handful of tools beyond a normal build environment:

```bash
sudo apt install -y git python3 gawk wget diffstat chrpath cpio zstd lz4 file
```

## 2. Locale

BitBake refuses to start without `en_US.UTF-8`. A user-level locale via
`LOCPATH` is not enough — glibc does not honour it here.

```bash
sudo sed -i 's/^# *en_US.UTF-8 UTF-8/en_US.UTF-8 UTF-8/' /etc/locale.gen
sudo locale-gen
```

## 3. Get BitBake and openembedded-core

**Not poky.** Its master branch is no longer updated:

> The poky repository master branch is no longer being updated.

The pieces are separate repositories now.

```bash
mkdir -p ~/yocto/oe && cd ~/yocto/oe
git clone --depth 1 https://git.openembedded.org/bitbake
git clone --depth 1 https://git.openembedded.org/openembedded-core
```

A shallow clone is enough and comes to about 60 MB.

## 4. Initialise a build

`oe-init-build-env` lives in openembedded-core and looks for BitBake beside
it, so point `BITBAKEDIR` at the separate clone:

```bash
cd ~/yocto/oe
export BITBAKEDIR="$HOME/yocto/oe/bitbake"
source openembedded-core/oe-init-build-env build
```

Do not run this under `set -u`: the script reads `BBSERVER` before setting it.

## 5. Configure

Append to `conf/local.conf`:

```
BB_NUMBER_THREADS = "28"
PARALLEL_MAKE = "-j 28"

# Download finished task output where the input hashes match, instead of
# rebuilding it. This is what keeps LLVM from being compiled locally.
SSTATE_MIRRORS ?= "file://.* http://sstate.yoctoproject.org/all/PATH;downloadfilename=PATH"

# Only needed on a distribution that is not on Yocto's tested list.
SANITY_TESTED_DISTROS = ""
```

Set the thread counts to your core count.

## 6. Add the layer and build

```bash
bitbake-layers add-layer /path/to/C_Minus_Language/meta-cminus
bitbake cminus-hello
```

`bitbake cmc-native` alone builds just the compiler, which is most of the time
anyway.

## 7. Check what came out

```bash
cd ~/yocto/oe/build
W=tmp/work/x86-64-v3-oe-linux/cminus-hello/1.0

file $W/build/cminus-hello
readelf -d $W/build/cminus-hello | grep -E 'GNU_HASH|BIND_NOW'
echo "270 192" | qemu-x86_64 -L $W/recipe-sysroot $W/build/cminus-hello
```

The last line prints `6`. The packages land in
`tmp/deploy/ipk/x86-64-v3/cminus-hello*.ipk`.

To put the program in an image instead, add to `local.conf`:

```
IMAGE_INSTALL:append = " cminus-hello"
```

and build `core-image-minimal`.

## Another architecture

`MACHINE` in `local.conf` decides the target; the recipes follow it, since
`cminus-hello` passes `${TARGET_SYS}` to `cmc --target`.

```
MACHINE = "qemuarm64"
```

That rebuilds the cross toolchain for aarch64, so budget the time again.

## Disk

The build directory reaches about 18 GB, of which roughly 1 GB is downloads
and 0.5 GB sstate. `INHERIT += "rm_work"` in `local.conf` trades rebuild speed
for a much smaller `tmp/`.
