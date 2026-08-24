SUMMARY = "A C- program compiled into the image"
DESCRIPTION = "Shows how a .cm source becomes a target binary: cmc runs on \
the build host, generates code for the target, and links it against the \
target runtime using the toolchain BitBake provides."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://hello.cm"

# cmc-native is the compiler; cminus-runtime supplies the target archive it
# links against.
DEPENDS = "cmc-native cminus-runtime"

# file:// sources land in UNPACKDIR; S points there so ${S} and the unpacked
# file agree.
S = "${UNPACKDIR}"
B = "${WORKDIR}/build"

do_compile() {
    mkdir -p ${B}

    # --target picks the architecture. --cc takes the whole driver command,
    # not a program name, because the cross driver is useless without the
    # sysroot BitBake puts in CC.
    #
    # LDFLAGS belongs in that same command: cmc uses the driver only to link,
    # and the distribution's link flags are policy rather than decoration.
    # Leave them out and the binary gets a SysV hash instead of GNU_HASH,
    # which package QA rejects.
    ${STAGING_BINDIR_NATIVE}/cmc \
        --target ${TARGET_SYS} \
        --cc "${CC} ${LDFLAGS}" \
        --runtime ${STAGING_LIBDIR}/libcminus_rt.a \
        -O2 ${S}/hello.cm \
        -o ${B}/cminus-hello
}

do_install() {
    install -d ${D}${bindir}
    install -m 0755 ${B}/cminus-hello ${D}${bindir}/cminus-hello
}
