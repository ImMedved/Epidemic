#pragma once

#include "archive_tree.h"
#include "Epidemic/Runtime/Serialization/archive_reader.h"
#include "Epidemic/Runtime/Serialization/archive_writer.h"
#include "Epidemic/Runtime/Serialization/serialization_error.h"
#include "Epidemic/Runtime/Serialization/serialization_services.h"

#include <vector>

namespace epidemic::runtime
{
class InMemoryArchiveWriter final : public IArchiveWriter
{
  public:
    InMemoryArchiveWriter();

    [[nodiscard]] foundation::Result<void> BeginObject(std::string_view name) override;
    [[nodiscard]] foundation::Result<void> EndObject() override;
    [[nodiscard]] foundation::Result<void> BeginArray(std::string_view name, std::size_t size) override;
    [[nodiscard]] foundation::Result<void> BeginArrayElement(std::size_t index) override;
    [[nodiscard]] foundation::Result<void> EndArrayElement() override;
    [[nodiscard]] foundation::Result<void> EndArray() override;

    [[nodiscard]] foundation::Result<void> WriteString(std::string_view name, std::string_view value) override;
    [[nodiscard]] foundation::Result<void> WriteUInt64(std::string_view name, std::uint64_t value) override;
    [[nodiscard]] foundation::Result<void> WriteInt64(std::string_view name, std::int64_t value) override;
    [[nodiscard]] foundation::Result<void> WriteDouble(std::string_view name, double value) override;
    [[nodiscard]] foundation::Result<void> WriteBool(std::string_view name, bool value) override;
    [[nodiscard]] foundation::Result<void> WriteBytes(std::string_view name, std::span<const std::byte> value) override;
    [[nodiscard]] foundation::Result<void> WriteNull(std::string_view name) override;
    [[nodiscard]] foundation::Result<SerializedDocument> Finalize(foundation::StringId type_id, SchemaVersion schema_version) override;

    [[nodiscard]] ArchiveObjectPtr Snapshot() const;

  private:
    enum class ContextKind { Object, Array, ArrayElement };
    struct Context
    {
        ContextKind kind = ContextKind::Object;
        ArchiveObjectPtr object{};
        ArchiveArrayPtr array{};
    };

    [[nodiscard]] foundation::Result<void> EnsureWritable() const;
    [[nodiscard]] foundation::Result<void> WriteValue(std::string_view name, ArchiveValue value);

    ArchiveObjectPtr root_;
    std::vector<Context> stack_;
    bool finalized_ = false;
};

class InMemoryArchiveReader final : public IArchiveReader
{
  public:
    explicit InMemoryArchiveReader(SerializedDocument document);
    explicit InMemoryArchiveReader(ArchiveObjectPtr root);

    [[nodiscard]] foundation::StringId GetTypeId() const override;
    [[nodiscard]] SchemaVersion GetSchemaVersion() const override;
    [[nodiscard]] std::uint32_t GetFormatVersion() const override;

    [[nodiscard]] foundation::Result<void> BeginObject(std::string_view name) override;
    [[nodiscard]] foundation::Result<void> EndObject() override;
    [[nodiscard]] foundation::Result<std::size_t> BeginArray(std::string_view name) override;
    [[nodiscard]] foundation::Result<void> BeginArrayElement(std::size_t index) override;
    [[nodiscard]] foundation::Result<void> EndArrayElement() override;
    [[nodiscard]] foundation::Result<void> EndArray() override;

    [[nodiscard]] foundation::Result<std::string> ReadString(std::string_view name) const override;
    [[nodiscard]] foundation::Result<std::uint64_t> ReadUInt64(std::string_view name) const override;
    [[nodiscard]] foundation::Result<std::int64_t> ReadInt64(std::string_view name) const override;
    [[nodiscard]] foundation::Result<double> ReadDouble(std::string_view name) const override;
    [[nodiscard]] foundation::Result<bool> ReadBool(std::string_view name) const override;
    [[nodiscard]] foundation::Result<std::vector<std::byte>> ReadBytes(std::string_view name) const override;
    [[nodiscard]] foundation::Result<bool> IsNull(std::string_view name) const override;

  private:
    enum class ContextKind { Object, Array, ArrayElement };
    struct Context
    {
        ContextKind kind = ContextKind::Object;
        const ArchiveObject* object = nullptr;
        const ArchiveArray* array = nullptr;
    };

    [[nodiscard]] const ArchiveObject* CurrentObject() const;
    [[nodiscard]] foundation::Result<const ArchiveValue*> FindValue(std::string_view name) const;

    SerializedDocument document_{};
    std::vector<Context> stack_;
};

class InMemoryArchiveFactory final : public IArchiveFactory
{
  public:
    [[nodiscard]] std::unique_ptr<IArchiveWriter> CreateWriter() const override;
    [[nodiscard]] foundation::Result<std::unique_ptr<IArchiveReader>> CreateReader(const SerializedDocument& document) const override;
};
} // namespace epidemic::runtime
