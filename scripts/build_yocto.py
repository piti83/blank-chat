#!/usr/bin/env python3

import argparse

from utils import change_to_project_root, print_info, print_success, run_command


def main():
    project_root = change_to_project_root()

    # Configure command line arguments
    parser = argparse.ArgumentParser(
        description="Automate Yocto environment setup and building."
    )
    parser.add_argument(
        "--sdk",
        action="store_true",
        help="Generate the cross-compilation SDK instead of the full system image.",
    )
    args = parser.parse_args()

    yocto_base = project_root.parent / "yocto-dev"
    poky_dir = yocto_base / "poky"
    meta_oe_dir = yocto_base / "meta-openembedded"
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

    # Determine what we are building based on the flag
    if args.sdk:
        bitbake_target = "bitbake -c populate_sdk blankchat-image-minimal"
        success_msg = "Yocto: SDK generated successfully! Check yocto-dev/poky/build/tmp/deploy/sdk/"
        print_info("Starting BitBake engine to generate SDK...")
    else:
        bitbake_target = "bitbake blankchat-image-minimal"
        success_msg = "Yocto: System image generated successfully!"
        print_info("Starting BitBake engine to build the image...")

    bash_command = f"""
        cd {poky_dir}
        source oe-init-build-env {build_dir}

        # Add layers. Redirect errors to /dev/null and add '|| true'
        # so the script doesn't fail if layers are already added.
        bitbake-layers add-layer {meta_oe_dir}/meta-oe 2>/dev/null || true
        bitbake-layers add-layer {meta_blankchat_dir} 2>/dev/null || true

        # FIX FOR ARCH LINUX: Force Yocto to accept glibc 2.43 so it uses uninative cache
        if ! grep -q "UNINATIVE_MAXGLIBCVERSION" conf/local.conf; then
            echo 'UNINATIVE_MAXGLIBCVERSION = "2.43"' >> conf/local.conf
        fi

        # Trigger the build
        {bitbake_target}
        """

    run_command(["bash", "-c", bash_command], fail_msg="Yocto operation failed.")
    print_success(success_msg)


if __name__ == "__main__":
    main()
