#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
import os
import socket
import struct
import sys
import time
from dataclasses import asdict, dataclass, field
from pathlib import Path

ACTION_PUSH = 0x01
ACTION_POLL = 0x02
ACTION_AUTH_CHALLENGE = 0x04
ACTION_AUTH_RESPONSE = 0x05

MAILBOX_SIZE = 16
FRAME_HEADER_SIZE = 21
TOR_CELL_PAYLOAD_SIZE = 498
CONTROL_PAYLOAD_SIZE = TOR_CELL_PAYLOAD_SIZE - FRAME_HEADER_SIZE
CHALLENGE_SIZE = 32
MAX_PAYLOAD_SIZE = 1024 * 1024
ZERO_MAILBOX = b"\x00" * MAILBOX_SIZE


class TestError(RuntimeError):
    pass


class ProtocolError(TestError):
    pass


class SocksError(TestError):
    pass


@dataclass
class CheckResult:
    name: str
    passed: bool
    detail: str


@dataclass
class Summary:
    onion: str
    passed: bool = False
    duration_seconds: float = 0.0
    checks: list[CheckResult] = field(default_factory=list)


def recv_exact(sock: socket.socket, size: int) -> bytes:
    data = bytearray()
    while len(data) < size:
        chunk = sock.recv(size - len(data))
        if not chunk:
            raise ConnectionError(
                f"connection closed while receiving {size} bytes "
                f"({len(data)} bytes received)"
            )
        data.extend(chunk)
    return bytes(data)


def build_frame(action: int, mailbox_id: bytes, payload: bytes = b"") -> bytes:
    if len(mailbox_id) != MAILBOX_SIZE:
        raise ValueError("bad mailbox size")
    if len(payload) > MAX_PAYLOAD_SIZE:
        raise ValueError("payload too large")

    return (
        bytes([action])
        + mailbox_id
        + struct.pack("<I", len(payload))
        + payload
    )


def pad_control_payload(payload: bytes = b"") -> bytes:
    if len(payload) > CONTROL_PAYLOAD_SIZE:
        raise ValueError("control payload too large")

    return payload + b"\x00" * (CONTROL_PAYLOAD_SIZE - len(payload))


def send_frame(
    sock: socket.socket,
    action: int,
    mailbox_id: bytes,
    payload: bytes = b"",
) -> None:
    sock.sendall(build_frame(action, mailbox_id, payload))


def recv_frame(sock: socket.socket) -> tuple[int, bytes, bytes]:
    header = recv_exact(sock, FRAME_HEADER_SIZE)
    action = header[0]
    mailbox = header[1:17]
    payload_length = struct.unpack("<I", header[17:21])[0]

    if payload_length > MAX_PAYLOAD_SIZE:
        raise ProtocolError(f"oversized payload: {payload_length}")

    payload = recv_exact(sock, payload_length) if payload_length else b""
    return action, mailbox, payload


def socks5_connect(
    socks_host: str,
    socks_port: int,
    onion: str,
    target_port: int,
    timeout: float,
) -> socket.socket:
    onion_bytes = onion.encode("ascii")
    if not onion_bytes or len(onion_bytes) > 255:
        raise SocksError("invalid onion hostname length")

    sock = socket.create_connection((socks_host, socks_port), timeout=timeout)
    sock.settimeout(timeout)

    try:
        sock.sendall(b"\x05\x01\x00")
        if recv_exact(sock, 2) != b"\x05\x00":
            raise SocksError("SOCKS5 NoAuth method rejected")

        request = (
            b"\x05\x01\x00\x03"
            + bytes([len(onion_bytes)])
            + onion_bytes
            + struct.pack(">H", target_port)
        )
        sock.sendall(request)

        version, reply, reserved, address_type = recv_exact(sock, 4)
        if version != 5 or reserved != 0:
            raise SocksError("invalid SOCKS5 response")
        if reply != 0:
            raise SocksError(f"SOCKS5 CONNECT failed with code 0x{reply:02x}")

        if address_type == 1:
            recv_exact(sock, 4)
        elif address_type == 3:
            recv_exact(sock, recv_exact(sock, 1)[0])
        elif address_type == 4:
            recv_exact(sock, 16)
        else:
            raise SocksError(f"unsupported SOCKS5 address type: {address_type}")

        recv_exact(sock, 2)
        return sock
    except Exception:
        sock.close()
        raise


def receive_challenge(sock: socket.socket) -> bytes:
    action, mailbox, payload = recv_frame(sock)

    if action != ACTION_AUTH_CHALLENGE:
        raise ProtocolError(
            f"expected AUTH_CHALLENGE, received action=0x{action:02x}"
        )
    if mailbox != ZERO_MAILBOX:
        raise ProtocolError("AUTH_CHALLENGE used non-zero mailbox ID")
    if len(payload) != CONTROL_PAYLOAD_SIZE:
        raise ProtocolError(
            f"AUTH_CHALLENGE payload has {len(payload)} bytes, "
            f"expected {CONTROL_PAYLOAD_SIZE}"
        )

    return payload[:CHALLENGE_SIZE]


