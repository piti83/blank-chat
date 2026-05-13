#!/usr/bin/env python3

import argparse

from utils import change_to_project_root, print_info, print_success, run_command


def main():
    project_root = change_to_project_root()

    parser = argparse.ArgumentParser(
        description="Automate Yocto environment setup and building."
    )
    parser.add_argument(
        "--sdk",
        action="store_true",
        help="Generate the cross-compilation SDK instead of the full system image.",
    )
    parser.add_argument(
        "--target",
        choices=["server", "client", "both"],
        default="both",
        help="Which production ISO to build (default: both).",
    )
    args = parser.parse_args()

    yocto_base = project_root.parent / "yocto-dev"
    poky_dir = yocto_base / "poky"
    meta_oe_dir = yocto_base / "meta-openembedded"
    meta_tor_dir = yocto_base / "meta-tor"
    build_dir = poky_dir / "build"
    meta_blankchat_dir = project_root / "meta-blankchat"

    print_info("Verifying Yocto environment...")

    if not yocto_base.exists():
        yocto_base.mkdir(parents=True)

    if not poky_dir.exists():
        print_info("Cloning Poky environment (scarthgap)...")
        run_command(
            [
                "git",
                "clone",
                "git://git.yoctoproject.org/poky",
                "-b",
                "scarthgap",
                str(poky_dir),
            ]
        )

    if not meta_oe_dir.exists():
        print_info("Cloning meta-openembedded layer (scarthgap)...")
        run_command(
            [
                "git",
                "clone",
                "git://git.openembedded.org/meta-openembedded",
                "-b",
                "scarthgap",
                str(meta_oe_dir),
            ]
        )

    if not meta_tor_dir.exists():
        print_info("Cloning meta-tor layer (scarthgap)...")
        run_command(
            [
                "git",
                "clone",
                "https://github.com/embetrix/meta-tor.git",
                "-b",
                "scarthgap",
                str(meta_tor_dir),
            ]
        )

    if args.sdk:
        bitbake_target = "bitbake -c populate_sdk blankchat-image-server"
        success_msg = "Yocto: SDK generated successfully! Check yocto-dev/poky/build/tmp/deploy/sdk/"
        print_info("Starting BitBake engine to generate SDK...")
    else:
        if args.target == "server":
            bitbake_target = "bitbake blankchat-image-server"
            success_msg = "Yocto: Server ISO generated successfully!"
        elif args.target == "client":
            bitbake_target = "bitbake blankchat-image-client"
            success_msg = "Yocto: Client ISO generated successfully!"
        else:
            bitbake_target = "bitbake blankchat-image-server blankchat-image-client"
            success_msg = "Yocto: BOTH Server and Client ISOs generated successfully!"
        print_info(f"Starting BitBake engine to build the {args.target} image(s)...")

    bash_command = f"""
            cd {poky_dir}
            source oe-init-build-env {build_dir}

            bitbake-layers add-layer {meta_oe_dir}/meta-oe 2>/dev/null || true
            bitbake-layers add-layer {meta_oe_dir}/meta-python 2>/dev/null || true
            bitbake-layers add-layer {meta_oe_dir}/meta-networking 2>/dev/null || true
            bitbake-layers add-layer {meta_tor_dir} 2>/dev/null || true
            bitbake-layers add-layer {meta_blankchat_dir} 2>/dev/null || true

            if ! grep -q 'MACHINE = "genericx86-64"' conf/local.conf; then
                echo 'MACHINE = "genericx86-64"' >> conf/local.conf
            fi

            if ! grep -q 'INHERIT += "externalsrc"' conf/local.conf; then
                echo 'INHERIT += "externalsrc"' >> conf/local.conf
                echo 'EXTERNALSRC:pn-blank-chat = "{project_root}"' >> conf/local.conf
            fi

            if ! grep -q 'DISTRO_FEATURES:append = " systemd usrmerge"' conf/local.conf; then
                echo 'DISTRO_FEATURES:append = " systemd usrmerge"' >> conf/local.conf
                echo 'VIRTUAL-RUNTIME_init_manager = "systemd"' >> conf/local.conf
                echo 'DISTRO_FEATURES_BACKFILL_CONSIDERED = "sysvinit"' >> conf/local.conf
                echo 'VIRTUAL-RUNTIME_initscripts = ""' >> conf/local.conf
            fi

            if ! grep -q 'IMAGE_FEATURES:remove = "read-only-rootfs"' conf/local.conf; then
                echo 'IMAGE_FEATURES:remove = "read-only-rootfs"' >> conf/local.conf
            fi

            sed -i '/UNINATIVE_MAXGLIBCVERSION/d' conf/local.conf

            {bitbake_target}
            """

    run_command(["bash", "-c", bash_command], fail_msg="Yocto operation failed.")
    print_success(success_msg)


if __name__ == "__main__":
    main()
