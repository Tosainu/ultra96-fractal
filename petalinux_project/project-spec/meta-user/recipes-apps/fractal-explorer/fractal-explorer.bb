#
# This file is the fractal-explorer recipe.
#

SUMMARY = "Simple fractal-explorer application"
SECTION = "PETALINUX/apps"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://main.cc \
           file://CMakeLists.txt \
           file://init \
          "

S = "${WORKDIR}"

inherit cmake pkgconfig update-rc.d

DEPENDS = "cairo \
           libdrm \
           virtual/egl \
          "

do_install:append() {
    install -Dm755 ${WORKDIR}/init ${D}${sysconfdir}/init.d/fractal-explorer
}

INITSCRIPT_NAME = "fractal-explorer"
INITSCRIPT_PARAMS = "start 99 5 2 . stop 20 0 1 6 ."
