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

    parser = argparse.ArgumentParser(
        description="Upload the Yocto SDK to GitLab Package Registry."
    )
    parser.add_argument(
        "--version", default="1.0.0", help="Version of the SDK package (default: 1.0.0)"
    )
    args = parser.parse_args()

    gitlab_token = os.environ.get("GITLAB_TOKEN")
    project_id = os.environ.get("GITLAB_PROJECT_ID")

    if not gitlab_token or not project_id:
        print_error("Missing environment variables!")
        print_info(
            "Please set GITLAB_TOKEN and GITLAB_PROJECT_ID before running this script."
        )
        print_info(
            "Example: GITLAB_TOKEN=your_secret GITLAB_PROJECT_ID=123456 python3 scripts/upload_sdk.py"
        )
        sys.exit(1)

    yocto_base = project_root.parent / "yocto-dev"
    sdk_dir = yocto_base / "poky" / "build" / "tmp" / "deploy" / "sdk"

    if not sdk_dir.exists():
        print_error(f"SDK directory not found: {sdk_dir}")
        print_info(
            "Did you generate the SDK first using 'python3 scripts/build_yocto.py --sdk'?"
        )
        sys.exit(1)

    print_info("Searching for the SDK installation script...")

    sdk_files = list(sdk_dir.glob("poky-glibc-x86_64-blankchat-image-minimal-*.sh"))

    if not sdk_files:
        print_error("No SDK .sh file found in the deploy directory.")
        sys.exit(1)

    sdk_file = max(sdk_files, key=os.path.getmtime)
    filename = sdk_file.name
    print_info(f"Found SDK file: {filename}")

    url = f"https://gitlab.com/api/v4/projects/{project_id}/packages/generic/yocto-sdk/{args.version}/{filename}"

    print_info(f"Uploading to GitLab Package Registry (Version: {args.version})...")
    print_info(
        "This might take a while depending on your upload speed (SDK is usually large)."
    )

    curl_cmd = [
        "curl",
        "--header",
        f"PRIVATE-TOKEN: {gitlab_token}",
        "--upload-file",
        str(sdk_file),
        url,
    ]

    run_command(
        curl_cmd,
        fail_msg="Upload failed. Check your token, project ID, and network connection.",
    )
    print_success("SDK uploaded successfully to GitLab Package Registry!")


if __name__ == "__main__":
    main()
