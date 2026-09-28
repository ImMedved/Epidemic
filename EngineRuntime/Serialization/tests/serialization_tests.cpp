#include "Epidemic/Runtime/Serialization/archive_reader.h"
#include "Epidemic/Runtime/Serialization/archive_writer.h"
#include "Epidemic/Runtime/Serialization/migration.h"
#include "Epidemic/Runtime/Serialization/migration_executor.h"
#include "Epidemic/Runtime/Serialization/migration_registry.h"
#include "Epidemic/Runtime/Serialization/schema_version.h"
#include "Epidemic/Runtime/Serialization/serialization_error.h"
#include "Epidemic/Runtime/Serialization/serialization_services.h"
#include "Epidemic/Runtime/Serialization/serializer.h"
#include "Epidemic/Runtime/Serialization/serializer_registry.h"
#include "in_memory_archive.h"
#include "migration_registry.h"
#include "serializer_registry.h"

#include <cstddef>
#include <iostream>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace epidemic::runtime
{
struct SerializationRuntimeTestAccess
{
    static void FailNextObjectPublication(InMemoryArchiveWriter& writer) noexcept
    {
        writer.fail_next_object_publication_for_testing_ = true;
    }

    static void FailNextArrayPublication(InMemoryArchiveWriter& writer) noexcept
    {
        writer.fail_next_array_publication_for_testing_ = true;
    }

    static void FailNextArrayElementPublication(InMemoryArchiveWriter& writer) noexcept
    {
        writer.fail_next_array_element_publication_for_testing_ = true;
    }

    static void FailNextFieldPublication(InMemoryArchiveWriter& writer) noexcept
    {
        writer.fail_next_field_publication_for_testing_ = true;
    }

    static void FailNextFinalizeCandidate(InMemoryArchiveWriter& writer) noexcept
    {
        writer.fail_next_finalize_candidate_for_testing_ = true;
    }

    static void FailNextSerializerRegistrationAllocation(SerializerRegistry& registry) noexcept
    {
        registry.fail_next_registration_allocation_for_testing_ = true;
    }

    static void FailNextMigrationRegistrationAllocation(MigrationRegistry& registry) noexcept
    {
        registry.fail_next_registration_allocation_for_testing_ = true;
    }

    static void FailNextMigrationPathAllocation(MigrationRegistry& registry) noexcept
    {
        registry.fail_next_path_allocation_for_testing_ = true;
    }
};
} // namespace epidemic::runtime

namespace
{
using epidemic::foundation::Result;
using epidemic::foundation::StringId;
using epidemic::runtime::CreateSerializationError;
using epidemic::runtime::CreateSerializationServices;
using epidemic::runtime::IArchiveFactory;
using epidemic::runtime::IArchiveReader;
using epidemic::runtime::IArchiveWriter;
using epidemic::runtime::IMigration;
using epidemic::runtime::InMemoryArchiveReader;
using epidemic::runtime::InMemoryArchiveWriter;
using epidemic::runtime::MigrationKey;
using epidemic::runtime::MigrationRegistry;
using epidemic::runtime::SchemaVersion;
using epidemic::runtime::SerializedDocument;
using epidemic::runtime::ApplyMigrations;
using epidemic::runtime::DeserializeObject;
using epidemic::runtime::SerializeObject;
using epidemic::runtime::SerializerRegistry;

[[nodiscard]] StringId Id(std::string_view value)
{
    return StringId::FromString(value);
}

[[nodiscard]] bool HasRootField(const InMemoryArchiveWriter& writer, std::string_view name)
{
    const auto snapshot = writer.Snapshot();
    return snapshot != nullptr && snapshot->fields.find(std::string(name)) != snapshot->fields.end();
}

template <typename TArm, typename TOperation, typename TInvariant>
[[nodiscard]] bool VerifyWriterFault(TArm&& arm, TOperation&& operation, TInvariant&& invariant)
{
    InMemoryArchiveWriter writer;
    if (!writer.WriteUInt64("baseline", 7u))
    {
        return false;
    }

    arm(writer);
    const auto result = operation(writer);
    return !result && result.GetError().HasCode("serialization.out_of_memory") &&
           invariant(writer) && HasRootField(writer, "baseline");
}

struct ProbeData
{
    std::string name;
    std::uint64_t count = 0;
};

struct CopyFaultData
{
    std::string name;
    std::uint64_t count = 0;

    CopyFaultData() = default;
    CopyFaultData(std::string value, std::uint64_t value_count) : name(std::move(value)), count(value_count) {}

    CopyFaultData(const CopyFaultData& other)
    {
        if (fail_next_copy)
        {
            fail_next_copy = false;
            throw std::bad_alloc{};
        }
        name = other.name;
        count = other.count;
    }

    CopyFaultData& operator=(const CopyFaultData&) = default;
    CopyFaultData(CopyFaultData&&) noexcept = default;
    CopyFaultData& operator=(CopyFaultData&&) noexcept = default;

    friend void swap(CopyFaultData& left, CopyFaultData& right) noexcept
    {
        using std::swap;
        swap(left.name, right.name);
        swap(left.count, right.count);
    }

    inline static bool fail_next_copy = false;
};

class CopyFaultSerializer final : public epidemic::runtime::ISerializer
{
  public:
    [[nodiscard]] StringId GetTypeId() const override { return Id("serialization.copy_fault"); }
    [[nodiscard]] SchemaVersion GetSchemaVersion() const override { return {1u, 0u, 0u}; }
    [[nodiscard]] std::type_index GetCppType() const override { return typeid(CopyFaultData); }

    [[nodiscard]] Result<void> Serialize(const void* object, IArchiveWriter& writer) const override
    {
        const auto& value = *static_cast<const CopyFaultData*>(object);
        const auto name = writer.WriteString("name", value.name);
        if (!name)
        {
            return name;
        }
        return writer.WriteUInt64("count", value.count);
    }

    [[nodiscard]] Result<void> Deserialize(IArchiveReader&, void* object) const override
    {
        auto& value = *static_cast<CopyFaultData*>(object);
        value.name = "committed";
        value.count = 42u;
        return Result<void>::Success();
    }
};

struct ThrowingCommitData
{
    std::uint64_t value = 0;

    ThrowingCommitData() = default;
    ThrowingCommitData(const ThrowingCommitData&) = default;
    ThrowingCommitData& operator=(const ThrowingCommitData&) = default;
    ThrowingCommitData(ThrowingCommitData&&) noexcept = default;
    ThrowingCommitData& operator=(ThrowingCommitData&&)
    {
        value = 999;
        throw std::runtime_error("commit boom");
    }
};

class ThrowingCommitSerializer final : public epidemic::runtime::ISerializer
{
  public:
    [[nodiscard]] StringId GetTypeId() const override { return Id("serialization.throwing_commit"); }
    [[nodiscard]] SchemaVersion GetSchemaVersion() const override { return {1u, 0u, 0u}; }
    [[nodiscard]] std::type_index GetCppType() const override { return typeid(ThrowingCommitData); }

    [[nodiscard]] Result<void> Serialize(const void*, IArchiveWriter&) const override
    {
        return Result<void>::Failure(CreateSerializationError("serialization.unused", "unused"));
    }

    [[nodiscard]] Result<void> Deserialize(IArchiveReader&, void* object) const override
    {
        static_cast<ThrowingCommitData*>(object)->value = 42;
        return Result<void>::Success();
    }
};

class ProbeSerializer final : public epidemic::runtime::ISerializer
{
  public:
    [[nodiscard]] StringId GetTypeId() const override
    {
        return Id("serialization.probe");
    }

    [[nodiscard]] SchemaVersion GetSchemaVersion() const override
    {
        return {1u, 0u, 0u};
    }

    [[nodiscard]] std::type_index GetCppType() const override
    {
        return typeid(ProbeData);
    }

    [[nodiscard]] Result<void> Serialize(const void* object, IArchiveWriter& writer) const override
    {
        if (object == nullptr)
        {
            return Result<void>::Failure(CreateSerializationError("serialization.object_null", "object pointer must not be null"));
        }

        const auto& probe = *static_cast<const ProbeData*>(object);
        const auto name = writer.WriteString("name", probe.name);
        if (!name)
        {
            return name;
        }
        return writer.WriteUInt64("count", probe.count);
    }

