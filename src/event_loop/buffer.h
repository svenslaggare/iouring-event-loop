#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>
#include <memory>

namespace event_loop {
    class BufferData {
    private:
        std::size_t mUseCount = 0;
        std::size_t mSize = 0;
        std::unique_ptr<std::uint8_t[]> mData;
    public:
        explicit BufferData(std::size_t size);

        std::size_t size() const;
        std::uint8_t* data() const;
        void clear();

        std::size_t useCount() const;
        void increaseUse();
        void decreaseUse();
    };

    class Buffer {
    private:
        BufferData* mUnderlying = nullptr;
        std::size_t mOffset = 0;
        std::size_t mSize = 0;

        void decreaseUse();
        Buffer(BufferData* data, std::size_t offset, std::size_t size);
    public:
        explicit Buffer(std::size_t size);
        ~Buffer();

        static Buffer fromString(const std::string_view& string);

        Buffer(const Buffer& other);
        Buffer& operator=(const Buffer& other);

        Buffer(Buffer&& other) noexcept;
        Buffer& operator=(Buffer&& other) noexcept;

        std::size_t size() const;
        std::uint8_t* data() const;
        void clear();

        Buffer slice(std::size_t offset, std::size_t size);

        std::size_t useCount() const;
    };
}