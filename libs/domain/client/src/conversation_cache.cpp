#include "client/conversation_cache.h"

#include <algorithm>
#include <fstream>
#include <system_error>

#include <core/logger.h>
#include <core/string_utils.h>

#include <simdjson.h>

namespace {

constexpr auto DirToString(bc::domain::client::MessageDirection dir) -> std::string_view
{
    return dir == bc::domain::client::MessageDirection::INBOUND ? "inbound" : "outbound";
}

auto StringToDir(std::string_view str) -> bc::domain::client::MessageDirection
{
    return str == "inbound" ? bc::domain::client::MessageDirection::INBOUND
                            : bc::domain::client::MessageDirection::OUTBOUND;
}

constexpr auto StatusToString(bc::domain::client::MessageStatus status) -> std::string_view
{
    switch (status) {
    case bc::domain::client::MessageStatus::PENDING_ACK:
        return "pending_ack";
    case bc::domain::client::MessageStatus::DELIVERED:
        return "delivered";
    case bc::domain::client::MessageStatus::FAILED:
        return "failed";
    }
    return "failed";
}

auto StringToStatus(std::string_view str) -> bc::domain::client::MessageStatus
{
    if (str == "pending_ack") {
        return bc::domain::client::MessageStatus::PENDING_ACK;
    }
    if (str == "delivered") {
        return bc::domain::client::MessageStatus::DELIVERED;
    }
    return bc::domain::client::MessageStatus::FAILED;
}

[[nodiscard]] auto ParseCacheEntry(simdjson::ondemand::parser& parser, std::string_view line)
    -> std::optional<bc::domain::client::CacheEntry>
{
    simdjson::padded_string padded(line);
    simdjson::ondemand::document doc;
    auto error = parser.iterate(padded).get(doc);

    if (static_cast<bool>(error)) {
        BC_WARN("Failed to parse history line");
        return std::nullopt;
    }

    simdjson::ondemand::object obj;
    if (static_cast<bool>(doc.get(obj))) {
        return std::nullopt;
    }

    bc::domain::client::CacheEntry entry;
    std::string_view tempView;

    if (!static_cast<bool>(obj.find_field("id").get(tempView))) {
        entry.id = std::string(tempView);
    }

    std::uint64_t ts = 0;
    if (!static_cast<bool>(obj.find_field("timestamp").get(ts))) {
        entry.timestamp = ts;
    }

    if (!static_cast<bool>(obj.find_field("direction").get(tempView))) {
        entry.direction = StringToDir(tempView);
    }

    if (!static_cast<bool>(obj.find_field("alias").get(tempView))) {
        entry.alias = std::string(tempView);
    }

    if (!static_cast<bool>(obj.find_field("status").get(tempView))) {
        entry.status = StringToStatus(tempView);
    }

    if (!static_cast<bool>(obj.find_field("payload").get(tempView))) {
        entry.payload.resize(tempView.length() / 2);
        if (!bc::core::DecodeHexToArray(tempView, entry.payload)) {
            entry.payload.clear();
        }
    }

    return entry;
}

} // namespace

namespace bc::domain::client {

auto ConversationCache::Initialize(const std::filesystem::path& cacheDirectory) -> bool
{
    cacheDir = cacheDirectory;
    std::error_code ec;

    if (!std::filesystem::exists(cacheDir, ec)) {
        std::filesystem::create_directories(cacheDir, ec);
    }
    return !static_cast<bool>(ec);
}

auto ConversationCache::GetFilePathForAlias(std::string_view alias) const -> std::filesystem::path
{
    return cacheDir / ("history_" + std::string(alias) + ".jsonl");
}

auto ConversationCache::AppendMessage(const CacheEntry& entry) -> void
{
    auto path = GetFilePathForAlias(entry.alias);
    std::ofstream out(path, std::ios::app);
    if (!out) {
        BC_ERROR("Failed to open cache file for appending: {}", path.string());
        return;
    }

    out << "{\"id\":\"" << bc::core::EscapeJsonString(entry.id) << "\","
        << "\"timestamp\":" << entry.timestamp << ","
        << "\"direction\":\"" << DirToString(entry.direction) << "\","
        << "\"alias\":\"" << bc::core::EscapeJsonString(entry.alias) << "\","
        << "\"status\":\"" << StatusToString(entry.status) << "\","
        << "\"payload\":\"" << bc::core::EncodeHex(entry.payload) << "\"}\n";
}

auto ConversationCache::UpdateMessageStatus(std::string_view alias, std::string_view messageId,
                                            MessageStatus newStatus) -> void
{
    auto history = LoadHistory(alias);

    auto it = std::ranges::find_if(
        history, [messageId](const CacheEntry& entry) -> bool { return entry.id == messageId; });

    if (it == history.end()) {
        return;
    }

    it->status = newStatus;

    auto path = GetFilePathForAlias(alias);
    std::filesystem::path tempPath = path.string() + ".tmp";

    std::ofstream out(tempPath, std::ios::trunc);
    if (!out) {
        BC_ERROR("Failed to open temp file for status update: {}", tempPath.string());
        return;
    }

    for (const auto& entry : history) {
        out << "{\"id\":\"" << bc::core::EscapeJsonString(entry.id) << "\","
            << "\"timestamp\":" << entry.timestamp << ","
            << "\"direction\":\"" << DirToString(entry.direction) << "\","
            << "\"alias\":\"" << bc::core::EscapeJsonString(entry.alias) << "\","
            << "\"status\":\"" << StatusToString(entry.status) << "\","
            << "\"payload\":\"" << bc::core::EncodeHex(entry.payload) << "\"}\n";
    }
    out.close();

    std::error_code ec;
    std::filesystem::rename(tempPath, path, ec);
    if (ec) {
        BC_ERROR("Failed to atomically update history file: {}", ec.message());
    }
}

auto ConversationCache::LoadHistory(std::string_view alias) const -> std::vector<CacheEntry>
{
    std::vector<CacheEntry> result;
    auto path = GetFilePathForAlias(alias);

    if (!std::filesystem::exists(path)) {
        return result;
    }

    std::ifstream in(path);
    if (!in) {
        return result;
    }

    simdjson::ondemand::parser parser;
    std::string line;

    while (std::getline(in, line)) {
        if (line.empty()) {
            continue;
        }

        if (auto entryOpt = ParseCacheEntry(parser, line)) {
            result.push_back(std::move(*entryOpt));
        }
    }

    return result;
}

auto ConversationCache::DeleteHistory(std::string_view alias) -> bool
{
    auto path = GetFilePathForAlias(alias);
    std::error_code ec;
    return std::filesystem::remove(path, ec);
}

} // namespace bc::domain::client
