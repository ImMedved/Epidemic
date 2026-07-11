#pragma once

#include "Epidemic/Runtime/Serialization/archive_reader.h"
#include "Epidemic/Runtime/Serialization/archive_value.h"
#include "Epidemic/Runtime/Serialization/archive_writer.h"
#include "Epidemic/Runtime/Serialization/serialization_error.h"

#include <vector>

namespace epidemic::runtime
{
class InMemoryArchiveWriter final : public IArchiveWriter
{
  public:
    InMemoryArchiveWriter();

    [[nodiscard]] foundation::Result<void> BeginObject(std::string_view name) override;
    [[nodiscard]] foundation::Result<void> EndObject() override;
    [[nodiscard]] foundation::Result<void> WriteString(std::string_view name, std::string_view value) override;
    [[nodiscard]] foundation::Result<void> WriteUInt64(std::string_view name, std::uint64_t value) override;
    [[nodiscard]] foundation::Result<void> WriteInt64(std::string_view name, std::int64_t value) override;
    [[nodiscard]] foundation::Result<void> WriteDouble(std::string_view name, double value) override;
    [[nodiscard]] foundation::Result<void> WriteBool(std::string_view name, bool value) override;

    [[nodiscard]] ArchiveObjectPtr Snapshot() const;

  private:
    [[nodiscard]] foundation::Result<void> WriteValue(std::string_view name, ArchiveValue value);

    ArchiveObjectPtr root_;
    std::vector<ArchiveObjectPtr> stack_;
};

class InMemoryArchiveReader final : public IArchiveReader
{
  public:
    explicit InMemoryArchiveReader(ArchiveObjectPtr root);

    [[nodiscard]] foundation::Result<void> BeginObject(std::string_view name) override;
    [[nodiscard]] foundation::Result<void> EndObject() override;
    [[nodiscard]] foundation::Result<std::string> ReadString(std::string_view name) const override;
    [[nodiscard]] foundation::Result<std::uint64_t> ReadUInt64(std::string_view name) const override;
    [[nodiscard]] foundation::Result<std::int64_t> ReadInt64(std::string_view name) const override;
    [[nodiscard]] foundation::Result<double> ReadDouble(std::string_view name) const override;
    [[nodiscard]] foundation::Result<bool> ReadBool(std::string_view name) const override;

  private:
    [[nodiscard]] const ArchiveObject* CurrentObject() const;
    [[nodiscard]] foundation::Result<const ArchiveValue*> FindValue(std::string_view name) const;

    ArchiveObjectPtr root_;
    std::vector<const ArchiveObject*> stack_;
};
} // namespace epidemic::runtime
