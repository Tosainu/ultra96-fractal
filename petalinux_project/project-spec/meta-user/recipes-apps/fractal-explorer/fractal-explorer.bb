#
# This file is the fractal-explorer recipe.
#

SUMMARY = "Simple fractal-explorer application"
SECTION = "PETALINUX/apps"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://main.cc \
           file://Makefile \
           git://github.com/memononen/nanovg.git;protocol=https;destsuffix=nanovg;rev=f4069e6a1ad5da430fb0a9c57476d5ddc2ff89b2 \
          "

S = "${WORKDIR}"

DEPENDS = " \
          cairo \
          virtual/egl \
          "

do_compile() {
	     oe_runmake
}

do_install() {
	     install -d ${D}${bindir}
	     install -m 0755 fractal-explorer ${D}${bindir}
}
