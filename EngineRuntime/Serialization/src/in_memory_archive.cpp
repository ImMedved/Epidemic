#include "in_memory_archive.h"

// File note:
// Implementation file for the surrounding runtime type or test fixture. The comments
// below describe responsibilities, data flow and relationships between local helpers.
namespace epidemic::runtime
{
namespace
{
// Function note: Handles as object.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
[[nodiscard]] foundation::Result<const ArchiveObjectPtr*> AsObject(const ArchiveValue* value, std::string_view name)
{
    const auto object = std::get_if<ArchiveObjectPtr>(&value->storage);
    if (object == nullptr || !(*object))
    {
        return foundation::Result<const ArchiveObjectPtr*>::Failure(
            // Function note: Creates serialization error.
            // Inputs/outputs: see the signature; the method consumes caller-provided values and
            // returns either a value, status flag or Result according to the surrounding API.
            // Relations: this member is part of the local runtime workflow and pairs with
            // neighboring query/update helpers defined in the same class or file.
            CreateSerializationError("serialization.type_mismatch", "archive field is not an object", name));
    }

    return foundation::Result<const ArchiveObjectPtr*>::Success(object);
}

// Function note: Handles as mutable object.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
[[nodiscard]] foundation::Result<ArchiveObjectPtr*> AsMutableObject(ArchiveValue* value, std::string_view name)
{
    auto object = std::get_if<ArchiveObjectPtr>(&value->storage);
    if (object == nullptr || !(*object))
    {
        return foundation::Result<ArchiveObjectPtr*>::Failure(
            // Function note: Creates serialization error.
            // Inputs/outputs: see the signature; the method consumes caller-provided values and
            // returns either a value, status flag or Result according to the surrounding API.
            // Relations: this member is part of the local runtime workflow and pairs with
            // neighboring query/update helpers defined in the same class or file.
            CreateSerializationError("serialization.type_mismatch", "archive field is not an object", name));
    }

    return foundation::Result<ArchiveObjectPtr*>::Success(object);
}
} // namespace

// Function note: Handles in memory archive writer.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
InMemoryArchiveWriter::InMemoryArchiveWriter() : root_(std::make_shared<ArchiveObject>())
{
    stack_.push_back(root_);
}

// Function note: Handles begin object.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
foundation::Result<void> InMemoryArchiveWriter::BeginObject(std::string_view name)
{
    auto object = std::make_shared<ArchiveObject>();
    if (name.empty())
    {
        return foundation::Result<void>::Failure(
            // Function note: Creates serialization error.
            // Inputs/outputs: see the signature; the method consumes caller-provided values and
            // returns either a value, status flag or Result according to the surrounding API.
            // Relations: this member is part of the local runtime workflow and pairs with
            // neighboring query/update helpers defined in the same class or file.
            CreateSerializationError("serialization.invalid_name", "object name must not be empty"));
    }

    // Function note: Writes value.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    const auto write_result = WriteValue(name, ArchiveValue{object});
    if (!write_result)
    {
        return write_result;
    }

    stack_.push_back(std::move(object));
    return foundation::Result<void>::Success();
}

// Function note: Handles end object.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
foundation::Result<void> InMemoryArchiveWriter::EndObject()
{
    if (stack_.size() <= 1)
    {
        return foundation::Result<void>::Failure(
            // Function note: Creates serialization error.
            // Inputs/outputs: see the signature; the method consumes caller-provided values and
            // returns either a value, status flag or Result according to the surrounding API.
            // Relations: this member is part of the local runtime workflow and pairs with
            // neighboring query/update helpers defined in the same class or file.
            CreateSerializationError("serialization.object_stack_underflow", "no open object to close"));
    }

    stack_.pop_back();
    return foundation::Result<void>::Success();
}

// Function note: Writes string.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
foundation::Result<void> InMemoryArchiveWriter::WriteString(std::string_view name, std::string_view value)
{
    return WriteValue(name, ArchiveValue{std::string(value)});
}

// Function note: Writes uint64.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
foundation::Result<void> InMemoryArchiveWriter::WriteUInt64(std::string_view name, std::uint64_t value)
{
    return WriteValue(name, ArchiveValue{value});
}

// Function note: Writes int64.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
foundation::Result<void> InMemoryArchiveWriter::WriteInt64(std::string_view name, std::int64_t value)
{
    return WriteValue(name, ArchiveValue{value});
}

// Function note: Writes double.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
foundation::Result<void> InMemoryArchiveWriter::WriteDouble(std::string_view name, double value)
{
    return WriteValue(name, ArchiveValue{value});
}

// Function note: Writes bool.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
foundation::Result<void> InMemoryArchiveWriter::WriteBool(std::string_view name, bool value)
{
    return WriteValue(name, ArchiveValue{value});
}

// Function note: Handles snapshot.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
ArchiveObjectPtr InMemoryArchiveWriter::Snapshot() const
{
    return root_;
}

// Function note: Writes value.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
foundation::Result<void> InMemoryArchiveWriter::WriteValue(std::string_view name, ArchiveValue value)
{
    if (name.empty())
    {
        return foundation::Result<void>::Failure(
            // Function note: Creates serialization error.
            // Inputs/outputs: see the signature; the method consumes caller-provided values and
            // returns either a value, status flag or Result according to the surrounding API.
            // Relations: this member is part of the local runtime workflow and pairs with
            // neighboring query/update helpers defined in the same class or file.
            CreateSerializationError("serialization.invalid_name", "field name must not be empty"));
    }

    ArchiveObjectPtr& current = stack_.back();
    // Function note: Handles string.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    current->fields[std::string(name)] = std::move(value);
    return foundation::Result<void>::Success();
}

// Function note: Handles in memory archive reader.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
InMemoryArchiveReader::InMemoryArchiveReader(ArchiveObjectPtr root) : root_(std::move(root))
{
    stack_.push_back(root_.get());
}

// Function note: Handles begin object.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
foundation::Result<void> InMemoryArchiveReader::BeginObject(std::string_view name)
{
    if (name.empty())
    {
        return foundation::Result<void>::Failure(
            // Function note: Creates serialization error.
            // Inputs/outputs: see the signature; the method consumes caller-provided values and
            // returns either a value, status flag or Result according to the surrounding API.
            // Relations: this member is part of the local runtime workflow and pairs with
            // neighboring query/update helpers defined in the same class or file.
            CreateSerializationError("serialization.invalid_name", "object name must not be empty"));
    }

    // Function note: Finds value.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    const auto value_result = FindValue(name);
    if (!value_result)
    {
        return foundation::Result<void>::Failure(value_result.GetError());
    }

    // Function note: Handles as object.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    const auto object_result = AsObject(value_result.Value(), name);
    if (!object_result)
    {
        return foundation::Result<void>::Failure(object_result.GetError());
    }

    stack_.push_back(object_result.Value()->get());
    return foundation::Result<void>::Success();
}

// Function note: Handles end object.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
foundation::Result<void> InMemoryArchiveReader::EndObject()
{
    if (stack_.size() <= 1)
    {
        return foundation::Result<void>::Failure(
            // Function note: Creates serialization error.
            // Inputs/outputs: see the signature; the method consumes caller-provided values and
            // returns either a value, status flag or Result according to the surrounding API.
            // Relations: this member is part of the local runtime workflow and pairs with
            // neighboring query/update helpers defined in the same class or file.
            CreateSerializationError("serialization.object_stack_underflow", "no open object to close"));
    }

    stack_.pop_back();
    return foundation::Result<void>::Success();
}

// Function note: Reads string.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
foundation::Result<std::string> InMemoryArchiveReader::ReadString(std::string_view name) const
{
    // Function note: Finds value.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    const auto value_result = FindValue(name);
    if (!value_result)
    {
        return foundation::Result<std::string>::Failure(value_result.GetError());
    }

    const auto value = std::get_if<std::string>(&value_result.Value()->storage);
    if (value == nullptr)
    {
        return foundation::Result<std::string>::Failure(
            // Function note: Creates serialization error.
            // Inputs/outputs: see the signature; the method consumes caller-provided values and
            // returns either a value, status flag or Result according to the surrounding API.
            // Relations: this member is part of the local runtime workflow and pairs with
            // neighboring query/update helpers defined in the same class or file.
            CreateSerializationError("serialization.type_mismatch", "archive field is not a string", name));
    }

    return foundation::Result<std::string>::Success(*value);
}

// Function note: Reads uint64.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
foundation::Result<std::uint64_t> InMemoryArchiveReader::ReadUInt64(std::string_view name) const
{
    // Function note: Finds value.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    const auto value_result = FindValue(name);
    if (!value_result)
    {
        return foundation::Result<std::uint64_t>::Failure(value_result.GetError());
    }

    const auto value = std::get_if<std::uint64_t>(&value_result.Value()->storage);
    if (value == nullptr)
    {
        return foundation::Result<std::uint64_t>::Failure(
            // Function note: Creates serialization error.
            // Inputs/outputs: see the signature; the method consumes caller-provided values and
            // returns either a value, status flag or Result according to the surrounding API.
            // Relations: this member is part of the local runtime workflow and pairs with
            // neighboring query/update helpers defined in the same class or file.
            CreateSerializationError("serialization.type_mismatch", "archive field is not a uint64", name));
    }

    return foundation::Result<std::uint64_t>::Success(*value);
}

// Function note: Reads int64.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
foundation::Result<std::int64_t> InMemoryArchiveReader::ReadInt64(std::string_view name) const
{
    // Function note: Finds value.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    const auto value_result = FindValue(name);
    if (!value_result)
    {
        return foundation::Result<std::int64_t>::Failure(value_result.GetError());
    }

    const auto value = std::get_if<std::int64_t>(&value_result.Value()->storage);
    if (value == nullptr)
    {
        return foundation::Result<std::int64_t>::Failure(
            // Function note: Creates serialization error.
            // Inputs/outputs: see the signature; the method consumes caller-provided values and
            // returns either a value, status flag or Result according to the surrounding API.
            // Relations: this member is part of the local runtime workflow and pairs with
            // neighboring query/update helpers defined in the same class or file.
            CreateSerializationError("serialization.type_mismatch", "archive field is not an int64", name));
    }

    return foundation::Result<std::int64_t>::Success(*value);
}

// Function note: Reads double.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
foundation::Result<double> InMemoryArchiveReader::ReadDouble(std::string_view name) const
{
    // Function note: Finds value.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    const auto value_result = FindValue(name);
    if (!value_result)
    {
        return foundation::Result<double>::Failure(value_result.GetError());
    }

    const auto value = std::get_if<double>(&value_result.Value()->storage);
    if (value == nullptr)
    {
        return foundation::Result<double>::Failure(
            // Function note: Creates serialization error.
            // Inputs/outputs: see the signature; the method consumes caller-provided values and
            // returns either a value, status flag or Result according to the surrounding API.
            // Relations: this member is part of the local runtime workflow and pairs with
            // neighboring query/update helpers defined in the same class or file.
            CreateSerializationError("serialization.type_mismatch", "archive field is not a double", name));
    }

    return foundation::Result<double>::Success(*value);
}

// Function note: Reads bool.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
foundation::Result<bool> InMemoryArchiveReader::ReadBool(std::string_view name) const
{
    // Function note: Finds value.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    const auto value_result = FindValue(name);
    if (!value_result)
    {
        return foundation::Result<bool>::Failure(value_result.GetError());
    }

    const auto value = std::get_if<bool>(&value_result.Value()->storage);
    if (value == nullptr)
    {
        return foundation::Result<bool>::Failure(
            // Function note: Creates serialization error.
            // Inputs/outputs: see the signature; the method consumes caller-provided values and
            // returns either a value, status flag or Result according to the surrounding API.
            // Relations: this member is part of the local runtime workflow and pairs with
            // neighboring query/update helpers defined in the same class or file.
            CreateSerializationError("serialization.type_mismatch", "archive field is not a bool", name));
    }

    return foundation::Result<bool>::Success(*value);
}

// Function note: Handles current object.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
const ArchiveObject* InMemoryArchiveReader::CurrentObject() const
{
    return stack_.back();
}

// Function note: Finds value.
// Inputs/outputs: see the signature; the method consumes caller-provided values and
// returns either a value, status flag or Result according to the surrounding API.
// Relations: this member is part of the local runtime workflow and pairs with
// neighboring query/update helpers defined in the same class or file.
foundation::Result<const ArchiveValue*> InMemoryArchiveReader::FindValue(std::string_view name) const
{
    if (name.empty())
    {
        return foundation::Result<const ArchiveValue*>::Failure(
            // Function note: Creates serialization error.
            // Inputs/outputs: see the signature; the method consumes caller-provided values and
            // returns either a value, status flag or Result according to the surrounding API.
            // Relations: this member is part of the local runtime workflow and pairs with
            // neighboring query/update helpers defined in the same class or file.
            CreateSerializationError("serialization.invalid_name", "field name must not be empty"));
    }

    // Function note: Handles current object.
    // Inputs/outputs: see the signature; the method consumes caller-provided values and
    // returns either a value, status flag or Result according to the surrounding API.
    // Relations: this member is part of the local runtime workflow and pairs with
    // neighboring query/update helpers defined in the same class or file.
    const ArchiveObject* current = CurrentObject();
    const auto iterator = current->fields.find(std::string(name));
    if (iterator == current->fields.end())
    {
        return foundation::Result<const ArchiveValue*>::Failure(
            // Function note: Creates serialization error.
            // Inputs/outputs: see the signature; the method consumes caller-provided values and
            // returns either a value, status flag or Result according to the surrounding API.
            // Relations: this member is part of the local runtime workflow and pairs with
            // neighboring query/update helpers defined in the same class or file.
            CreateSerializationError("serialization.field_missing", "archive field was not found", name));
    }

    return foundation::Result<const ArchiveValue*>::Success(&iterator->second);
}
} // namespace epidemic::runtime
