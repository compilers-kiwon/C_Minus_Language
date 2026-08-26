# Building meta-cminus from scratch

These are the steps that were actually run, on Ubuntu 26.04 with 28 cores.
Expect a couple of hours and roughly 20 GB the first time: LLVM is built from
source, and so is the cross toolchain.

## 1. Host packages

BitBake needs a handful of tools beyond a normal build environment:

```bash
sudo apt install -y git python3 gawk wget diffstat chrpath cpio zstd lz4 file \
                    rpcsvc-proto
```

`rpcsvc-proto` is there for `rpcgen`. BitBake lists it in `HOSTTOOLS`, and a
missing entry in that list stops the very first `bitbake` command a second in,
with the layer never read:

```
ERROR: The following required tools (as specified by HOSTTOOLS) appear to be
unavailable in PATH, please install them in order to proceed:
  rpcgen
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
BB_NUMBER_THREADS = "8"
PARALLEL_MAKE = "-j 8"

# Download finished task output where the input hashes match, instead of
# rebuilding it. This is what keeps LLVM from being compiled locally.
SSTATE_MIRRORS ?= "file://.* http://sstate.yoctoproject.org/all/PATH;downloadfilename=PATH"

# Only needed on a distribution that is not on Yocto's tested list.
SANITY_TESTED_DISTROS = ""
```

**Size these by memory, not by core count.** The two multiply: BitBake runs
`BB_NUMBER_THREADS` recipes at once and each of them may run `PARALLEL_MAKE`
compilers, so 28 and `-j 28` is up to 784 of them. Building one recipe never
comes close, which is how the mistake hides; building an image did, and the
kernel started killing processes:

```
Out of memory: Killed process 2189 (systemd)
```

Reckon on a gigabyte or two per compiler. 8 and `-j 8` is comfortable on a
32 GB machine.

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

and build `core-image-minimal`. It then appears in the rootfs manifest:

```
$ grep cminus-hello tmp/deploy/images/qemux86-64/*.manifest
core-image-minimal-qemux86-64.rootfs.manifest:cminus-hello x86-64-v3 1.0-r0
```

## After an interrupted build

Two things bite, and neither announces itself.

A half-finished git clone under `downloads/` survives `rm -rf tmp`, and
deleting the clone is not enough on its own: the `do_fetch` stamp still says
the source is there, so BitBake skips to `do_unpack` and fails with
`clone directory not available or not up to date`. `bitbake -c cleanall
<recipe>` clears workdir, sstate, download and stamp together.

Separately, `oe-init-build-env` takes the BitBake directory as its *second*
argument and exports it. One mistyped invocation therefore leaves a bad
`BITBAKEDIR` in the shell, and every later attempt inherits it and fails the
same way even after the command is corrected. A new terminal is the quickest
cure.

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
