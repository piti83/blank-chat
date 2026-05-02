#include <core/secure_buffer.h>
#include <cstdint>

namespace bc::core {

SecureBuffer::SecureBuffer(std::size_t size) : bufferSize(size)
{
    if (size > 0) {
        bufferData = new BufferType[size]();
    } else {
        bufferData = nullptr;
    }
}

} // namespace bc::core