    [[nodiscard]] Result<void> Deserialize(IArchiveReader& reader, void* object) const override
    {
        if (object == nullptr)
        {
            return Result<void>::Failure(CreateSerializationError("serialization.object_null", "object pointer must not be null"));
        }
        if (reader.GetTypeId() != GetTypeId())
        {
            return Result<void>::Failure(CreateSerializationError("serialization.type_mismatch", "document type does not match serializer"));
        }

        auto& probe = *static_cast<ProbeData*>(object);
        const auto name = reader.ReadString("name");
        if (!name)
        {
            return Result<void>::Failure(name.GetError());
        }
        const auto count = reader.ReadUInt64("count");
        if (!count)
        {
            return Result<void>::Failure(count.GetError());
        }

        probe.name = name.Value();
        probe.count = count.Value();
        return Result<void>::Success();
    }
};

class ProbeMigration final : public IMigration
{
  public:
    explicit ProbeMigration(MigrationKey key) : key_(key)
    {
    }

    [[nodiscard]] MigrationKey GetKey() const override
    {
        return key_;
    }

    [[nodiscard]] Result<void> Apply(IArchiveReader& input, IArchiveWriter& output) const override
    {
        (void)input;
        return output.WriteString("migration", "applied");
    }

  private:
    MigrationKey key_{};
};


class MutatingFailDeserializeSerializer final : public epidemic::runtime::ISerializer
{
  public:
    explicit MutatingFailDeserializeSerializer(bool should_throw = false) : should_throw_(should_throw) {}

    [[nodiscard]] StringId GetTypeId() const override { return Id("serialization.probe"); }
    [[nodiscard]] SchemaVersion GetSchemaVersion() const override { return {1u, 0u, 0u}; }
    [[nodiscard]] std::type_index GetCppType() const override { return typeid(ProbeData); }

    [[nodiscard]] Result<void> Serialize(const void*, IArchiveWriter&) const override
    {
        return Result<void>::Failure(CreateSerializationError("serialization.unused", "unused"));
    }

    [[nodiscard]] Result<void> Deserialize(IArchiveReader&, void* object) const override
    {
        auto& probe = *static_cast<ProbeData*>(object);
        probe.name = "mutated";
        probe.count = 999;
        if (should_throw_)
        {
            throw std::runtime_error("deserialize boom");
        }
        return Result<void>::Failure(CreateSerializationError("serialization.injected_failure", "injected deserialize failure"));
    }

  private:
    bool should_throw_ = false;
};

class MetadataOverrideWriter final : public IArchiveWriter
{
  public:
    enum class Mode
    {
        InvalidDocument,
        WrongType,
        WrongVersion,
    };

    explicit MetadataOverrideWriter(Mode mode) : mode_(mode)
    {
    }

    [[nodiscard]] Result<void> BeginObject(std::string_view name) override { return writer_.BeginObject(name); }
    [[nodiscard]] Result<void> EndObject() override { return writer_.EndObject(); }
    [[nodiscard]] Result<void> BeginArray(std::string_view name, std::size_t size) override { return writer_.BeginArray(name, size); }
    [[nodiscard]] Result<void> BeginArrayElement(std::size_t index) override { return writer_.BeginArrayElement(index); }
    [[nodiscard]] Result<void> EndArrayElement() override { return writer_.EndArrayElement(); }
    [[nodiscard]] Result<void> EndArray() override { return writer_.EndArray(); }
    [[nodiscard]] Result<void> WriteString(std::string_view name, std::string_view value) override { return writer_.WriteString(name, value); }
    [[nodiscard]] Result<void> WriteUInt64(std::string_view name, std::uint64_t value) override { return writer_.WriteUInt64(name, value); }
    [[nodiscard]] Result<void> WriteInt64(std::string_view name, std::int64_t value) override { return writer_.WriteInt64(name, value); }
    [[nodiscard]] Result<void> WriteDouble(std::string_view name, double value) override { return writer_.WriteDouble(name, value); }
    [[nodiscard]] Result<void> WriteBool(std::string_view name, bool value) override { return writer_.WriteBool(name, value); }
    [[nodiscard]] Result<void> WriteBytes(std::string_view name, std::span<const std::byte> value) override { return writer_.WriteBytes(name, value); }
    [[nodiscard]] Result<void> WriteNull(std::string_view name) override { return writer_.WriteNull(name); }

    [[nodiscard]] Result<SerializedDocument> Finalize(StringId type_id, SchemaVersion schema_version) override
    {
        if (mode_ == Mode::InvalidDocument)
        {
            return Result<SerializedDocument>::Success(SerializedDocument{});
        }
        if (mode_ == Mode::WrongType)
        {
            return writer_.Finalize(Id("serialization.wrong_type"), schema_version);
        }
        return writer_.Finalize(type_id, {schema_version.major + 1u, schema_version.minor, schema_version.patch});
    }

  private:
    InMemoryArchiveWriter writer_{};
    Mode mode_ = Mode::InvalidDocument;
};

class MetadataOverrideArchiveFactory final : public IArchiveFactory
{
  public:
    explicit MetadataOverrideArchiveFactory(MetadataOverrideWriter::Mode mode) : mode_(mode)
    {
    }

    [[nodiscard]] std::unique_ptr<IArchiveWriter> CreateWriter() const override
    {
        return std::make_unique<MetadataOverrideWriter>(mode_);
    }

    [[nodiscard]] Result<std::unique_ptr<IArchiveReader>> CreateReader(const SerializedDocument& document) const override
    {
        return in_memory_.CreateReader(document);
    }

  private:
    MetadataOverrideWriter::Mode mode_ = MetadataOverrideWriter::Mode::InvalidDocument;
    epidemic::runtime::InMemoryArchiveFactory in_memory_{};
};

class FaultInjectingArchiveFactory final : public IArchiveFactory
{
  public:
    enum class Fault
    {
        None,
        CreateWriter,
        FieldPublication,
        FinalizeCandidate,
    };

    explicit FaultInjectingArchiveFactory(Fault fault, std::size_t writer_index = 1u)
        : fault_(fault), writer_index_(writer_index)
    {
    }

    [[nodiscard]] std::unique_ptr<IArchiveWriter> CreateWriter() const override
    {
        const std::size_t current_index = ++writer_count_;
        if (fault_ == Fault::CreateWriter && current_index == writer_index_)
        {
            throw std::bad_alloc{};
        }

        auto writer = std::make_unique<InMemoryArchiveWriter>();
        if (current_index == writer_index_)
        {
            if (fault_ == Fault::FieldPublication)
            {
                epidemic::runtime::SerializationRuntimeTestAccess::FailNextFieldPublication(*writer);
            }
            else if (fault_ == Fault::FinalizeCandidate)
            {
                epidemic::runtime::SerializationRuntimeTestAccess::FailNextFinalizeCandidate(*writer);
            }
        }
        return writer;
    }

    [[nodiscard]] Result<std::unique_ptr<IArchiveReader>> CreateReader(const SerializedDocument& document) const override
    {
        return in_memory_.CreateReader(document);
    }

  private:
    Fault fault_ = Fault::None;
    std::size_t writer_index_ = 1u;
    mutable std::size_t writer_count_ = 0u;
    epidemic::runtime::InMemoryArchiveFactory in_memory_{};
};

class BlindProbeSerializer final : public epidemic::runtime::ISerializer
{
  public:
    explicit BlindProbeSerializer(SchemaVersion schema = {1u, 0u, 0u}) : schema_(schema) {}

    [[nodiscard]] StringId GetTypeId() const override { return Id("serialization.blind_probe"); }
    [[nodiscard]] SchemaVersion GetSchemaVersion() const override { return schema_; }
    [[nodiscard]] std::type_index GetCppType() const override { return typeid(ProbeData); }
    [[nodiscard]] std::size_t DeserializeCalls() const noexcept { return deserialize_calls_; }

    [[nodiscard]] Result<void> Serialize(const void*, IArchiveWriter&) const override
    {
        return Result<void>::Success();
    }

    [[nodiscard]] Result<void> Deserialize(IArchiveReader&, void* object) const override
    {
        ++deserialize_calls_;
        static_cast<ProbeData*>(object)->count = 123u;
        return Result<void>::Success();
    }

