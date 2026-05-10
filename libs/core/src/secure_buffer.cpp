#include "core/secure_buffer.h"

#include <utility>

namespace bc::core {

SecureBuffer::SecureBuffer(std::size_t size) : bufferSize(size)
{
    if (size > 0) {
        // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
        bufferData = new BufferType[size]();
    } else {
        bufferData = nullptr;
    }
}

SecureBuffer::~SecureBuffer() noexcept
{
    delete[] bufferData;
}

SecureBuffer::SecureBuffer(SecureBuffer&& other) noexcept
{
    bufferSize = std::exchange(other.bufferSize, 0);
    bufferData = std::exchange(other.bufferData, nullptr);
}

auto SecureBuffer::operator=(SecureBuffer&& other) noexcept -> SecureBuffer&
{
    if (this != &other) {
        delete[] this->bufferData;
        this->bufferSize = std::exchange(other.bufferSize, 0);
        this->bufferData = std::exchange(other.bufferData, nullptr);
    }
    return *this;
}

auto SecureBuffer::Data() noexcept -> BufferType*
{
    return bufferData;
}

auto SecureBuffer::Data() const noexcept -> const BufferType*
{
    return bufferData;
}

auto SecureBuffer::Size() const noexcept -> std::size_t
{
    return bufferSize;
}

auto SecureBuffer::IsEmpty() const noexcept -> bool
{
    return bufferSize == 0;
}

auto SecureBuffer::AsSpan() const noexcept -> std::span<const BufferType>
{
    return {bufferData, bufferSize};
}

auto SecureBuffer::AsMutableSpan() noexcept -> std::span<BufferType>
{
    return {bufferData, bufferSize};
}

} // namespace bc::core
