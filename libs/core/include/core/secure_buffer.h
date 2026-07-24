#ifndef BC_LIBS_CORE_INCLUDE_SECUREBUFFER_H_
#define BC_LIBS_CORE_INCLUDE_SECUREBUFFER_H_

#include <cstdint>
#include <span>

namespace bc::core {

using BufferType = std::uint8_t;

class SecureBuffer
{
public:
    SecureBuffer() noexcept = default;

    explicit SecureBuffer(std::size_t size);

    ~SecureBuffer() noexcept;

    SecureBuffer(const SecureBuffer&) = delete;
    auto operator=(const SecureBuffer&) -> SecureBuffer& = delete;

    SecureBuffer(SecureBuffer&& other) noexcept;
    auto operator=(SecureBuffer&& other) noexcept -> SecureBuffer&;

    [[nodiscard]] auto Data() noexcept -> BufferType*;
    [[nodiscard]] auto Data() const noexcept -> const BufferType*;
    [[nodiscard]] auto Size() const noexcept -> std::size_t;
    [[nodiscard]] auto IsEmpty() const noexcept -> bool;
    [[nodiscard]] auto AsSpan() const noexcept -> std::span<const BufferType>;
    [[nodiscard]] auto AsMutableSpan() noexcept -> std::span<BufferType>;

private:
    std::size_t bufferSize{};
    BufferType* bufferData{nullptr};
};

} // namespace bc::core

#endif