  private:
    SchemaVersion schema_{};
    mutable std::size_t deserialize_calls_ = 0;
};

class InvalidMetadataSerializer final : public epidemic::runtime::ISerializer
{
  public:
    InvalidMetadataSerializer(StringId type_id, SchemaVersion schema) : type_id_(type_id), schema_(schema) {}
    [[nodiscard]] StringId GetTypeId() const override { return type_id_; }
    [[nodiscard]] SchemaVersion GetSchemaVersion() const override { return schema_; }
    [[nodiscard]] std::type_index GetCppType() const override { return typeid(ProbeData); }

    [[nodiscard]] Result<void> Serialize(const void*, IArchiveWriter&) const override { return Result<void>::Success(); }
    [[nodiscard]] Result<void> Deserialize(IArchiveReader&, void*) const override { return Result<void>::Success(); }

  private:
    StringId type_id_{};
    SchemaVersion schema_{};
};

class ThrowingMetadataSerializer final : public epidemic::runtime::ISerializer
{
  public:
    [[nodiscard]] StringId GetTypeId() const override { throw std::runtime_error("metadata boom"); }
    [[nodiscard]] SchemaVersion GetSchemaVersion() const override { return {1u, 0u, 0u}; }
    [[nodiscard]] std::type_index GetCppType() const override { return typeid(ProbeData); }
    [[nodiscard]] Result<void> Serialize(const void*, IArchiveWriter&) const override { return Result<void>::Success(); }
    [[nodiscard]] Result<void> Deserialize(IArchiveReader&, void*) const override { return Result<void>::Success(); }
};

class ThrowingBodySerializer final : public epidemic::runtime::ISerializer
{
  public:
    [[nodiscard]] StringId GetTypeId() const override { return Id("serialization.throwing_body"); }
    [[nodiscard]] SchemaVersion GetSchemaVersion() const override { return {1u, 0u, 0u}; }
    [[nodiscard]] std::type_index GetCppType() const override { return typeid(ProbeData); }

    [[nodiscard]] Result<void> Serialize(const void*, IArchiveWriter&) const override
    {
        throw std::runtime_error("serialize boom");
    }
    [[nodiscard]] Result<void> Deserialize(IArchiveReader&, void* object) const override
    {
        static_cast<ProbeData*>(object)->count = 999u;
        throw std::runtime_error("deserialize boom");
    }
};

class FailingMigration final : public IMigration
{
  public:
    FailingMigration(MigrationKey key, bool should_throw = false) : key_(key), should_throw_(should_throw) {}
    [[nodiscard]] MigrationKey GetKey() const override { return key_; }
    [[nodiscard]] Result<void> Apply(IArchiveReader&, IArchiveWriter& output) const override
    {
        if (!output.WriteString("partial", "candidate"))
        {
            return Result<void>::Failure(CreateSerializationError("serialization.test_write_failed", "fixture write failed"));
        }
        if (should_throw_)
        {
            throw std::runtime_error("migration boom");
        }
        return Result<void>::Failure(CreateSerializationError("serialization.injected_failure", "migration rejected candidate"));
    }

