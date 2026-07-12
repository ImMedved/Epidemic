#include "Epidemic/Runtime/Serialization/archive_reader.h"
#include "Epidemic/Runtime/Serialization/archive_writer.h"
#include "Epidemic/Runtime/Serialization/migration.h"
#include "Epidemic/Runtime/Serialization/migration_registry.h"
#include "Epidemic/Runtime/Serialization/schema_version.h"
#include "Epidemic/Runtime/Serialization/serializer.h"
#include "Epidemic/Runtime/Serialization/serializer_registry.h"
#include "in_memory_archive.h"
#include "migration_registry.h"
#include "serializer_registry.h"

// File note:
// Focused module-level tests for the surrounding runtime component. Each helper builds
// a narrow fixture, and each Test* function verifies one public contract or regression.
#include <type_traits>

namespace
{
using epidemic::runtime::IArchiveReader;
using epidemic::runtime::IArchiveWriter;
using epidemic::runtime::IMigration;
using epidemic::runtime::IMigrationRegistry;
using epidemic::runtime::ISerializer;
using epidemic::runtime::ISerializerRegistry;
using epidemic::runtime::InMemoryArchiveReader;
using epidemic::runtime::InMemoryArchiveWriter;
using epidemic::runtime::MigrationKey;
using epidemic::runtime::MigrationRegistry;
using epidemic::runtime::SchemaVersion;
using epidemic::runtime::SerializationState;
using epidemic::runtime::SerializerRegistry;

class ProbeSerializer final : public ISerializer
{
  public:
    ProbeSerializer(const char* type_name, SchemaVersion version)
        // Function note: Handles type id .
        // Inputs/outputs: see the signature; the method consumes caller-provided values and
        // returns either a value, status flag or Result according to the surrounding API.
        // Relations: this member is part of the local runtime workflow and pairs with
        // neighboring query/update helpers defined in the same class or file.
        : type_id_(epidemic::foundation::StringId::FromString(type_name)), version_(version)
    {
    }

    // Function note: Gets type id.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] epidemic::foundation::StringId GetTypeId() const override
    {
        return type_id_;
    }

    // Function note: Gets schema version.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] SchemaVersion GetSchemaVersion() const override
    {
        return version_;
    }

  private:
    epidemic::foundation::StringId type_id_{};
    SchemaVersion version_{};
};

class ProbeMigration final : public IMigration
{
  public:
    // Function note: Handles probe migration.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    explicit ProbeMigration(MigrationKey key) : key_(key)
    {
    }

    // Function note: Gets key.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] MigrationKey GetKey() const override
    {
        return key_;
    }

    // Function note: Handles apply.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    [[nodiscard]] epidemic::foundation::Result<void> Apply(IArchiveReader& input, IArchiveWriter& output) override
    {
        (void)input;
        (void)output;
        return epidemic::foundation::Result<void>::Success();
    }

