#!/usr/bin/env python3
from __future__ import annotations

import sys
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[2]
TCP_CLIENT_CPP = PROJECT_ROOT / "libs/network/src/tcp_client.cpp"
MARKER = "BT04_EVENT TX"


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(
            f"{label}: expected exactly one matching source fragment, found {count}. "
            "The benchmark branch probably changed; inspect tcp_client.cpp before applying diagnostics."
        )
    return text.replace(old, new, 1)


def main() -> int:
    text = TCP_CLIENT_CPP.read_text(encoding="utf-8")

    if MARKER in text:
        print("[+] BT-04 diagnostics are already present in libs/network/src/tcp_client.cpp")
        return 0

    text = replace_once(
        text,
        '#include <array>\n',
        '#include <array>\n#include <chrono>\n',
        'include <chrono>',
    )

    text = replace_once(
        text,
        '''    writeInProgress = true;\n    boost::asio::async_write(socket, boost::asio::buffer(writeQueue.front()),\n''',
        '''    writeInProgress = true;\n\n    const auto txTimestampNs = std::chrono::duration_cast<std::chrono::nanoseconds>(\n                                   std::chrono::steady_clock::now().time_since_epoch())\n                                   .count();\n    const auto& txFrame = writeQueue.front();\n    const auto txAction = txFrame.empty() ? 0U : static_cast<unsigned int>(txFrame.front());\n    BC_INFO("BT04_EVENT TX timestamp_ns={} action={} bytes={}", txTimestampNs, txAction,\n            txFrame.size());\n\n    boost::asio::async_write(socket, boost::asio::buffer(writeQueue.front()),\n''',
        'TX timestamp diagnostic',
    )

    TCP_CLIENT_CPP.write_text(text, encoding="utf-8")
    print("[+] Applied BT-04 TX timestamp diagnostics to libs/network/src/tcp_client.cpp")
    print("[i] Rebuild the Yocto client image before running BT-04.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"[!] Failed to apply BT-04 diagnostics: {exc}", file=sys.stderr)
        raise SystemExit(1)
