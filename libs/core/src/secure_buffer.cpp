#include "core/secure_buffer.h"

#include <utility>

#include <sodium.h>

#include <core/logger.h>

namespace bc::core {

SecureBuffer::SecureBuffer(std::size_t size)
    : bufferSize(size),
      bufferData(size > 0 ? static_cast<BufferType*>(sodium_malloc(size)) : nullptr)
{
    if (size > 0 && bufferData == nullptr) {
        BC_CRITICAL("sodium_malloc failed to allocate secure memory for SecureBuffer!");
        std::abort();
    }

    if (bufferData != nullptr) {
        sodium_memzero(bufferData, bufferSize);
    }
}

SecureBuffer::~SecureBuffer() noexcept
{
    if (bufferData != nullptr) {
        sodium_free(bufferData);
        bufferData = nullptr;
    }
}

SecureBuffer::SecureBuffer(SecureBuffer&& other) noexcept
{
    bufferSize = std::exchange(other.bufferSize, 0);
    bufferData = std::exchange(other.bufferData, nullptr);
}

auto SecureBuffer::operator=(SecureBuffer&& other) noexcept -> SecureBuffer&
{
    if (this != &other) {
        if (bufferData != nullptr) {
            sodium_free(bufferData);
        }
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
