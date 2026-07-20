#pragma once

#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

namespace epidemic::runtime
{
class IResourcePayload
{
  public:
    virtual ~IResourcePayload() = default;

    [[nodiscard]] virtual std::size_t GetSizeBytes() const noexcept = 0;
};

class ByteResourcePayload final : public IResourcePayload
{
  public:
    explicit ByteResourcePayload(std::vector<std::byte> bytes = {}) : bytes_(std::move(bytes))
    {
    }

    [[nodiscard]] std::size_t GetSizeBytes() const noexcept override
    {
        return bytes_.size();
    }

    [[nodiscard]] const std::vector<std::byte>& Bytes() const noexcept
    {
        return bytes_;
    }

  private:
    std::vector<std::byte> bytes_;
};

using ResourcePayloadPtr = std::shared_ptr<const IResourcePayload>;
} // namespace epidemic::runtime