def pow_is_valid(challenge: bytes, nonce: bytes) -> bool:
    if len(challenge) != CHALLENGE_SIZE:
        raise ValueError("bad challenge size")
    if len(nonce) != 8:
        raise ValueError("bad nonce size")

    digest = hashlib.blake2b(challenge + nonce, digest_size=16).digest()
    return digest[0] == 0 and digest[1] < 0x10


def solve_pow(challenge: bytes) -> bytes:
    for nonce in range(1, 1 << 64):
        nonce_bytes = struct.pack("<Q", nonce)
        if pow_is_valid(challenge, nonce_bytes):
            return nonce_bytes

    raise TestError("unable to solve PoW")


def find_invalid_nonce(challenge: bytes) -> bytes:
    for nonce in range(0, 1 << 64):
        nonce_bytes = struct.pack("<Q", nonce)
        if not pow_is_valid(challenge, nonce_bytes):
            return nonce_bytes

    raise TestError("unable to find invalid PoW nonce")


def close_quietly(sock: socket.socket | None) -> None:
    if sock is None:
        return

    try:
        sock.shutdown(socket.SHUT_RDWR)
    except OSError:
        pass
    sock.close()


def wait_for_rejection(sock: socket.socket, timeout: float) -> str:
    deadline = time.monotonic() + timeout
    old_timeout = sock.gettimeout()

    try:
        while time.monotonic() < deadline:
            remaining = deadline - time.monotonic()
            sock.settimeout(min(0.5, max(remaining, 0.05)))

            try:
                data = sock.recv(4096)
            except socket.timeout:
                continue
            except (ConnectionResetError, ConnectionAbortedError, BrokenPipeError) as exc:
                return f"connection reset/aborted: {exc}"
            except OSError as exc:
                raise TestError(f"unexpected socket error while waiting for rejection: {exc}") from exc

            if data == b"":
                return "connection closed by server"

            raise TestError(
                f"server returned {len(data)} unexpected bytes instead of rejecting connection"
            )

        raise TestError(
            f"connection remained open for {timeout:.1f}s after rejected operation"
        )
    finally:
        try:
            sock.settimeout(old_timeout)
        except OSError:
            pass


def connect_and_receive_challenge(args: argparse.Namespace) -> tuple[socket.socket, bytes]:
    last_error: Exception | None = None

    for attempt in range(1, args.connect_retries + 1):
        sock: socket.socket | None = None
        try:
            sock = socks5_connect(
                args.socks_host,
                args.socks_port,
                args.onion,
                args.target_port,
                args.timeout,
            )
            challenge = receive_challenge(sock)
            return sock, challenge
        except Exception as exc:
            last_error = exc
            close_quietly(sock)
            if attempt < args.connect_retries:
                time.sleep(args.retry_delay)

    raise TestError(
        f"could not establish test connection after {args.connect_retries} attempts: "
        f"{last_error}"
    )


def test_valid_pow(args: argparse.Namespace) -> str:
    sock: socket.socket | None = None

    try:
        sock, challenge = connect_and_receive_challenge(args)
        nonce = solve_pow(challenge)

        send_frame(
            sock,
            ACTION_AUTH_RESPONSE,
            ZERO_MAILBOX,
            pad_control_payload(nonce),
        )

        mailbox = os.urandom(MAILBOX_SIZE)
        send_frame(
            sock,
            ACTION_POLL,
            mailbox,
            pad_control_payload(),
        )

        action, response_mailbox, payload = recv_frame(sock)

        if action != ACTION_POLL:
            raise TestError(
                f"authenticated POLL returned action=0x{action:02x}, expected POLL"
            )
        if response_mailbox != mailbox:
            raise TestError("authenticated POLL returned a different mailbox ID")
        if payload != pad_control_payload():
            raise TestError(
                f"authenticated empty POLL returned unexpected payload of {len(payload)} bytes"
            )

        return "valid PoW accepted and authenticated POLL returned expected empty response"
    finally:
        close_quietly(sock)


def test_invalid_pow(args: argparse.Namespace) -> str:
    sock: socket.socket | None = None

    try:
        sock, challenge = connect_and_receive_challenge(args)
        nonce = find_invalid_nonce(challenge)

        if pow_is_valid(challenge, nonce):
            raise TestError("internal error: generated nonce unexpectedly satisfies PoW")

        send_frame(
            sock,
            ACTION_AUTH_RESPONSE,
            ZERO_MAILBOX,
            pad_control_payload(nonce),
        )

        mailbox = os.urandom(MAILBOX_SIZE)
        try:
            send_frame(
                sock,
                ACTION_POLL,
                mailbox,
                pad_control_payload(),
            )
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError) as exc:
            return f"invalid PoW rejected before POLL could be sent: {exc}"

        return wait_for_rejection(sock, args.timeout)
    finally:
        close_quietly(sock)