  private:
    MigrationKey key_{};
    bool should_throw_ = false;
};

[[nodiscard]] bool TestArchiveStructuralAndReaderFailuresAreControlled()
{
    InMemoryArchiveWriter writer;
    const auto empty_value = writer.WriteString("", "x");
    const auto empty_object = writer.BeginObject("");
    const auto empty_array = writer.BeginArray("", 0);
    const auto root_end_array = writer.EndArray();
    if (empty_value || empty_object || empty_array || root_end_array)
    {
        return false;
    }
    if (!empty_value.GetError().HasCode("serialization.invalid_name") ||
        !empty_object.GetError().HasCode("serialization.invalid_name") ||
        !empty_array.GetError().HasCode("serialization.invalid_name") ||
        !root_end_array.GetError().HasCode("serialization.malformed"))
    {
        return false;
    }

    if (!writer.BeginObject("object"))
    {
        return false;
    }
    const auto wrong_object_end = writer.EndArray();
    if (wrong_object_end || !wrong_object_end.GetError().HasCode("serialization.malformed") || !writer.EndObject())
    {
        return false;
    }
    if (!writer.BeginArray("array", 1))
    {
        return false;
    }
    const auto wrong_array_end = writer.EndObject();
    const auto bad_index = writer.BeginArrayElement(1);
    if (wrong_array_end || bad_index || !writer.BeginArrayElement(0) || !writer.WriteUInt64("value", 9u) ||
        !writer.EndArrayElement() || !writer.EndArray())
    {
        return false;
    }

    const auto doc = writer.Finalize(Id("serialization.structure"), {1u, 0u, 0u});
    if (!doc)
    {
        return false;
    }

    InMemoryArchiveReader reader(doc.Value());
    const auto missing = reader.ReadString("missing");
    const auto wrong_kind = reader.ReadString("array");
    const auto array_size = reader.BeginArray("array");
    const auto reader_bad_index = reader.BeginArrayElement(3);
    const auto reader_wrong_end = reader.EndObject();
    return !missing && missing.GetError().HasCode("serialization.field_missing") && !wrong_kind &&
           wrong_kind.GetError().HasCode("serialization.type_mismatch") && array_size && array_size.Value() == 1u &&
           !reader_bad_index && reader_bad_index.GetError().HasCode("serialization.invalid_array_index") &&
           !reader_wrong_end && reader_wrong_end.GetError().HasCode("serialization.malformed") &&
           reader.BeginArrayElement(0) && reader.ReadUInt64("value").Value() == 9u && reader.EndArrayElement() && reader.EndArray();
}

[[nodiscard]] bool TestDeepEmptyStructuresBytesAndNullRoundTrip()
{
    InMemoryArchiveWriter writer;
    const std::vector<std::byte> bytes{std::byte{0x00}, std::byte{0x42}, std::byte{0xff}};
    if (!writer.BeginObject("outer") || !writer.BeginObject("empty_object") || !writer.EndObject() ||
        !writer.BeginArray("empty_array", 0) || !writer.EndArray() || !writer.BeginObject("deep") ||
        !writer.WriteBytes("bytes", bytes) || !writer.WriteNull("nil") || !writer.EndObject() || !writer.EndObject())
    {
        return false;
    }
    const auto doc = writer.Finalize(Id("serialization.deep"), {1u, 0u, 0u});
    if (!doc)
    {
        return false;
    }

    InMemoryArchiveReader reader(doc.Value());
    if (!reader.BeginObject("outer") || !reader.BeginObject("empty_object") || !reader.EndObject())
    {
        return false;
    }
    const auto empty_count = reader.BeginArray("empty_array");
    if (!empty_count || empty_count.Value() != 0u || !reader.EndArray() || !reader.BeginObject("deep"))
    {
        return false;
    }
    const auto read_bytes = reader.ReadBytes("bytes");
    const auto is_null = reader.IsNull("nil");
    return read_bytes && read_bytes.Value() == bytes && is_null && is_null.Value() && reader.EndObject() && reader.EndObject();
}

[[nodiscard]] bool TestFinalizeValidatesMetadataAndPreservesWriterOnFailure()
{
    InMemoryArchiveWriter writer;
    if (!writer.WriteString("name", "candidate"))
    {
        return false;
    }
    const auto invalid_type = writer.Finalize({}, {1u, 0u, 0u});
    const auto invalid_schema = writer.Finalize(Id("serialization.valid"), {});
    if (invalid_type || invalid_schema || !invalid_type.GetError().HasCode("serialization.invalid_type") ||
        !invalid_schema.GetError().HasCode("serialization.invalid_schema_version"))
    {
        return false;
    }
    const auto valid = writer.Finalize(Id("serialization.valid"), {1u, 0u, 0u});
    return valid && valid.Value().IsValid();
}

[[nodiscard]] bool TestArchiveAllocationFailureAtomicity()
{
    const auto absent = [](const InMemoryArchiveWriter& writer, std::string_view name)
    {
        return !HasRootField(writer, name);
    };

    if (!VerifyWriterFault(
            [](InMemoryArchiveWriter& writer)
            {
                epidemic::runtime::SerializationRuntimeTestAccess::FailNextObjectPublication(writer);
            },
            [](InMemoryArchiveWriter& writer) { return writer.BeginObject("candidate_object"); },
            [&](const InMemoryArchiveWriter& writer) { return absent(writer, "candidate_object"); }))
    {
        return false;
    }

    if (!VerifyWriterFault(
            [](InMemoryArchiveWriter& writer)
            {
                epidemic::runtime::SerializationRuntimeTestAccess::FailNextArrayPublication(writer);
            },
            [](InMemoryArchiveWriter& writer) { return writer.BeginArray("candidate_array", 4); },
            [&](const InMemoryArchiveWriter& writer) { return absent(writer, "candidate_array"); }))
    {
        return false;
    }

    InMemoryArchiveWriter element_writer;
    if (!element_writer.WriteUInt64("baseline", 7u) || !element_writer.BeginArray("items", 1))
    {
        return false;
    }
    epidemic::runtime::SerializationRuntimeTestAccess::FailNextArrayElementPublication(element_writer);
    const auto element_failed = element_writer.BeginArrayElement(0);
    if (element_failed || !element_failed.GetError().HasCode("serialization.out_of_memory") ||
        !HasRootField(element_writer, "baseline"))
    {
        return false;
    }
    const auto element_snapshot = element_writer.Snapshot();
    const auto element_found = element_snapshot->fields.find("items");
    if (element_found == element_snapshot->fields.end())
    {
        return false;
    }
    const auto array = std::get_if<epidemic::runtime::ArchiveArrayPtr>(&element_found->second.storage);
    if (array == nullptr || !*array || (*array)->initialized.size() != 1u || (*array)->initialized[0] ||
        !element_writer.BeginArrayElement(0) || !element_writer.EndArrayElement() || !element_writer.EndArray())
    {
        return false;
    }

    const std::vector<std::byte> bytes(64u, std::byte{0x5a});
    const auto verify_field = [&](auto&& operation, std::string_view field)
    {
        return VerifyWriterFault(
            [](InMemoryArchiveWriter& writer)
            {
                epidemic::runtime::SerializationRuntimeTestAccess::FailNextFieldPublication(writer);
            },
            std::forward<decltype(operation)>(operation),
            [&](const InMemoryArchiveWriter& writer) { return absent(writer, field); });
    };

    if (!verify_field([](InMemoryArchiveWriter& writer) { return writer.WriteString("string", std::string(96u, 'x')); }, "string") ||
        !verify_field([](InMemoryArchiveWriter& writer) { return writer.WriteUInt64("u64", 1u); }, "u64") ||
        !verify_field([](InMemoryArchiveWriter& writer) { return writer.WriteInt64("i64", -1); }, "i64") ||
        !verify_field([](InMemoryArchiveWriter& writer) { return writer.WriteDouble("double", 1.5); }, "double") ||
        !verify_field([](InMemoryArchiveWriter& writer) { return writer.WriteBool("bool", true); }, "bool") ||
        !verify_field([&](InMemoryArchiveWriter& writer) { return writer.WriteBytes("bytes", bytes); }, "bytes") ||
        !verify_field([](InMemoryArchiveWriter& writer) { return writer.WriteNull("null"); }, "null"))
    {
        return false;
    }

    InMemoryArchiveWriter finalize_writer;
    if (!finalize_writer.WriteString("name", "stable"))
    {
        return false;
    }
    epidemic::runtime::SerializationRuntimeTestAccess::FailNextFinalizeCandidate(finalize_writer);
    const auto failed_finalize = finalize_writer.Finalize(Id("serialization.finalize_fault"), {1u, 0u, 0u});
    if (failed_finalize || !failed_finalize.GetError().HasCode("serialization.out_of_memory") ||
        !finalize_writer.WriteString("after_failure", "still_writable"))
    {
        return false;
    }
    const auto recovered = finalize_writer.Finalize(Id("serialization.finalize_fault"), {1u, 0u, 0u});
    return recovered && recovered.Value().IsValid();
}

[[nodiscard]] bool TestSerializerRegistryAndTypedBoundaryValidation()
{
    SerializerRegistry allocation_registry;
    auto allocation_baseline = std::make_shared<BlindProbeSerializer>();
    auto allocation_candidate = std::make_shared<ProbeSerializer>();
    if (!allocation_registry.RegisterSerializer(allocation_baseline))
    {
        return false;
    }
    epidemic::runtime::SerializationRuntimeTestAccess::FailNextSerializerRegistrationAllocation(allocation_registry);
    const auto allocation_failed = allocation_registry.RegisterSerializer(allocation_candidate);
    if (allocation_failed || !allocation_failed.GetError().HasCode("serialization.out_of_memory") ||
        !allocation_registry.HasSerializer(allocation_baseline->GetTypeId()) ||
        allocation_registry.HasSerializer(allocation_candidate->GetTypeId()) ||
        !allocation_registry.RegisterSerializer(allocation_candidate))
    {
        return false;
    }

    SerializerRegistry registry;
    const auto null_result = registry.RegisterSerializer(nullptr);
    const auto invalid_type = registry.RegisterSerializer(std::make_shared<InvalidMetadataSerializer>(StringId{}, SchemaVersion{1u, 0u, 0u}));
    const auto invalid_schema = registry.RegisterSerializer(std::make_shared<InvalidMetadataSerializer>(Id("serialization.invalid_schema"), SchemaVersion{}));
    const auto throwing_metadata = registry.RegisterSerializer(std::make_shared<ThrowingMetadataSerializer>());
    if (null_result || invalid_type || invalid_schema || throwing_metadata ||
        !null_result.GetError().HasCode("serialization.serializer.null") ||
        !invalid_type.GetError().HasCode("serialization.serializer.invalid_type") ||
        !invalid_schema.GetError().HasCode("serialization.serializer.invalid_schema_version") ||
        !throwing_metadata.GetError().HasCode("serialization.serializer.exception"))
    {
        return false;
    }

    auto serializer = std::make_shared<BlindProbeSerializer>();
    if (!registry.RegisterSerializer(serializer) || !registry.Freeze() || !registry.Freeze())
    {
        return false;
    }
    const auto after_freeze = registry.RegisterSerializer(std::make_shared<ProbeSerializer>());
    if (after_freeze || !after_freeze.GetError().HasCode("serialization.registry_frozen"))
    {
        return false;
    }

    InMemoryArchiveWriter wrong_type_writer;
    const auto wrong_type_doc = wrong_type_writer.Finalize(Id("serialization.other"), serializer->GetSchemaVersion());
    InMemoryArchiveWriter wrong_schema_writer;
    const auto wrong_schema_doc = wrong_schema_writer.Finalize(serializer->GetTypeId(), {2u, 0u, 0u});
    if (!wrong_type_doc || !wrong_schema_doc)
    {
        return false;
    }

    ProbeData destination{"stable", 7u};
    InMemoryArchiveReader wrong_type_reader(wrong_type_doc.Value());
    const auto wrong_type = DeserializeObject(*serializer, wrong_type_reader, destination);
    InMemoryArchiveReader wrong_schema_reader(wrong_schema_doc.Value());
    const auto wrong_schema = DeserializeObject(*serializer, wrong_schema_reader, destination);
    return !wrong_type && wrong_type.GetError().HasCode("serialization.type_mismatch") &&
           !wrong_schema && wrong_schema.GetError().HasCode("serialization.schema_mismatch") &&
           serializer->DeserializeCalls() == 0u && destination.name == "stable" && destination.count == 7u;
}

[[nodiscard]] bool TestSerializerBodyExceptionsAreContained()
{
    ThrowingBodySerializer serializer;
    ProbeData source{"stable", 7u};
    InMemoryArchiveWriter writer;
    const auto serialize = SerializeObject(serializer, source, writer);
    if (serialize || !serialize.GetError().HasCode("serialization.serializer.exception"))
    {
        return false;
    }

    InMemoryArchiveWriter document_writer;
    const auto document = document_writer.Finalize(serializer.GetTypeId(), serializer.GetSchemaVersion());
    if (!document)
    {
        return false;
    }
    InMemoryArchiveReader reader(document.Value());
    ProbeData destination{"stable", 7u};
    const auto deserialize = DeserializeObject(serializer, reader, destination);
    return !deserialize && deserialize.GetError().HasCode("serialization.serializer.exception") &&
           destination.name == "stable" && destination.count == 7u;
}

[[nodiscard]] bool TestDeserializeAllocationFailureAtomicity()
{
    CopyFaultSerializer serializer;
    InMemoryArchiveWriter writer;
    const auto document = writer.Finalize(serializer.GetTypeId(), serializer.GetSchemaVersion());
    if (!document)
    {
        return false;
    }

    InMemoryArchiveReader failing_reader(document.Value());
    CopyFaultData destination{"original", 7u};
    CopyFaultData::fail_next_copy = true;
    const auto failed = DeserializeObject(serializer, failing_reader, destination);
    if (failed || !failed.GetError().HasCode("serialization.out_of_memory") ||
        destination.name != "original" || destination.count != 7u)
    {
        return false;
    }

    InMemoryArchiveReader recovery_reader(document.Value());
    const auto recovered = DeserializeObject(serializer, recovery_reader, destination);
    return recovered && destination.name == "committed" && destination.count == 42u;
}

[[nodiscard]] bool TestApplyMigrationsAllocationFailureAtomicity()
{
    const auto type = Id("serialization.migration_fault");
    MigrationRegistry registry;
    if (!registry.RegisterMigration(std::make_shared<ProbeMigration>(MigrationKey{type, {1u, 0u, 0u}, {2u, 0u, 0u}})) ||
        !registry.RegisterMigration(std::make_shared<ProbeMigration>(MigrationKey{type, {2u, 0u, 0u}, {3u, 0u, 0u}})))
    {
        return false;
    }

    epidemic::runtime::InMemoryArchiveFactory archives;
    InMemoryArchiveWriter writer;
    if (!writer.WriteString("name", "source"))
    {
        return false;
    }
    const auto source = writer.Finalize(type, {1u, 0u, 0u});
    if (!source)
    {
        return false;
    }

    const auto source_is_unchanged = [&]()
    {
        const auto source_reader = archives.CreateReader(source.Value());
        return source_reader && source.Value().GetSchemaVersion() == SchemaVersion{1u, 0u, 0u} &&
               source_reader.Value()->ReadString("name").Value() == "source";
    };

    for (std::size_t writer_index = 1u; writer_index <= 2u; ++writer_index)
    {
        FaultInjectingArchiveFactory faulting_archives{FaultInjectingArchiveFactory::Fault::FinalizeCandidate, writer_index};
        const auto failed = ApplyMigrations(source.Value(), {3u, 0u, 0u}, registry, faulting_archives);
        if (failed || !failed.GetError().HasCode("serialization.out_of_memory") || !source_is_unchanged())
        {
            return false;
        }
        const auto recovery_path = registry.FindMigrationPath(type, {1u, 0u, 0u}, {3u, 0u, 0u});
        if (!recovery_path || recovery_path.Value().size() != 2u)
        {
            return false;
        }
    }

    FaultInjectingArchiveFactory field_fault_archives{FaultInjectingArchiveFactory::Fault::FieldPublication, 1u};
    const auto write_failed = ApplyMigrations(source.Value(), {3u, 0u, 0u}, registry, field_fault_archives);
    if (write_failed || !write_failed.GetError().HasCode("serialization.out_of_memory") || !source_is_unchanged())
    {
        return false;
    }

    const auto recovered = ApplyMigrations(source.Value(), {3u, 0u, 0u}, registry, archives);
    return recovered && recovered.Value().GetSchemaVersion() == SchemaVersion{3u, 0u, 0u} && source_is_unchanged();
}

[[nodiscard]] bool TestMigrationFailuresPreserveSourceAndContainExceptions()
{
    const auto type = Id("serialization.failed_migration");
    epidemic::runtime::InMemoryArchiveFactory archives;
    InMemoryArchiveWriter source_writer;
    if (!source_writer.WriteString("name", "source"))
    {
        return false;
    }
    const auto source = source_writer.Finalize(type, {1u, 0u, 0u});
    if (!source)
    {
        return false;
    }

    MigrationRegistry failed_registry;
    if (!failed_registry.RegisterMigration(std::make_shared<FailingMigration>(MigrationKey{type, {1u, 0u, 0u}, {2u, 0u, 0u}})))
    {
        return false;
    }
    const auto failed = ApplyMigrations(source.Value(), {2u, 0u, 0u}, failed_registry, archives);

    MigrationRegistry throwing_registry;
    if (!throwing_registry.RegisterMigration(std::make_shared<FailingMigration>(MigrationKey{type, {1u, 0u, 0u}, {2u, 0u, 0u}}, true)))
    {
        return false;
    }
    const auto thrown = ApplyMigrations(source.Value(), {2u, 0u, 0u}, throwing_registry, archives);
    const auto source_reader = archives.CreateReader(source.Value());
    return !failed && failed.GetError().HasCode("serialization.injected_failure") && !thrown &&
           thrown.GetError().HasCode("serialization.migration.exception") && source_reader &&
           source_reader.Value()->ReadString("name").Value() == "source" &&
           source.Value().GetSchemaVersion() == SchemaVersion{1u, 0u, 0u};
}

[[nodiscard]] bool TestMigrationVersionValidationAndPathAllocationAtomicity()
{
    const auto type = Id("serialization.path_fault");
    const MigrationKey one_to_two{type, {1u, 0u, 0u}, {2u, 0u, 0u}};
    const MigrationKey two_to_three{type, {2u, 0u, 0u}, {3u, 0u, 0u}};
    MigrationRegistry registry;
    const auto zero_from = registry.RegisterMigration(std::make_shared<ProbeMigration>(MigrationKey{type, {}, {1u, 0u, 0u}}));
    const auto zero_to = registry.RegisterMigration(std::make_shared<ProbeMigration>(MigrationKey{type, {1u, 0u, 0u}, {}}));
    if (zero_from || zero_to || !zero_from.GetError().HasCode("serialization.migration.invalid_version") ||
        !zero_to.GetError().HasCode("serialization.migration.invalid_version") ||
        !registry.RegisterMigration(std::make_shared<ProbeMigration>(one_to_two)))
    {
        return false;
    }

    auto second_migration = std::make_shared<ProbeMigration>(two_to_three);
    epidemic::runtime::SerializationRuntimeTestAccess::FailNextMigrationRegistrationAllocation(registry);
    const auto registration_failed = registry.RegisterMigration(second_migration);
    if (registration_failed || !registration_failed.GetError().HasCode("serialization.out_of_memory") ||
        !registry.HasMigration(one_to_two) || registry.HasMigration(two_to_three) ||
        !registry.RegisterMigration(second_migration))
    {
        return false;
    }

    epidemic::runtime::SerializationRuntimeTestAccess::FailNextMigrationPathAllocation(registry);
    const auto failed_path = registry.FindMigrationPath(type, {1u, 0u, 0u}, {3u, 0u, 0u});
    if (failed_path || !failed_path.GetError().HasCode("serialization.out_of_memory") ||
        !registry.HasMigration(one_to_two) || !registry.HasMigration(two_to_three))
    {
        return false;
    }

    const auto recovery = registry.FindMigrationPath(type, {1u, 0u, 0u}, {3u, 0u, 0u});
    return recovery && recovery.Value().size() == 2u;
}

[[nodiscard]] bool TestSerializeToDocumentAllocationFailureAtomicity()
{
    ProbeSerializer serializer;
    const ProbeData source{"source", 17u};

    FaultInjectingArchiveFactory create_fault{FaultInjectingArchiveFactory::Fault::CreateWriter};
    const auto create_failed = epidemic::runtime::SerializeToDocument(serializer, source, create_fault);
    if (create_failed || !create_failed.GetError().HasCode("serialization.out_of_memory"))
    {
        return false;
    }

    FaultInjectingArchiveFactory field_fault{FaultInjectingArchiveFactory::Fault::FieldPublication};
    const auto field_failed = epidemic::runtime::SerializeToDocument(serializer, source, field_fault);
    if (field_failed || !field_failed.GetError().HasCode("serialization.out_of_memory"))
    {
        return false;
    }

    FaultInjectingArchiveFactory finalize_fault{FaultInjectingArchiveFactory::Fault::FinalizeCandidate};
    const auto finalize_failed = epidemic::runtime::SerializeToDocument(serializer, source, finalize_fault);
    if (finalize_failed || !finalize_failed.GetError().HasCode("serialization.out_of_memory"))
    {
        return false;
    }

    FaultInjectingArchiveFactory healthy{FaultInjectingArchiveFactory::Fault::None};
    const auto recovered = epidemic::runtime::SerializeToDocument(serializer, source, healthy);
    return recovered && recovered.Value().IsValid();
}

[[nodiscard]] bool TestPrimitiveDocumentRoundTrip()
{
    InMemoryArchiveWriter writer;
    const std::vector<std::byte> bytes{std::byte{0x01}, std::byte{0x7f}, std::byte{0xff}};
    if (!writer.WriteString("name", "potato") || !writer.WriteUInt64("count", 7u) || !writer.WriteInt64("delta", -4) ||
        !writer.WriteDouble("weight", 1.5) || !writer.WriteBool("fresh", true) || !writer.WriteBytes("blob", bytes) ||
        !writer.WriteNull("empty"))
    {
        return false;
    }

    const auto document = writer.Finalize(Id("serialization.primitive"), {1u, 2u, 3u});
    if (!document)
    {
        return false;
    }

    InMemoryArchiveReader reader(document.Value());
    const auto read_bytes = reader.ReadBytes("blob");
    return document.Value().IsValid() && document.Value().GetTypeId() == Id("serialization.primitive") &&
           document.Value().GetSchemaVersion() == SchemaVersion{1u, 2u, 3u} &&
           document.Value().GetFormatVersion() == 1u && reader.GetTypeId() == Id("serialization.primitive") &&
           reader.GetSchemaVersion() == SchemaVersion{1u, 2u, 3u} &&
           reader.GetFormatVersion() == 1u && reader.ReadString("name").Value() == "potato" &&
           reader.ReadUInt64("count").Value() == 7u && reader.ReadInt64("delta").Value() == -4 &&
           reader.ReadDouble("weight").Value() == 1.5 && reader.ReadBool("fresh").Value() && read_bytes &&
           read_bytes.Value() == bytes && reader.IsNull("empty").Value();
}

[[nodiscard]] bool TestDuplicateFieldsAreRejected()
{
    InMemoryArchiveWriter writer;
    const auto first = writer.WriteString("name", "first");
    const auto duplicate = writer.WriteString("name", "second");
    return first && !duplicate && duplicate.GetError().HasCode("serialization.duplicate_field");
}

[[nodiscard]] bool TestNestedObjectAndArrayRoundTrip()
{
    InMemoryArchiveWriter writer;
    if (!writer.BeginObject("item") || !writer.WriteString("id", "items/potato") || !writer.EndObject())
    {
        return false;
    }
    if (!writer.BeginArray("children", 2) || !writer.BeginArrayElement(0) || !writer.WriteString("name", "leaf") ||
        !writer.EndArrayElement() || !writer.BeginArrayElement(1) || !writer.WriteString("name", "root") ||
        !writer.EndArrayElement() || !writer.EndArray())
    {
        return false;
    }

    const auto document = writer.Finalize(Id("serialization.nested"), {1u, 0u, 0u});
    if (!document)
    {
        return false;
    }

    InMemoryArchiveReader reader(document.Value());
    if (!reader.BeginObject("item") || reader.ReadString("id").Value() != "items/potato" || !reader.EndObject())
    {
        return false;
    }

    const auto children_count = reader.BeginArray("children");
    return children_count && children_count.Value() == 2u && reader.BeginArrayElement(1) &&
           reader.ReadString("name").Value() == "root" && reader.EndArrayElement() && reader.EndArray();
}

[[nodiscard]] bool TestMalformedArchiveOperationsReturnResults()
{
    InMemoryArchiveWriter writer;
    const auto unmatched_end = writer.EndObject();
    const auto array = writer.BeginArray("items", 1);
    const auto bad_index = writer.BeginArrayElement(3);
    const auto unclosed_finalize = writer.Finalize(Id("serialization.bad"), {1u, 0u, 0u});
    return !unmatched_end && unmatched_end.GetError().HasCode("serialization.malformed") && array && !bad_index &&
           bad_index.GetError().HasCode("serialization.invalid_array_index") && !unclosed_finalize &&
           unclosed_finalize.GetError().HasCode("serialization.malformed");
}

[[nodiscard]] bool TestFinalizeMakesWriterImmutable()
{
    InMemoryArchiveWriter writer;
    if (!writer.WriteString("name", "sealed"))
    {
        return false;
    }

    const auto document = writer.Finalize(Id("serialization.sealed"), {1u, 0u, 0u});
    const auto late_write = writer.WriteString("name", "mutated");
    const auto second_finalize = writer.Finalize(Id("serialization.sealed"), {1u, 0u, 0u});
    return document && !late_write && late_write.GetError().HasCode("serialization.finalized") && !second_finalize &&
           second_finalize.GetError().HasCode("serialization.finalized");
}

[[nodiscard]] bool TestSerializerExecutesRoundTrip()
{
    ProbeSerializer serializer;
    ProbeData source{"runtime", 42u};
    InMemoryArchiveWriter writer;
    if (!SerializeObject(serializer, source, writer))
    {
        return false;
    }

    const auto document = writer.Finalize(serializer.GetTypeId(), serializer.GetSchemaVersion());
    if (!document)
    {
        return false;
    }

    ProbeData target{};
    InMemoryArchiveReader reader(document.Value());
    const auto deserialize = DeserializeObject(serializer, reader, target);
    return deserialize && target.name == source.name && target.count == source.count;
}

[[nodiscard]] bool TestTypedSerializerRejectsWrongCppType()
{
    struct OtherData
    {
        int value = 0;
    };

    ProbeSerializer serializer;
    OtherData other{};
    InMemoryArchiveWriter writer;
    const auto serialize = SerializeObject(serializer, other, writer);
    return !serialize && serialize.GetError().HasCode("serialization.cpp_type_mismatch");
}

[[nodiscard]] bool TestSerializerRejectsWrongTypeAndMissingFields()
{
    ProbeSerializer serializer;
    InMemoryArchiveWriter wrong_type_writer;
    if (!wrong_type_writer.WriteString("name", "runtime") || !wrong_type_writer.WriteUInt64("count", 42u))
    {
        return false;
    }
    const auto wrong_type_doc = wrong_type_writer.Finalize(Id("serialization.other"), serializer.GetSchemaVersion());
    if (!wrong_type_doc)
    {
        return false;
    }

    ProbeData target{};
    InMemoryArchiveReader wrong_type_reader(wrong_type_doc.Value());
    const auto wrong_type = DeserializeObject(serializer, wrong_type_reader, target);

    InMemoryArchiveWriter missing_field_writer;
    if (!missing_field_writer.WriteString("name", "runtime"))
    {
        return false;
    }
    const auto missing_field_doc = missing_field_writer.Finalize(serializer.GetTypeId(), serializer.GetSchemaVersion());
    if (!missing_field_doc)
    {
        return false;
    }

    InMemoryArchiveReader missing_field_reader(missing_field_doc.Value());
    const auto missing_field = DeserializeObject(serializer, missing_field_reader, target);
    return !wrong_type && wrong_type.GetError().HasCode("serialization.type_mismatch") && !missing_field &&
           missing_field.GetError().HasCode("serialization.field_missing");
}

[[nodiscard]] bool TestSerializerRegistryOwnsSharedSerializers()
{
    SerializerRegistry registry;
    auto serializer = std::make_shared<ProbeSerializer>();
    const auto type_id = serializer->GetTypeId();
    const auto registered = registry.RegisterSerializer(serializer);
    serializer.reset();

    const auto found = registry.FindSerializer(type_id);
    const auto duplicate = registry.RegisterSerializer(std::make_shared<ProbeSerializer>());
    return registered && found && found->GetTypeId() == type_id && registry.HasSerializer(type_id) && !duplicate &&
           duplicate.GetError().HasCode("serialization.serializer.duplicate_type");
}

[[nodiscard]] bool TestMigrationRegistryFindsExactAndChainedPaths()
{
    MigrationRegistry registry;
    const auto type = Id("serialization.migrated");
    const auto one_to_two = std::make_shared<ProbeMigration>(MigrationKey{type, {1u, 0u, 0u}, {2u, 0u, 0u}});
    const auto two_to_three = std::make_shared<ProbeMigration>(MigrationKey{type, {2u, 0u, 0u}, {3u, 0u, 0u}});
    if (!registry.RegisterMigration(one_to_two) || !registry.RegisterMigration(two_to_three))
    {
        return false;
    }

    const auto exact = registry.FindMigration(one_to_two->GetKey());
    const auto path = registry.FindMigrationPath(type, {1u, 0u, 0u}, {3u, 0u, 0u});
    const auto same = registry.FindMigrationPath(type, {3u, 0u, 0u}, {3u, 0u, 0u});
    return exact == one_to_two && path && path.Value().size() == 2u && same && same.Value().empty();
}

[[nodiscard]] bool TestMigrationRegistryRejectsMissingCycleAndAmbiguousPaths()
{
    const auto type = Id("serialization.migrated");

    MigrationRegistry missing_registry;
    const auto missing = missing_registry.FindMigrationPath(type, {1u, 0u, 0u}, {2u, 0u, 0u});

    MigrationRegistry cycle_registry;
    if (!cycle_registry.RegisterMigration(std::make_shared<ProbeMigration>(MigrationKey{type, {1u, 0u, 0u}, {2u, 0u, 0u}})) ||
        !cycle_registry.RegisterMigration(std::make_shared<ProbeMigration>(MigrationKey{type, {2u, 0u, 0u}, {1u, 0u, 0u}})))
    {
        return false;
    }
    const auto cycle = cycle_registry.FindMigrationPath(type, {1u, 0u, 0u}, {3u, 0u, 0u});

    MigrationRegistry ambiguous_registry;
    if (!ambiguous_registry.RegisterMigration(std::make_shared<ProbeMigration>(MigrationKey{type, {1u, 0u, 0u}, {2u, 0u, 0u}})) ||
        !ambiguous_registry.RegisterMigration(std::make_shared<ProbeMigration>(MigrationKey{type, {2u, 0u, 0u}, {3u, 0u, 0u}})) ||
        !ambiguous_registry.RegisterMigration(std::make_shared<ProbeMigration>(MigrationKey{type, {1u, 0u, 0u}, {4u, 0u, 0u}})) ||
        !ambiguous_registry.RegisterMigration(std::make_shared<ProbeMigration>(MigrationKey{type, {4u, 0u, 0u}, {3u, 0u, 0u}})))
    {
        return false;
    }
    const auto ambiguous = ambiguous_registry.FindMigrationPath(type, {1u, 0u, 0u}, {3u, 0u, 0u});

    return !missing && missing.GetError().HasCode("serialization.migration.path_missing") && !cycle &&
           cycle.GetError().HasCode("serialization.migration.cycle") && !ambiguous &&
           ambiguous.GetError().HasCode("serialization.migration.ambiguous_path");
}

[[nodiscard]] bool TestApplyMigrationsBuildsNewDocument()
{
    const auto services = CreateSerializationServices();
    if (!services || !services.Value().migrations || !services.Value().archives)
    {
        return false;
    }

    const auto type = Id("serialization.migrated");
    if (!services.Value().migrations->RegisterMigration(
            std::make_shared<ProbeMigration>(MigrationKey{type, {1u, 0u, 0u}, {2u, 0u, 0u}})) ||
        !services.Value().migrations->RegisterMigration(
            std::make_shared<ProbeMigration>(MigrationKey{type, {2u, 0u, 0u}, {3u, 0u, 0u}})))
    {
        return false;
    }

    auto writer = services.Value().archives->CreateWriter();
    if (!writer || !writer->WriteString("name", "original"))
    {
        return false;
    }
    const auto original = writer->Finalize(type, {1u, 0u, 0u});
    if (!original)
    {
        return false;
    }

    const auto migrated = ApplyMigrations(original.Value(), {3u, 0u, 0u}, *services.Value().migrations, *services.Value().archives);
    if (!migrated || original.Value().GetSchemaVersion() != SchemaVersion{1u, 0u, 0u} ||
        migrated.Value().GetSchemaVersion() != SchemaVersion{3u, 0u, 0u} || migrated.Value().GetTypeId() != type)
    {
        return false;
    }

    const auto original_reader = services.Value().archives->CreateReader(original.Value());
    const auto migrated_reader = services.Value().archives->CreateReader(migrated.Value());
    return original_reader && migrated_reader && original_reader.Value()->ReadString("name").Value() == "original" &&
           migrated_reader.Value()->ReadString("migration").Value() == "applied";
}

[[nodiscard]] bool TestApplyMigrationsValidatesEachStepOutput()
{
    const auto services = CreateSerializationServices();
    if (!services || !services.Value().migrations || !services.Value().archives)
    {
        return false;
    }

    const auto type = Id("serialization.validation");
    if (!services.Value().migrations->RegisterMigration(
            std::make_shared<ProbeMigration>(MigrationKey{type, {1u, 0u, 0u}, {2u, 0u, 0u}})))
    {
        return false;
    }

    auto writer = services.Value().archives->CreateWriter();
    if (!writer || !writer->WriteString("name", "original"))
    {
        return false;
    }
    const auto original = writer->Finalize(type, {1u, 0u, 0u});
    if (!original)
    {
        return false;
    }

    MetadataOverrideArchiveFactory invalid_factory{MetadataOverrideWriter::Mode::InvalidDocument};
    MetadataOverrideArchiveFactory wrong_type_factory{MetadataOverrideWriter::Mode::WrongType};
    MetadataOverrideArchiveFactory wrong_version_factory{MetadataOverrideWriter::Mode::WrongVersion};

    const auto invalid = ApplyMigrations(original.Value(), {2u, 0u, 0u}, *services.Value().migrations, invalid_factory);
    const auto wrong_type = ApplyMigrations(original.Value(), {2u, 0u, 0u}, *services.Value().migrations, wrong_type_factory);
    const auto wrong_version = ApplyMigrations(original.Value(), {2u, 0u, 0u}, *services.Value().migrations, wrong_version_factory);

    return !invalid && invalid.GetError().HasCode("serialization.migration.invalid_output") &&
           !wrong_type && wrong_type.GetError().HasCode("serialization.migration.type_changed") &&
           !wrong_version && wrong_version.GetError().HasCode("serialization.migration.version_mismatch");
}


[[nodiscard]] bool TestArraySlotsAreWriteOnceAndComplete()
{
    InMemoryArchiveWriter writer;
    if (!writer.BeginArray("items", 1) || !writer.BeginArrayElement(0) || !writer.WriteString("name", "first") ||
        !writer.EndArrayElement())
    {
        return false;
    }
    const auto duplicate = writer.BeginArrayElement(0);
    if (duplicate || !duplicate.GetError().HasCode("serialization.duplicate_array_element"))
    {
        return false;
    }
    return writer.EndArray().HasValue();
}

[[nodiscard]] bool TestIncompleteArrayIsRejected()
{
    InMemoryArchiveWriter writer;
    if (!writer.BeginArray("items", 1))
    {
        return false;
    }
    const auto end = writer.EndArray();
    const auto finalized = writer.Finalize(Id("serialization.incomplete_array"), {1u, 0u, 0u});
    return !end && end.GetError().HasCode("serialization.array_incomplete") &&
           !finalized && finalized.GetError().HasCode("serialization.malformed");
}

[[nodiscard]] bool TestDeserializeObjectIsFailureAtomic()
{
    InMemoryArchiveWriter writer;
    if (!writer.WriteString("name", "source") || !writer.WriteUInt64("count", 1))
    {
        return false;
    }
    const auto document = writer.Finalize(Id("serialization.probe"), {1u, 0u, 0u});
    if (!document)
    {
        return false;
    }
    InMemoryArchiveReader reader(document.Value());
    ProbeData destination{"original", 7};
    const auto failed = DeserializeObject(MutatingFailDeserializeSerializer{}, reader, destination);
    if (failed || destination.name != "original" || destination.count != 7)
    {
        return false;
    }

    InMemoryArchiveReader throwing_reader(document.Value());
    const auto thrown = DeserializeObject(MutatingFailDeserializeSerializer{true}, throwing_reader, destination);
    return !thrown && thrown.GetError().HasCode("serialization.serializer.exception") &&
           destination.name == "original" && destination.count == 7;
}

[[nodiscard]] bool TestDeserializeRejectsThrowingCommitBeforeMutation()
{
    InMemoryArchiveWriter writer;
    const auto document = writer.Finalize(Id("serialization.throwing_commit"), {1u, 0u, 0u});
    if (!document)
    {
        return false;
    }
    InMemoryArchiveReader reader(document.Value());
    ThrowingCommitData destination{};
    destination.value = 7;
    const auto result = DeserializeObject(ThrowingCommitSerializer{}, reader, destination);
    return !result && result.GetError().HasCode("serialization.staging_unsupported") && destination.value == 7;
}

[[nodiscard]] bool TestRegistriesCanFreeze()
{
    SerializerRegistry serializers;
    MigrationRegistry migrations;
    const auto registered_serializer = serializers.RegisterSerializer(std::make_shared<ProbeSerializer>());
    const auto frozen_serializer = serializers.Freeze();
    const auto rejected_serializer = serializers.RegisterSerializer(std::make_shared<ProbeSerializer>());

    const auto type = Id("serialization.freeze_migration");
    const auto registered_migration = migrations.RegisterMigration(
        std::make_shared<ProbeMigration>(MigrationKey{type, {1u, 0u, 0u}, {2u, 0u, 0u}}));
    const auto frozen_migration = migrations.Freeze();
    const auto path = migrations.FindMigrationPath(type, {1u, 0u, 0u}, {2u, 0u, 0u});
    const auto rejected_migration = migrations.RegisterMigration(
        std::make_shared<ProbeMigration>(MigrationKey{type, {2u, 0u, 0u}, {3u, 0u, 0u}}));

    return registered_serializer && frozen_serializer && serializers.IsFrozen() && !rejected_serializer &&
           rejected_serializer.GetError().HasCode("serialization.registry_frozen") && registered_migration &&
           frozen_migration && migrations.IsFrozen() && path && path.Value().size() == 1 && !rejected_migration &&
           rejected_migration.GetError().HasCode("serialization.registry_frozen");
}

[[nodiscard]] bool TestSerializationServicesFactoryCreatesUsableServices()
{
    const auto services = CreateSerializationServices();
    if (!services || !services.Value().serializers || !services.Value().migrations || !services.Value().archives)
    {
        return false;
    }

    auto writer = services.Value().archives->CreateWriter();
    if (!writer || !writer->WriteString("name", "factory"))
    {
        return false;
    }
    const auto document = writer->Finalize(Id("serialization.factory"), {1u, 0u, 0u});
    if (!document)
    {
        return false;
    }

    const auto reader = services.Value().archives->CreateReader(document.Value());
    return reader && reader.Value()->ReadString("name").Value() == "factory";
}
bool TestPublicApiEvidenceCoverage()
{
    static_assert(std::has_virtual_destructor_v<epidemic::runtime::IArchiveFactory>);
    static_assert(std::has_virtual_destructor_v<epidemic::runtime::IArchiveReader>);
    static_assert(std::has_virtual_destructor_v<epidemic::runtime::IArchiveWriter>);
    static_assert(std::has_virtual_destructor_v<epidemic::runtime::IMigration>);
    static_assert(std::has_virtual_destructor_v<epidemic::runtime::IMigrationRegistry>);
    static_assert(std::has_virtual_destructor_v<epidemic::runtime::ISerializer>);
    static_assert(std::has_virtual_destructor_v<epidemic::runtime::ISerializerRegistry>);

    const SerializedDocument empty{};
    if (empty.IsValid())
    {
        return false;
    }

    const SchemaVersion one{1u, 0u, 0u};
    const SchemaVersion two{2u, 0u, 0u};
    if (!(one < two))
    {
        return false;
    }

    const MigrationKey key{Id("serialization.evidence"), one, two};
    const MigrationKey same = key;
    if (!(key == same) || std::hash<MigrationKey>{}(key) != std::hash<MigrationKey>{}(same))
    {
        return false;
    }

    ProbeSerializer serializer;
    if (serializer.GetCppType() != typeid(ProbeData))
    {
        return false;
    }

    return CreateSerializationError("serialization.evidence", "evidence").HasCode("serialization.evidence");
}

} // namespace

