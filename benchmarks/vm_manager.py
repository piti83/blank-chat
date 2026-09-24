import subprocess
import sys
from pathlib import Path

sys.path.append(str(Path(__file__).resolve().parent.parent / "scripts"))

from utils import change_to_project_root, print_info, print_success, print_error, run_command

VM_SERVER = "bc-server"
VM_CLIENT_1 = "bc-client-1"
VM_CLIENT_2 = "bc-client-2"
VMS = [VM_SERVER, VM_CLIENT_1, VM_CLIENT_2]


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


def make_qemu_backing_path_accessible(project_root: Path, vms_dir: Path, base_dir: Path) -> None:
    """
    QEMU runs under the system libvirt account, not as the invoking user.
    The backing-file path therefore needs execute permission on directories.
    """
    runtime_dir = project_root / ".chutney"

    for directory in (runtime_dir, vms_dir, base_dir):
        directory.mkdir(parents=True, exist_ok=True)
        directory.chmod(0o755)


def stage_base_image(source: Path, destination: Path) -> Path:
    """
    Create a benchmark-private copy/reflink of a Yocto WIC image.

    The deploy image is never exposed directly to libvirt/QEMU. The staged
    image is read-only from QEMU's point of view and may safely be used as a
    QCOW2 backing file.
    """
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.parent.chmod(0o755)

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

    # The system libvirt QEMU process must be able to read the backing image.
    # The staged base contains a fresh benchmark image, not runtime secrets.
    destination.chmod(0o644)

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
        ]
    )


def destroy_vms():
    print_info("Cleaning up KVM environment (removing old VMs)...")
    for vm in VMS:
        res = subprocess.run(
            ["virsh", "-c", "qemu:///system", "domstate", vm],
            capture_output=True,
            text=True,
        )
        if res.returncode == 0:
            if "running" in res.stdout:
                run_command(
                    ["virsh", "-c", "qemu:///system", "destroy", vm],
                    exit_on_fail=False,
                    fail_msg=f"Failed to stop {vm}",
                )
            run_command(
                [
                    "virsh",
                    "-c",
                    "qemu:///system",
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
            "qemu:///system",
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

    vms_dir = project_root / ".chutney" / "vms"
    base_dir = vms_dir / "base"

    make_qemu_backing_path_accessible(project_root, vms_dir, base_dir)

    server_disk = vms_dir / "server.qcow2"
    client1_disk = vms_dir / "client1.qcow2"
    client2_disk = vms_dir / "client2.qcow2"

    destroy_vms()

    staged_server_wic = stage_base_image(
        server_wic, base_dir / "server-base.wic"
    )
    staged_client_wic = stage_base_image(
        client_wic, base_dir / "client-base.wic"
    )

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
