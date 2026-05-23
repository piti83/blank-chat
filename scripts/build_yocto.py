#!/usr/bin/env python3

import argparse
import os
import sys

from utils import (
    change_to_project_root,
    print_error,
    print_info,
    print_success,
    run_command,
)


def main():
    project_root = change_to_project_root()
    workspace_root = project_root.parent

    parser = argparse.ArgumentParser(
        description="Automate Yocto environment setup and building inside Docker."
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
        help="Which production image to build (default: both).",
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

    dockerfile = project_root / "scripts" / "Dockerfile.yocto"

    if not dockerfile.exists():
        print_error(
            f"Cannot find {dockerfile}. Ensure the file exists in your scripts directory."
        )
        sys.exit(1)

    print_info("Ensuring Yocto Docker builder image is ready...")

    uid = os.getuid()
    gid = os.getgid()

    run_command(
        [
            "docker",
            "build",
            "-t",
            "blankchat-yocto-builder",
            "--build-arg",
            f"UID={uid}",
            "--build-arg",
            f"GID={gid}",
            "-f",
            str(dockerfile),
            str(project_root),
        ],
        fail_msg="Failed to build the Yocto Docker builder image.",
    )

    if args.sdk:
        bitbake_target = "bitbake -c populate_sdk blankchat-image-server"
        success_msg = "Yocto: SDK generated successfully! Check yocto-dev/poky/build/tmp/deploy/sdk/"
        print_info("Starting BitBake engine to generate SDK...")
    else:
        if args.target == "server":
            bitbake_target = "bitbake blankchat-image-server"
            success_msg = "Yocto: Server image generated successfully!"
        elif args.target == "client":
            bitbake_target = "bitbake blankchat-image-client"
            success_msg = "Yocto: Client image generated successfully!"
        else:
            bitbake_target = "bitbake blankchat-image-server blankchat-image-client"
            success_msg = "Yocto: Server and Client images generated successfully!"
        print_info(f"Starting BitBake engine to build the {args.target} image(s)...")

    bash_command = f"""
            cd {poky_dir}
            source oe-init-build-env {build_dir}

            bitbake-layers add-layer {meta_oe_dir}/meta-oe 2>/dev/null || true
            bitbake-layers add-layer {meta_oe_dir}/meta-python 2>/dev/null || true
            bitbake-layers add-layer {meta_oe_dir}/meta-networking 2>/dev/null || true
            bitbake-layers add-layer {meta_tor_dir} 2>/dev/null || true
            bitbake-layers add-layer {meta_blankchat_dir} 2>/dev/null || true

            echo 'MACHINE = "genericx86-64"' > conf/auto.conf
            echo 'DISTRO = "blankchat"' >> conf/auto.conf
            echo 'INHERIT += "externalsrc"' >> conf/auto.conf
            echo 'EXTERNALSRC:pn-blank-chat = "{project_root}"' >> conf/auto.conf

            sed -i '/UNINATIVE_MAXGLIBCVERSION/d' conf/local.conf

            {bitbake_target}
            """

    docker_cmd = [
        "docker",
        "run",
        "--rm",
        "-v",
        f"{workspace_root}:{workspace_root}",
        "-w",
        str(poky_dir),
    ]

    if sys.stdout.isatty():
        docker_cmd.append("-it")

    docker_cmd.extend(["blankchat-yocto-builder", "bash", "-c", bash_command])

    run_command(docker_cmd, fail_msg="Yocto compilation failed.")
    print_success(success_msg)


if __name__ == "__main__":
    main()
