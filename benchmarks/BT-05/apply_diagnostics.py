#!/usr/bin/env python3
from __future__ import annotations

import sys
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parents[2]
REPL_CPP = PROJECT_ROOT / "libs/cli/src/repl.cpp"
BT05_MARKERS = (
    "BT05_EVENT QUEUE",
    "BT05_EVENT RX",
    "BT05_EVENT ACK",
)
BT06_CLOCK_MARKER = "BT06_EVENT CLOCK"


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(
            f"{label}: expected exactly one matching source fragment, found {count}. "
            "The benchmark branch probably changed; inspect repl.cpp before applying diagnostics."
        )
    return text.replace(old, new, 1)


def ensure_chrono(text: str) -> str:
    if "#include <chrono>\n" in text:
        return text
    return replace_once(
        text,
        "#include <algorithm>\n",
        "#include <algorithm>\n#include <chrono>\n",
        "include <chrono>",
    )


def apply_bt05(text: str) -> str:
    present = [marker in text for marker in BT05_MARKERS]
    if all(present):
        print("[+] BT-05 QUEUE/RX/ACK diagnostics are already present")
        return text
    if any(present):
        raise RuntimeError(
            "Only part of the BT-05 diagnostics is present in repl.cpp. "
            "Restore or inspect the file before applying the patch again."
        )

    text = ensure_chrono(text)
    text = replace_once(
        text,
        '''    {
        std::scoped_lock lock(outboxMutex);
        outbox.push(std::move(frame));
    }

    PrintThreadSafe("Message queued for transmission.\\n");
''',
        '''    {
        std::scoped_lock lock(outboxMutex);
        outbox.push(std::move(frame));
    }

    const auto bt05QueueTimestampNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                          std::chrono::system_clock::now().time_since_epoch())
                                          .count();
    BC_INFO("BT05_EVENT QUEUE timestamp_ns={} msg_id={}", bt05QueueTimestampNs, msgId);

    PrintThreadSafe("Message queued for transmission.\\n");
''',
        "QUEUE diagnostic",
    )
    text = replace_once(
        text,
        '''    cache.AppendMessage(entry);

    bc::protocol::Payload ackPayload(msgId.begin(), msgId.end());
''',
        '''    cache.AppendMessage(entry);

    const auto bt05RxTimestampNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                       std::chrono::system_clock::now().time_since_epoch())
                                       .count();
    BC_INFO("BT05_EVENT RX timestamp_ns={} msg_id={}", bt05RxTimestampNs, msgId);

    bc::protocol::Payload ackPayload(msgId.begin(), msgId.end());
''',
        "RX diagnostic",
    )
    text = replace_once(
        text,
        '''    cache.UpdateMessageStatus(alias, msgId, bc::domain::client::MessageStatus::DELIVERED);
    PrintThreadSafe(std::format("\\nMessage DELIVERED to {}\\n>>> ", alias));
''',
        '''    cache.UpdateMessageStatus(alias, msgId, bc::domain::client::MessageStatus::DELIVERED);

    const auto bt05AckTimestampNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                        std::chrono::system_clock::now().time_since_epoch())
                                        .count();
    BC_INFO("BT05_EVENT ACK timestamp_ns={} msg_id={}", bt05AckTimestampNs, msgId);

    PrintThreadSafe(std::format("\\nMessage DELIVERED to {}\\n>>> ", alias));
''',
        "ACK diagnostic",
    )
    print("[+] Applied BT-05 QUEUE/RX/ACK diagnostics")
    return text


def apply_bt06_clock(text: str) -> str:
    if BT06_CLOCK_MARKER in text:
        print("[+] BT-06 in-process clock diagnostic is already present")
        return text

    text = ensure_chrono(text)
    text = replace_once(
        text,
        '''auto Repl::HandleList() -> void
{
    auto aliases = addressBook.GetAllAliases();
''',
        '''auto Repl::HandleList() -> void
{
    const auto bt06ClockTimestampNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                          std::chrono::system_clock::now().time_since_epoch())
                                          .count();
    BC_INFO("BT06_EVENT CLOCK timestamp_ns={}", bt06ClockTimestampNs);

    auto aliases = addressBook.GetAllAliases();
''',
        "BT-06 clock diagnostic",
    )
    print("[+] Applied BT-06 in-process clock diagnostic")
    return text


def main() -> int:
    text = REPL_CPP.read_text(encoding="utf-8")
    text = apply_bt05(text)
    text = apply_bt06_clock(text)
    REPL_CPP.write_text(text, encoding="utf-8")
    print("[i] Rebuild the Yocto client image before running BT-05/BT-06.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"[!] Failed to apply BT-05/BT-06 diagnostics: {exc}", file=sys.stderr)
        raise SystemExit(1)
