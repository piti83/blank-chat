#!/usr/bin/env python3
from __future__ import annotations

import sys
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parents[2]
REPL_CPP = PROJECT_ROOT / "libs/cli/src/repl.cpp"
TCP_CLIENT_CPP = PROJECT_ROOT / "libs/network/src/tcp_client.cpp"

BT05_MARKERS = (
    "BT05_EVENT QUEUE",
    "BT05_EVENT RX",
    "BT05_EVENT ACK",
)
BT06_CLOCK_MARKER = "BT06_EVENT CLOCK"
BT06_MAP_MARKER = "BT06_EVENT MAP"
BT06_TX_MARKER = "BT06_EVENT TX"


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(
            f"{label}: expected exactly one matching source fragment, found {count}. "
            "The benchmark branch probably changed; inspect the source before applying diagnostics."
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


def apply_bt05_and_bt06_map(text: str) -> str:
    bt05_present = [marker in text for marker in BT05_MARKERS]
    map_present = BT06_MAP_MARKER in text

    if all(bt05_present) and map_present:
        print("[+] BT-05 QUEUE/RX/ACK and BT-06 MAP diagnostics are already present")
        return text

    if any(bt05_present) or map_present:
        raise RuntimeError(
            "Only part of the BT-05/BT-06 message diagnostics is present in repl.cpp. "
            "Restore repl.cpp to the branch version and rerun --prepare."
        )

    text = ensure_chrono(text)

    text = replace_once(
        text,
        '''    auto frame = bc::protocol::Frame::CreatePush(contact->txMailboxId, std::move(*ciphertextOpt));
    {
        std::scoped_lock lock(outboxMutex);
        outbox.push(std::move(frame));
    }

    PrintThreadSafe("Message queued for transmission.\\n");
''',
        '''    auto frame = bc::protocol::Frame::CreatePush(contact->txMailboxId, std::move(*ciphertextOpt));

    const auto bt06FrameHash = bc::core::HashPayload(frame.Serialize());
    BC_INFO("BT06_EVENT MAP frame_hash={} msg_id={}", bt06FrameHash, msgId);

    {
        std::scoped_lock lock(outboxMutex);
        outbox.push(std::move(frame));
    }

    const auto bt05QueueTimestampNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                          std::chrono::system_clock::now().time_since_epoch())
                                          .count();
    BC_INFO("BT05_EVENT QUEUE timestamp_ns={} msg_id={}", bt05QueueTimestampNs, msgId);

    PrintThreadSafe("Message queued for transmission.\\n");
''',
        "QUEUE + MAP diagnostics",
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

    print("[+] Applied BT-05 QUEUE/RX/ACK and BT-06 MAP diagnostics")
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


def apply_bt06_tx(text: str) -> str:
    if BT06_TX_MARKER in text:
        print("[+] BT-06 transport TX diagnostic is already present")
        return text

    text = ensure_chrono(text)
    text = replace_once(
        text,
        '''    writeInProgress = true;
    boost::asio::async_write(socket, boost::asio::buffer(writeQueue.front()),
''',
        '''    writeInProgress = true;

    const auto& bt06TxFrame = writeQueue.front();
    const auto bt06TxAction =
        bt06TxFrame.empty() ? 0U : static_cast<unsigned int>(bt06TxFrame.front());
    const auto bt06TxBytes = bt06TxFrame.size();
    const auto bt06TxFrameHash = bc::core::HashPayload(bt06TxFrame);
    const auto bt06TxTimestampNs =
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count();

    BC_INFO("BT06_EVENT TX timestamp_ns={} action={} bytes={} frame_hash={}",
            bt06TxTimestampNs, bt06TxAction, bt06TxBytes, bt06TxFrameHash);

    boost::asio::async_write(socket, boost::asio::buffer(writeQueue.front()),
''',
        "BT-06 transport TX diagnostic",
    )

    print("[+] Applied BT-06 transport TX diagnostic")
    return text


def main() -> int:
    repl_text = REPL_CPP.read_text(encoding="utf-8")
    repl_text = apply_bt05_and_bt06_map(repl_text)
    repl_text = apply_bt06_clock(repl_text)

    tcp_text = TCP_CLIENT_CPP.read_text(encoding="utf-8")
    tcp_text = apply_bt06_tx(tcp_text)

    REPL_CPP.write_text(repl_text, encoding="utf-8")
    TCP_CLIENT_CPP.write_text(tcp_text, encoding="utf-8")

    print("[i] Rebuild the Yocto client image before running BT-05/BT-06.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"[!] Failed to apply BT-05/BT-06 diagnostics: {exc}", file=sys.stderr)
        raise SystemExit(1)
