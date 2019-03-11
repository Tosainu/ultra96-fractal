SUMMARY = "Recipe for  build an external kernel-module-fractal Linux kernel module"
SECTION = "PETALINUX/modules"
LICENSE = "GPLv2 | MIT"
LIC_FILES_CHKSUM = "file://COPYING;md5=b4f53902635ed0c03c89257898d34521"

inherit module

SRC_URI = "file://Makefile \
           file://fractal.c \
	   file://COPYING \
          "

S = "${WORKDIR}"

# The inherit of module.bbclass will automatically name module packages with
# "kernel-module-" prefix as required by the oe-core build environment.
