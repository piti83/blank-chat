import grp
import os
import pwd
import subprocess
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

sys.path.append(str(Path(__file__).resolve().parent.parent / "scripts"))

from utils import change_to_project_root, print_info, print_success, print_error, run_command

VM_SERVER = "bc-server"
VM_CLIENT_1 = "bc-client-1"
VM_CLIENT_2 = "bc-client-2"
VMS = [VM_SERVER, VM_CLIENT_1, VM_CLIENT_2]

LIBVIRT_URI = "qemu:///system"
LIBVIRT_POOL = "default"
STORAGE_SUBDIR = "blank-chat-benchmarks"


def get_yocto_deploy_dir(project_root: Path) -> Path:
    deploy_dir = (
        project_root.parent
        / "yocto-dev"
        / "poky"
        / "build"
        / "tmp"
        / "deploy"
        / "images"
        / "genericx86-64"
    )
    if not deploy_dir.exists():
        print_error(f"Yocto images directory not found: {deploy_dir}")
        sys.exit(1)
    return deploy_dir


def get_libvirt_pool_target() -> Path:
    result = subprocess.run(
        ["virsh", "-c", LIBVIRT_URI, "pool-dumpxml", LIBVIRT_POOL],
        text=True,
        capture_output=True,
    )

    if result.returncode == 0:
        try:
            root = ET.fromstring(result.stdout)
            path_text = root.findtext("./target/path")
            if path_text:
                return Path(path_text).resolve()
        except ET.ParseError:
            pass

    fallback = Path("/var/lib/libvirt/images")
    print_info(
        f"Could not resolve libvirt pool '{LIBVIRT_POOL}' target; "
        f"using fallback {fallback}"
    )
    return fallback


def prepare_storage_root(pool_target: Path) -> Path:
    """
    Create benchmark storage below the system libvirt image location.

    Keeping the complete QCOW2 backing chain outside /home avoids DAC traversal
    failures when QEMU runs as the dedicated libvirt user.
    """
    storage_root = pool_target / STORAGE_SUBDIR
    owner = pwd.getpwuid(os.getuid()).pw_name
    group = grp.getgrgid(os.getgid()).gr_name

    run_command(
        [
            "sudo",
            "install",
            "-d",
            "-m",
            "0755",
            "-o",
            owner,
            "-g",
            group,
            str(storage_root),
            str(storage_root / "base"),
        ],
        fail_msg=f"Failed to prepare libvirt benchmark storage: {storage_root}",
    )

    return storage_root


def stage_base_image(source: Path, destination: Path) -> Path:
    """
    Copy/reflink a Yocto WIC into benchmark-private libvirt storage.

    The original BitBake deploy artifact is never exposed directly to QEMU.
    """
    temporary = destination.with_name(destination.name + ".tmp")

    temporary.unlink(missing_ok=True)
    destination.unlink(missing_ok=True)

    print_info(f"Staging benchmark base image: {source.name} -> {destination}")
    run_command(
        [
            "cp",
            "--reflink=auto",
            "--sparse=always",
            "--",
            str(source),
            str(temporary),
        ],
        fail_msg=f"Failed to stage benchmark base image: {source}",
    )

    temporary.replace(destination)

    # Backing images are immutable inputs for the benchmark VM disks.
    destination.chmod(0o444)

    return destination.resolve()


def create_cow_disk(base_image: Path, target_disk: Path):
    if target_disk.exists():
        target_disk.unlink()

    print_info(f"Creating CoW disk: {target_disk.name} -> {base_image.name}")
    run_command(
        [
            "qemu-img",
            "create",
            "-f",
            "qcow2",
            "-F",
            "raw",
            "-b",
            str(base_image),
            str(target_disk),
        ],
        fail_msg=f"Failed to create QCOW2 disk: {target_disk}",
    )


def destroy_vms():
    print_info("Cleaning up KVM environment (removing old VMs)...")
    for vm in VMS:
        res = subprocess.run(
            ["virsh", "-c", LIBVIRT_URI, "domstate", vm],
            capture_output=True,
            text=True,
        )
        if res.returncode != 0:
            continue

        if "running" in res.stdout.lower():
            run_command(
                ["virsh", "-c", LIBVIRT_URI, "destroy", vm],
                exit_on_fail=False,
                fail_msg=f"Failed to stop {vm}",
            )

        run_command(
            [
                "virsh",
                "-c",
                LIBVIRT_URI,
                "undefine",
                vm,
                "--nvram",
                "--remove-all-storage",
            ],
            exit_on_fail=False,
            fail_msg=f"Failed to undefine {vm}",
        )


def create_and_start_vm(vm_name: str, disk_path: Path):
    print_info(f"Registering and starting VM {vm_name}...")
    run_command(
        [
            "/usr/bin/python3",
            "/usr/bin/virt-install",
            "--connect",
            LIBVIRT_URI,
            "--name",
            vm_name,
            "--memory",
            "2048",
            "--vcpus",
            "2",
            "--disk",
            f"path={disk_path},format=qcow2,bus=sata",
            "--import",
            "--os-variant",
            "generic",
            "--network",
            "network=default,model=virtio",
            "--boot",
            "uefi",
            "--graphics",
            "none",
            "--noautoconsole",
            "--quiet",
        ],
        fail_msg=f"Failed to create VM {vm_name}",
    )


def main():
    project_root = change_to_project_root()
    deploy_dir = get_yocto_deploy_dir(project_root)

    server_wic = (
        deploy_dir / "blankchat-image-server-genericx86-64.rootfs.wic"
    ).resolve()
    client_wic = (
        deploy_dir / "blankchat-image-client-genericx86-64.rootfs.wic"
    ).resolve()

    if not server_wic.exists() or not client_wic.exists():
        print_error(
            "Missing .wic files (server or client). Ensure build_yocto.py completed successfully."
        )
        sys.exit(1)

    # Stop/remove any previous benchmark domains before replacing their backing
    # chain.
    destroy_vms()

    pool_target = get_libvirt_pool_target()
    storage_root = prepare_storage_root(pool_target)
    base_dir = storage_root / "base"

    print_info(f"Using libvirt benchmark storage: {storage_root}")

    staged_server_wic = stage_base_image(
        server_wic, base_dir / "server-base.wic"
    )
    staged_client_wic = stage_base_image(
        client_wic, base_dir / "client-base.wic"
    )

    server_disk = storage_root / "server.qcow2"
    client1_disk = storage_root / "client1.qcow2"
    client2_disk = storage_root / "client2.qcow2"

    create_cow_disk(staged_server_wic, server_disk)
    create_cow_disk(staged_client_wic, client1_disk)
    create_cow_disk(staged_client_wic, client2_disk)

    print_success("Temporary images generated successfully.")

    create_and_start_vm(VM_SERVER, server_disk)
    create_and_start_vm(VM_CLIENT_1, client1_disk)
    create_and_start_vm(VM_CLIENT_2, client2_disk)

    print_success("All virtual machines have been started in the background.")


if __name__ == "__main__":
    main()
