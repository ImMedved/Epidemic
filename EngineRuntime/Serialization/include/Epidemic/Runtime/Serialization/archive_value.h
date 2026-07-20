#pragma once

#include "Epidemic/Foundation/string_id.h"
#include "Epidemic/Runtime/Serialization/schema_version.h"

#include <cstdint>
#include <memory>

namespace epidemic::runtime
{
class SerializedDocument
{
  public:
    SerializedDocument() = default;

    [[nodiscard]] foundation::StringId GetTypeId() const;
    [[nodiscard]] SchemaVersion GetSchemaVersion() const;
    [[nodiscard]] std::uint32_t GetFormatVersion() const;
    [[nodiscard]] bool IsValid() const noexcept;

  private:
    struct Impl;

    explicit SerializedDocument(std::shared_ptr<const Impl> impl);

    std::shared_ptr<const Impl> impl_;

    friend class InMemoryArchiveReader;
    friend class InMemoryArchiveWriter;
    friend class InMemoryArchiveFactory;
};
} // namespace epidemic::runtime
