#ifndef BC_LIBS_CORE_INCLUDE_SECUREBUFFER_H_
#define BC_LIBS_CORE_INCLUDE_SECUREBUFFER_H_

#include <span>

#include <core/core_types.h>

namespace bc::core {

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

    [[nodiscard]] auto Data() noexcept -> CoreByte*;
    [[nodiscard]] auto Data() const noexcept -> const CoreByte*;
    [[nodiscard]] auto Size() const noexcept -> std::size_t;
    [[nodiscard]] auto IsEmpty() const noexcept -> bool;
    [[nodiscard]] auto AsSpan() const noexcept -> std::span<const CoreByte>;
    [[nodiscard]] auto AsMutableSpan() noexcept -> std::span<CoreByte>;

private:
    std::size_t bufferSize{};
    CoreByte* bufferData{nullptr};
};

} // namespace bc::core

#endif
