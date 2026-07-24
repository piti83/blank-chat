#ifndef BC_LIBS_DOMAIN_CLIENT_INCLUDE_CONVERSATION_CACHE_H_
#define BC_LIBS_DOMAIN_CLIENT_INCLUDE_CONVERSATION_CACHE_H_

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <protocol/frame.h>

namespace bc::domain::client {

enum class MessageDirection : std::uint8_t { INBOUND, OUTBOUND };

enum class MessageStatus : std::uint8_t { PENDING_ACK, DELIVERED, FAILED };

struct CacheEntry
{
    std::string id;
    std::uint64_t timestamp{0};
    MessageDirection direction{MessageDirection::OUTBOUND};
    std::string alias;
    MessageStatus status{MessageStatus::FAILED};
    bc::protocol::Payload payload;
};
class ConversationCache
{
public:
    ConversationCache() = default;
    ~ConversationCache() = default;

    ConversationCache(const ConversationCache&) = delete;
    auto operator=(const ConversationCache&) -> ConversationCache& = delete;
    ConversationCache(ConversationCache&&) = default;
    auto operator=(ConversationCache&&) -> ConversationCache& = default;

    auto Initialize(const std::filesystem::path& cacheDirectory) -> bool;

    auto AppendMessage(const CacheEntry& entry) -> void;

    auto UpdateMessageStatus(std::string_view alias, std::string_view messageId,
                             MessageStatus newStatus) -> void;

    [[nodiscard]] auto LoadHistory(std::string_view alias) const -> std::vector<CacheEntry>;

    auto DeleteHistory(std::string_view alias) -> bool;

private:
    [[nodiscard]] auto GetFilePathForAlias(std::string_view alias) const -> std::filesystem::path;

    std::filesystem::path cacheDir;
};

} // namespace bc::domain::client

#endif // BC_LIBS_DOMAIN_CLIENT_INCLUDE_CONVERSATION_CACHE_H_
