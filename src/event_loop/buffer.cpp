#include <cstring>
#include <iostream>
#include "buffer.h"

namespace event_loop {
    BufferData::BufferData(std::size_t size)
        : mSize(size), mData(std::make_unique<std::uint8_t[]>(size)) {
        clear();
    }

    std::size_t BufferData::size() const {
        return mSize;
    }

    std::uint8_t* BufferData::data() const {
        return mData.get();
    }

    void BufferData::clear() {
        std::memset(mData.get(), 0, mSize);
    }

    std::size_t BufferData::useCount() const {
        return mUseCount;
    }

    void BufferData::increaseUse() {
        mUseCount++;
    }

    void BufferData::decreaseUse() {
        mUseCount--;
    }

    Buffer::Buffer(std::size_t size)
        : mUnderlying(new BufferData(size)) {
        mUnderlying->increaseUse();
    }

    Buffer::~Buffer() {
        decreaseUse();
    }

    Buffer Buffer::fromString(const std::string_view& string) {
        Buffer buffer { string.size() };
        memcpy(buffer.data(), string.data(), string.size());
        return buffer;
    }

    Buffer::Buffer(const Buffer& other)
        : mUnderlying(other.mUnderlying) {
        mUnderlying->increaseUse();
    }

    Buffer& Buffer::operator=(const Buffer& other) {
        if (&other != this) {
            decreaseUse();
            mUnderlying = other.mUnderlying;

            if (mUnderlying != nullptr) {
                mUnderlying->increaseUse();
            }
        }

        return *this;
    }

    Buffer::Buffer(Buffer&& other) noexcept
        : mUnderlying(other.mUnderlying) {
        other.mUnderlying = nullptr;
    }

    Buffer& Buffer::operator=(Buffer&& other) noexcept {
        if (&other != this) {
            decreaseUse();
            mUnderlying = other.mUnderlying;
            other.mUnderlying = nullptr;
        }

        return *this;
    }

    std::size_t Buffer::size() const {
        if (mUnderlying != nullptr) {
            return mUnderlying->size();
        }

        return 0;
    }

    std::uint8_t* Buffer::data() const {
        if (mUnderlying != nullptr) {
            return mUnderlying->data();
        }

        return nullptr;
    }

    void Buffer::clear() {
        if (mUnderlying != nullptr) {
            mUnderlying->clear();
        }
    }

    std::size_t Buffer::useCount() const {
        if (mUnderlying != nullptr) {
            return mUnderlying->useCount();
        }

        return 0;
    }

    void Buffer::decreaseUse() {
        if (mUnderlying != nullptr) {
            mUnderlying->decreaseUse();
            if (mUnderlying->useCount() == 0) {
                delete mUnderlying;
                mUnderlying = nullptr;
            }
        }
    }
}