SUMMARY = "Parsing gigabytes of JSON per second"
DESCRIPTION = "simdjson is a C++ library that parser json very efficiently"
HOMEPAGE = "https://simdjson.org/"
BUGTRACKER = "https://github.com/simdjson/simdjson/issues"

LICENSE = "Apache-2.0"
LIC_FILES_CHKSUM = "file://LICENSE;md5=08ad3d5aa0308e4e47849861fbb9da7b"

SRC_URI = "git://github.com/simdjson/simdjson.git;protocol=https;branch=master"

SRCREV = "f25d5f9fc244b24ee9a24933d1ecc685f6d300d4"

S = "${WORKDIR}/git"

inherit cmake

EXTRA_OECMAKE = "-DSIMDJSON_BUILD_TESTING=OFF"
