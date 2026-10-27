#!/usr/bin/env python3
from __future__ import annotations

import sys
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parents[2]
REPL_CPP = PROJECT_ROOT / "libs/cli/src/repl.cpp"
MARKER = "BT02_EVENT TEXT_CIPHERTEXT"


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(
            f"{label}: expected exactly one matching source fragment, found {count}. "
            "The benchmark branch probably changed; inspect repl.cpp before applying diagnostics."
        )
    return text.replace(old, new, 1)


def main() -> int:
    text = REPL_CPP.read_text(encoding="utf-8")

    if MARKER in text:
        print("[+] BT-02 diagnostics are already present in libs/cli/src/repl.cpp")
        return 0

    text = replace_once(
        text,
        "#include <span>\n",
        "#include <span>\n#include <vector>\n",
        "include <vector>",
    )

    text = replace_once(
        text,
        '''    return payload;\n}\n\n} // namespace\n''',
        '''    return payload;\n}\n\ntemplate <typename Range>\nauto Bt02Fingerprint(const Range& data) -> std::string\n{\n    std::vector<std::uint8_t> bytes(data.begin(), data.end());\n    return bc::core::HashPayload(bytes);\n}\n\n} // namespace\n''',
        "BT-02 fingerprint helper",
    )

    text = replace_once(
        text,
        '''    if (!ciphertextOpt) {\n        std::cout << "Encryption failed for contact '" << alias << "'. Message dropped.\\n";\n        return;\n    }\n\n    auto frame = bc::protocol::Frame::CreatePush(contact->txMailboxId, std::move(*ciphertextOpt));\n''',
        '''    if (!ciphertextOpt) {\n        std::cout << "Encryption failed for contact '" << alias << "'. Message dropped.\\n";\n        return;\n    }\n\n    BC_INFO("BT02_EVENT TEXT_CIPHERTEXT alias={} msg_id={} mailbox={} ciphertext={}", alias, msgId,\n            bc::core::EncodeHex(contact->txMailboxId.AsSpan()),\n            bc::core::EncodeHex(std::span<const std::uint8_t>{ciphertextOpt->data(),\n                                                              ciphertextOpt->size()}));\n\n    auto frame = bc::protocol::Frame::CreatePush(contact->txMailboxId, std::move(*ciphertextOpt));\n''',
        "text ciphertext event",
    )

    text = replace_once(
        text,
        '''    if (crypto_sign_verify_detached(\n            reqDataOpt->signature.data(), reqDataOpt->ephemeralPublicKey.data(),\n            reqDataOpt->ephemeralPublicKey.size(), contact->publicKey.data()) != 0) {\n        BC_ERROR("Invalid signature on PFS_ROTATE_REQUEST from {}. Potential MITM attack!", alias);\n        return;\n    }\n\n    const bool initialRotation = !contact->initialPfsComplete;\n''',
        '''    if (crypto_sign_verify_detached(\n            reqDataOpt->signature.data(), reqDataOpt->ephemeralPublicKey.data(),\n            reqDataOpt->ephemeralPublicKey.size(), contact->publicKey.data()) != 0) {\n        BC_ERROR("Invalid signature on PFS_ROTATE_REQUEST from {}. Potential MITM attack!", alias);\n        return;\n    }\n\n    BC_INFO("BT02_EVENT PFS_ROTATE_REQUEST_VERIFIED alias={} peer_eph_fp={}", alias,\n            Bt02Fingerprint(reqDataOpt->ephemeralPublicKey));\n\n    const bool initialRotation = !contact->initialPfsComplete;\n''',
        "PFS request verification event",
    )

    text = replace_once(
        text,
        '''    auto myEphemeralOpt = bc::crypto::EphemeralKey::Generate();\n    if (!myEphemeralOpt) {\n        return;\n    }\n\n    std::array<std::uint8_t, bc::crypto::publicKeySize * 2> ackSignedData{};\n''',
        '''    auto myEphemeralOpt = bc::crypto::EphemeralKey::Generate();\n    if (!myEphemeralOpt) {\n        return;\n    }\n\n    BC_INFO("BT02_EVENT PFS_LOCAL_EPHEMERAL alias={} role=responder fp={}", alias,\n            Bt02Fingerprint(myEphemeralOpt->GetPublicKey()));\n\n    std::array<std::uint8_t, bc::crypto::publicKeySize * 2> ackSignedData{};\n''',
        "responder ephemeral event",
    )

    text = replace_once(
        text,
        '''        if (!initialRotation) {\n            contact->oldRxMailboxId = contact->rxMailboxId;\n\n            bc::core::SecureBuffer oldKey(bc::crypto::symmetricKeySize);\n            std::ranges::copy(contact->rxKey.AsSpan(), oldKey.AsMutableSpan().begin());\n            contact->oldRxKey = std::move(oldKey);\n        }\n\n        std::array<std::uint8_t, crypto_scalarmult_BYTES> sharedSecret{};\n''',
        '''        if (!initialRotation) {\n            contact->oldRxMailboxId = contact->rxMailboxId;\n\n            bc::core::SecureBuffer oldKey(bc::crypto::symmetricKeySize);\n            std::ranges::copy(contact->rxKey.AsSpan(), oldKey.AsMutableSpan().begin());\n            contact->oldRxKey = std::move(oldKey);\n\n            BC_INFO("BT02_EVENT OLD_RX_RETAINED alias={} old_mailbox={}", alias,\n                    bc::core::EncodeHex(contact->oldRxMailboxId->AsSpan()));\n        }\n\n        std::array<std::uint8_t, crypto_scalarmult_BYTES> sharedSecret{};\n''',
        "old RX retained event",
    )

    text = replace_once(
        text,
        '''    if (crypto_sign_verify_detached(ackDataOpt->signature.data(), ackSignedData.data(),\n                                    ackSignedData.size(), contact->publicKey.data()) != 0) {\n        BC_ERROR("Invalid signature on PFS_ROTATE_ACK from {}. Potential MITM attack!", alias);\n        return;\n    }\n\n    std::array<std::uint8_t, crypto_scalarmult_BYTES> sharedSecret{};\n''',
        '''    if (crypto_sign_verify_detached(ackDataOpt->signature.data(), ackSignedData.data(),\n                                    ackSignedData.size(), contact->publicKey.data()) != 0) {\n        BC_ERROR("Invalid signature on PFS_ROTATE_ACK from {}. Potential MITM attack!", alias);\n        return;\n    }\n\n    BC_INFO("BT02_EVENT PFS_ROTATE_ACK_VERIFIED alias={} peer_eph_fp={}", alias,\n            Bt02Fingerprint(ackDataOpt->ephemeralPublicKey));\n\n    std::array<std::uint8_t, crypto_scalarmult_BYTES> sharedSecret{};\n''',
        "PFS ACK verification event",
    )

    text = replace_once(
        text,
        '''    if (!plaintextOpt) {\n        PrintThreadSafe("Malformed or tampered PUSH message dropped silently.\\n");\n        return;\n    }\n\n    if (!usedOldKey && contact->oldRxMailboxId.has_value()) {\n        PrintThreadSafe(std::format(\n            "\\nTransition confirmed for {}. Dropping old mailbox safely.\\n>>> ", alias));\n        contact->oldRxMailboxId = std::nullopt;\n        contact->oldRxKey = std::nullopt;\n        addressBook.SaveToDisk();\n    }\n''',
        '''    if (!plaintextOpt) {\n        PrintThreadSafe("Malformed or tampered PUSH message dropped silently.\\n");\n        return;\n    }\n\n    if (usedOldKey && contact->oldRxMailboxId.has_value()) {\n        BC_INFO("BT02_EVENT OLD_RX_MESSAGE_ACCEPTED alias={} mailbox={}", alias,\n                bc::core::EncodeHex(contact->oldRxMailboxId->AsSpan()));\n    }\n\n    if (!usedOldKey && contact->oldRxMailboxId.has_value()) {\n        BC_INFO("BT02_EVENT OLD_RX_RETIRED alias={} old_mailbox={} new_mailbox={}", alias,\n                bc::core::EncodeHex(contact->oldRxMailboxId->AsSpan()),\n                bc::core::EncodeHex(contact->rxMailboxId.AsSpan()));\n        PrintThreadSafe(std::format(\n            "\\nTransition confirmed for {}. Dropping old mailbox safely.\\n>>> ", alias));\n        contact->oldRxMailboxId = std::nullopt;\n        contact->oldRxKey = std::nullopt;\n        addressBook.SaveToDisk();\n    }\n''',
        "old/new RX transition events",
    )

    text = replace_once(
        text,
        '''    std::string msgId(plaintextOpt->begin(), plaintextOpt->end());\n    cache.UpdateMessageStatus(alias, msgId, bc::domain::client::MessageStatus::DELIVERED);\n    PrintThreadSafe(std::format("\\nMessage DELIVERED to {}\\n>>> ", alias));\n''',
        '''    std::string msgId(plaintextOpt->begin(), plaintextOpt->end());\n    cache.UpdateMessageStatus(alias, msgId, bc::domain::client::MessageStatus::DELIVERED);\n    BC_INFO("BT02_EVENT ACK_DELIVERED alias={} msg_id={}", alias, msgId);\n    PrintThreadSafe(std::format("\\nMessage DELIVERED to {}\\n>>> ", alias));\n''',
        "ACK delivery event",
    )

    text = replace_once(
        text,
        '''    auto ephemeralOpt = bc::crypto::EphemeralKey::Generate();\n    if (!ephemeralOpt) {\n        BC_ERROR("Failed to generate ephemeral key for '{}'", alias);\n        return false;\n    }\n\n    std::array<std::uint8_t, bc::domain::client::cryptoSignBytes> signature{};\n''',
        '''    auto ephemeralOpt = bc::crypto::EphemeralKey::Generate();\n    if (!ephemeralOpt) {\n        BC_ERROR("Failed to generate ephemeral key for '{}'", alias);\n        return false;\n    }\n\n    BC_INFO("BT02_EVENT PFS_LOCAL_EPHEMERAL alias={} role=initiator fp={}", alias,\n            Bt02Fingerprint(ephemeralOpt->GetPublicKey()));\n\n    std::array<std::uint8_t, bc::domain::client::cryptoSignBytes> signature{};\n''',
        "initiator ephemeral event",
    )

    REPL_CPP.write_text(text, encoding="utf-8")
    print("[+] Applied BT-02 benchmark diagnostics to libs/cli/src/repl.cpp")
    print("[i] Rebuild the Yocto client image before running BT-02.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"[!] Failed to apply BT-02 diagnostics: {exc}", file=sys.stderr)
        raise SystemExit(1)