int main()
{
    struct NamedTest
    {
        const char* name;
        bool (*run)();
    };

    const NamedTest tests[] = {
        {"ArchiveStructuralAndReaderFailuresAreControlled", TestArchiveStructuralAndReaderFailuresAreControlled},
        {"DeepEmptyStructuresBytesAndNullRoundTrip", TestDeepEmptyStructuresBytesAndNullRoundTrip},
        {"FinalizeValidatesMetadataAndPreservesWriterOnFailure", TestFinalizeValidatesMetadataAndPreservesWriterOnFailure},
        {"ArchiveAllocationFailureAtomicity", TestArchiveAllocationFailureAtomicity},
        {"SerializeToDocumentAllocationFailureAtomicity", TestSerializeToDocumentAllocationFailureAtomicity},
        {"SerializerRegistryAndTypedBoundaryValidation", TestSerializerRegistryAndTypedBoundaryValidation},
        {"SerializerBodyExceptionsAreContained", TestSerializerBodyExceptionsAreContained},
        {"DeserializeAllocationFailureAtomicity", TestDeserializeAllocationFailureAtomicity},
        {"ApplyMigrationsAllocationFailureAtomicity", TestApplyMigrationsAllocationFailureAtomicity},
        {"MigrationFailuresPreserveSourceAndContainExceptions", TestMigrationFailuresPreserveSourceAndContainExceptions},
        {"MigrationVersionValidationAndPathAllocationAtomicity", TestMigrationVersionValidationAndPathAllocationAtomicity},
        {"PrimitiveDocumentRoundTrip", TestPrimitiveDocumentRoundTrip},
        {"DuplicateFieldsAreRejected", TestDuplicateFieldsAreRejected},
        {"NestedObjectAndArrayRoundTrip", TestNestedObjectAndArrayRoundTrip},
        {"MalformedArchiveOperationsReturnResults", TestMalformedArchiveOperationsReturnResults},
        {"FinalizeMakesWriterImmutable", TestFinalizeMakesWriterImmutable},
        {"SerializerExecutesRoundTrip", TestSerializerExecutesRoundTrip},
        {"TypedSerializerRejectsWrongCppType", TestTypedSerializerRejectsWrongCppType},
        {"SerializerRejectsWrongTypeAndMissingFields", TestSerializerRejectsWrongTypeAndMissingFields},
        {"SerializerRegistryOwnsSharedSerializers", TestSerializerRegistryOwnsSharedSerializers},
        {"MigrationRegistryFindsExactAndChainedPaths", TestMigrationRegistryFindsExactAndChainedPaths},
        {"MigrationRegistryRejectsMissingCycleAndAmbiguousPaths", TestMigrationRegistryRejectsMissingCycleAndAmbiguousPaths},
        {"ApplyMigrationsBuildsNewDocument", TestApplyMigrationsBuildsNewDocument},
        {"ApplyMigrationsValidatesEachStepOutput", TestApplyMigrationsValidatesEachStepOutput},
        {"ArraySlotsAreWriteOnceAndComplete", TestArraySlotsAreWriteOnceAndComplete},
        {"IncompleteArrayIsRejected", TestIncompleteArrayIsRejected},
        {"DeserializeObjectIsFailureAtomic", TestDeserializeObjectIsFailureAtomic},
        {"DeserializeRejectsThrowingCommit", TestDeserializeRejectsThrowingCommitBeforeMutation},
        {"RegistriesCanFreeze", TestRegistriesCanFreeze},
        {"SerializationServicesFactoryCreatesUsableServices", TestSerializationServicesFactoryCreatesUsableServices},
        {"PublicApiEvidenceCoverage", TestPublicApiEvidenceCoverage},
    };

    for (const NamedTest& test : tests)
    {
        if (!test.run())
        {
            std::cerr << "Serialization test failed: " << test.name << "\n";
            return 1;
        }
    }

    return 0;
}