def test_pre_auth_poll(args: argparse.Namespace) -> str:
    sock: socket.socket | None = None

    try:
        sock, _ = connect_and_receive_challenge(args)
        mailbox = os.urandom(MAILBOX_SIZE)

        try:
            send_frame(
                sock,
                ACTION_POLL,
                mailbox,
                pad_control_payload(),
            )
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError) as exc:
            return f"pre-auth POLL rejected during send: {exc}"

        return wait_for_rejection(sock, args.timeout)
    finally:
        close_quietly(sock)


def test_pre_auth_push(args: argparse.Namespace) -> str:
    sock: socket.socket | None = None

    try:
        sock, _ = connect_and_receive_challenge(args)
        mailbox = os.urandom(MAILBOX_SIZE)
        payload = b"BT01_PREAUTH_PUSH"

        try:
            send_frame(sock, ACTION_PUSH, mailbox, payload)
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError) as exc:
            return f"pre-auth PUSH rejected during send: {exc}"

        return wait_for_rejection(sock, args.timeout)
    finally:
        close_quietly(sock)


def add_check(summary: Summary, name: str, passed: bool, detail: str) -> None:
    summary.checks.append(CheckResult(name=name, passed=passed, detail=detail))
    status = "PASS" if passed else "FAIL"
    print(f"[{status}] {name}")
    if not passed:
        print(f"       {detail}")


def write_summary(path: str | None, summary: Summary) -> None:
    if not path:
        return

    output = Path(path)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(
        json.dumps(asdict(summary), indent=2) + "\n",
        encoding="utf-8",
    )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="BT-01 protocol-level Challenge-Response + PoW test"
    )
    parser.add_argument("--onion", required=True)
    parser.add_argument("--socks-host", default="127.0.0.1")
    parser.add_argument("--socks-port", type=int, default=9050)
    parser.add_argument("--target-port", type=int, default=80)
    parser.add_argument("--timeout", type=float, default=15.0)
    parser.add_argument("--connect-retries", type=int, default=3)
    parser.add_argument("--retry-delay", type=float, default=1.0)
    parser.add_argument("--summary-json")
    parser.add_argument("--probe-only", action="store_true")

    args = parser.parse_args()

    if args.timeout <= 0:
        parser.error("--timeout must be > 0")
    if args.connect_retries <= 0:
        parser.error("--connect-retries must be > 0")
    if args.retry_delay < 0:
        parser.error("--retry-delay must be >= 0")
    if "://" in args.onion:
        parser.error("--onion must be a hostname, not a URL")

    return args


def run_probe_only(args: argparse.Namespace) -> int:
    sock: socket.socket | None = None
    try:
        sock, _ = connect_and_receive_challenge(args)
        return 0
    except Exception:
        return 1
    finally:
        close_quietly(sock)


def main() -> int:
    args = parse_args()

    if args.probe_only:
        return run_probe_only(args)

    summary = Summary(onion=args.onion)
    started = time.monotonic()

    try:
        try:
            detail = test_valid_pow(args)
            add_check(summary, "valid PoW accepted", True, detail)
            add_check(summary, "authenticated POLL accepted", True, detail)
        except Exception as exc:
            detail = str(exc)
            add_check(summary, "valid PoW accepted", False, detail)
            add_check(summary, "authenticated POLL accepted", False, detail)

        try:
            detail = test_invalid_pow(args)
            add_check(summary, "invalid PoW rejected", True, detail)
        except Exception as exc:
            add_check(summary, "invalid PoW rejected", False, str(exc))

        try:
            detail = test_pre_auth_poll(args)
            add_check(summary, "pre-auth POLL rejected", True, detail)
        except Exception as exc:
            add_check(summary, "pre-auth POLL rejected", False, str(exc))

        try:
            detail = test_pre_auth_push(args)
            add_check(summary, "pre-auth PUSH rejected", True, detail)
        except Exception as exc:
            add_check(summary, "pre-auth PUSH rejected", False, str(exc))

        summary.passed = all(check.passed for check in summary.checks)

        print()
        print("BT-01: PASS" if summary.passed else "BT-01: FAIL")
        return 0 if summary.passed else 1
    except KeyboardInterrupt:
        print("\nBT-01: INTERRUPTED", file=sys.stderr)
        return 130
    finally:
        summary.duration_seconds = round(time.monotonic() - started, 3)
        write_summary(args.summary_json, summary)


if __name__ == "__main__":
    sys.exit(main())

