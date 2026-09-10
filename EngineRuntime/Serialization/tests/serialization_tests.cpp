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
#include <stdexcept>
#include <string>
#include <vector>

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

struct ProbeData
{
    std::string name;
    std::uint64_t count = 0;
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
} // namespace

int main()
{
    struct NamedTest
    {
        const char* name;
        bool (*run)();
    };

    const NamedTest tests[] = {
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