  private:
    MigrationKey key_{};
};

// Verifies primitive round trip.
bool TestPrimitiveRoundTrip()
{
    InMemoryArchiveWriter writer;
    if (!writer.WriteString("name", "potato") || !writer.WriteUInt64("count", 7u) || !writer.WriteInt64("delta", -4) ||
        !writer.WriteDouble("weight", 1.5) || !writer.WriteBool("fresh", true))
    {
        return false;
    }

    // Function note: Handles reader.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    InMemoryArchiveReader reader(writer.Snapshot());
    return reader.ReadString("name").Value() == "potato" && reader.ReadUInt64("count").Value() == 7u &&
           reader.ReadInt64("delta").Value() == -4 && reader.ReadDouble("weight").Value() == 1.5 &&
           reader.ReadBool("fresh").Value();
}

// Verifies nested object round trip.
bool TestNestedObjectRoundTrip()
{
    InMemoryArchiveWriter writer;
    if (!writer.BeginObject("item") || !writer.WriteString("id", "items/potato") || !writer.EndObject())
    {
        return false;
    }

    // Function note: Handles reader.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    InMemoryArchiveReader reader(writer.Snapshot());
    return reader.BeginObject("item") && reader.ReadString("id").Value() == "items/potato" && reader.EndObject();
}

// Verifies object nesting validation.
bool TestObjectNestingValidation()
{
    InMemoryArchiveWriter writer;
    if (writer.EndObject() || !writer.BeginObject("item") || !writer.WriteString("name", "potato") || !writer.EndObject())
    {
        return false;
    }

    // Function note: Handles reader.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    InMemoryArchiveReader reader(writer.Snapshot());
    return !reader.EndObject() && reader.BeginObject("item") && !reader.BeginObject("name") && reader.EndObject();
}

// Verifies missing field returns error.
bool TestMissingFieldReturnsError()
{
    InMemoryArchiveWriter writer;
    if (!writer.WriteString("name", "potato"))
    {
        return false;
    }

    // Function note: Handles reader.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    InMemoryArchiveReader reader(writer.Snapshot());
    const auto missing = reader.ReadString("missing");
    return !missing && missing.GetError().HasCode("serialization.field_missing");
}

// Verifies schema version comparison works.
bool TestSchemaVersionComparisonWorks()
{
    const SchemaVersion old_version{1u, 2u, 0u};
    const SchemaVersion new_version{1u, 3u, 0u};
    const SchemaVersion same_version{1u, 3u, 0u};

    return old_version < new_version && new_version == same_version && !(new_version < same_version);
}

// Verifies serializer registry rejects duplicate type.
bool TestSerializerRegistryRejectsDuplicateType()
{
    SerializerRegistry registry;
    // Function note: Handles first.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    ProbeSerializer first("item.record", {1u, 0u, 0u});
    // Function note: Handles second.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    ProbeSerializer second("item.record", {2u, 0u, 0u});

    const auto first_result = registry.RegisterSerializer(first);
    const auto second_result = registry.RegisterSerializer(second);
    return first_result && !second_result && second_result.GetError().HasCode("serialization.serializer.duplicate_type") &&
           registry.FindSerializer(first.GetTypeId()) == &first;
}

// Verifies migration registry stores migration key.
bool TestMigrationRegistryStoresMigrationKey()
{
    MigrationRegistry registry;
    // Function note: Handles from string.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    const MigrationKey key{epidemic::foundation::StringId::FromString("item.record"), {1u, 0u, 0u}, {2u, 0u, 0u}};
    // Function note: Handles migration.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    ProbeMigration migration(key);

    const auto register_result = registry.RegisterMigration(migration);
    return register_result && registry.HasMigration(key) && registry.FindMigration(key) == &migration;
}

// Verifies migration registry rejects duplicate key.
bool TestMigrationRegistryRejectsDuplicateKey()
{
    MigrationRegistry registry;
    // Function note: Handles from string.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    const MigrationKey key{epidemic::foundation::StringId::FromString("item.record"), {1u, 0u, 0u}, {2u, 0u, 0u}};
    // Function note: Handles first.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    ProbeMigration first(key);
    // Function note: Handles second.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    ProbeMigration second(key);

    const auto first_result = registry.RegisterMigration(first);
    const auto second_result = registry.RegisterMigration(second);
    return first_result && !second_result && second_result.GetError().HasCode("serialization.migration.duplicate_key");
}
} // namespace

// Runs the local test suite and maps failures to stable exit codes.
int main()
{
    // Function note: Handles static assert.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    static_assert(std::is_abstract_v<IArchiveReader>);
    // Function note: Handles static assert.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    static_assert(std::is_abstract_v<IArchiveWriter>);
    // Function note: Handles static assert.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    static_assert(std::is_abstract_v<IMigration>);
    // Function note: Handles static assert.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    static_assert(std::is_abstract_v<IMigrationRegistry>);
    // Function note: Handles static assert.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    static_assert(std::is_abstract_v<ISerializer>);
    // Function note: Handles static assert.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    static_assert(std::is_abstract_v<ISerializerRegistry>);
    // Function note: Handles static assert.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    static_assert(static_cast<int>(SerializationState::SchemaUnknown) != static_cast<int>(SerializationState::Failed));

    if (!TestPrimitiveRoundTrip())
    {
        return 1;
    }

    if (!TestNestedObjectRoundTrip())
    {
        return 2;
    }

    if (!TestObjectNestingValidation())
    {
        return 3;
    }

    if (!TestMissingFieldReturnsError())
    {
        return 4;
    }

    if (!TestSchemaVersionComparisonWorks())
    {
        return 5;
    }

    if (!TestSerializerRegistryRejectsDuplicateType())
    {
        return 6;
    }

    if (!TestMigrationRegistryStoresMigrationKey())
    {
        return 7;
    }

    if (!TestMigrationRegistryRejectsDuplicateKey())
    {
        return 8;
    }

    return 0;
}
