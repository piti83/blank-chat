do_configure:prepend() {
    sed -i 's|struct open_how \*how|const struct open_how \*how|g' ${S}/ports/linux/openat2/wrapfuncs.in
}
