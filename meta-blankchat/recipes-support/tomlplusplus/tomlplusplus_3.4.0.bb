SUMMARY = "Header-only TOML config file parser and serializer for C++17"
HOMEPAGE = "https://marzer.github.io/tomlplusplus/"
BUGTRACKER = "https://github.com/marzer/tomlplusplus/issues"

LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=90960f22c10049c117d56ed2ee5ee167"

SRC_URI = "git://github.com/marzer/tomlplusplus.git;protocol=https;nobranch=1"
SRCREV = "v3.4.0"

S = "${WORKDIR}/git"

inherit cmake
