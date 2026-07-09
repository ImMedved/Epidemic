#include "in_memory_archive.h"

namespace epidemic::runtime
{
namespace
{
[[nodiscard]] foundation::Result<const ArchiveObjectPtr*> AsObject(const ArchiveValue* value, std::string_view name)
{
    const auto object = std::get_if<ArchiveObjectPtr>(&value->storage);
    if (object == nullptr || !(*object))
    {
        return foundation::Result<const ArchiveObjectPtr*>::Failure(
            CreateSerializationError("serialization.type_mismatch", "archive field is not an object", name));
    }

    return foundation::Result<const ArchiveObjectPtr*>::Success(object);
}

[[nodiscard]] foundation::Result<ArchiveObjectPtr*> AsMutableObject(ArchiveValue* value, std::string_view name)
{
    auto object = std::get_if<ArchiveObjectPtr>(&value->storage);
    if (object == nullptr || !(*object))
    {
        return foundation::Result<ArchiveObjectPtr*>::Failure(
            CreateSerializationError("serialization.type_mismatch", "archive field is not an object", name));
    }

    return foundation::Result<ArchiveObjectPtr*>::Success(object);
}
} // namespace

InMemoryArchiveWriter::InMemoryArchiveWriter() : root_(std::make_shared<ArchiveObject>())
{
    stack_.push_back(root_);
}

foundation::Result<void> InMemoryArchiveWriter::BeginObject(std::string_view name)
{
    auto object = std::make_shared<ArchiveObject>();
    if (name.empty())
    {
        return foundation::Result<void>::Failure(
            CreateSerializationError("serialization.invalid_name", "object name must not be empty"));
    }

    const auto write_result = WriteValue(name, ArchiveValue{object});
    if (!write_result)
    {
        return write_result;
    }

    stack_.push_back(std::move(object));
    return foundation::Result<void>::Success();
}

foundation::Result<void> InMemoryArchiveWriter::EndObject()
{
    if (stack_.size() <= 1)
    {
        return foundation::Result<void>::Failure(
            CreateSerializationError("serialization.object_stack_underflow", "no open object to close"));
    }

    stack_.pop_back();
    return foundation::Result<void>::Success();
}

foundation::Result<void> InMemoryArchiveWriter::WriteString(std::string_view name, std::string_view value)
{
    return WriteValue(name, ArchiveValue{std::string(value)});
}

foundation::Result<void> InMemoryArchiveWriter::WriteUInt64(std::string_view name, std::uint64_t value)
{
    return WriteValue(name, ArchiveValue{value});
}

foundation::Result<void> InMemoryArchiveWriter::WriteInt64(std::string_view name, std::int64_t value)
{
    return WriteValue(name, ArchiveValue{value});
}

foundation::Result<void> InMemoryArchiveWriter::WriteDouble(std::string_view name, double value)
{
    return WriteValue(name, ArchiveValue{value});
}

foundation::Result<void> InMemoryArchiveWriter::WriteBool(std::string_view name, bool value)
{
    return WriteValue(name, ArchiveValue{value});
}

ArchiveObjectPtr InMemoryArchiveWriter::Snapshot() const
{
    return root_;
}

foundation::Result<void> InMemoryArchiveWriter::WriteValue(std::string_view name, ArchiveValue value)
{
    if (name.empty())
    {
        return foundation::Result<void>::Failure(
            CreateSerializationError("serialization.invalid_name", "field name must not be empty"));
    }

    ArchiveObjectPtr& current = stack_.back();
    current->fields[std::string(name)] = std::move(value);
    return foundation::Result<void>::Success();
}

InMemoryArchiveReader::InMemoryArchiveReader(ArchiveObjectPtr root) : root_(std::move(root))
{
    stack_.push_back(root_.get());
}

foundation::Result<void> InMemoryArchiveReader::BeginObject(std::string_view name)
{
    if (name.empty())
    {
        return foundation::Result<void>::Failure(
            CreateSerializationError("serialization.invalid_name", "object name must not be empty"));
    }

    const auto value_result = FindValue(name);
    if (!value_result)
    {
        return foundation::Result<void>::Failure(value_result.GetError());
    }

    const auto object_result = AsObject(value_result.Value(), name);
    if (!object_result)
    {
        return foundation::Result<void>::Failure(object_result.GetError());
    }

    stack_.push_back(object_result.Value()->get());
    return foundation::Result<void>::Success();
}

foundation::Result<void> InMemoryArchiveReader::EndObject()
{
    if (stack_.size() <= 1)
    {
        return foundation::Result<void>::Failure(
            CreateSerializationError("serialization.object_stack_underflow", "no open object to close"));
    }

    stack_.pop_back();
    return foundation::Result<void>::Success();
}

foundation::Result<std::string> InMemoryArchiveReader::ReadString(std::string_view name) const
{
    const auto value_result = FindValue(name);
    if (!value_result)
    {
        return foundation::Result<std::string>::Failure(value_result.GetError());
    }

    const auto value = std::get_if<std::string>(&value_result.Value()->storage);
    if (value == nullptr)
    {
        return foundation::Result<std::string>::Failure(
            CreateSerializationError("serialization.type_mismatch", "archive field is not a string", name));
    }

    return foundation::Result<std::string>::Success(*value);
}

foundation::Result<std::uint64_t> InMemoryArchiveReader::ReadUInt64(std::string_view name) const
{
    const auto value_result = FindValue(name);
    if (!value_result)
    {
        return foundation::Result<std::uint64_t>::Failure(value_result.GetError());
    }

    const auto value = std::get_if<std::uint64_t>(&value_result.Value()->storage);
    if (value == nullptr)
    {
        return foundation::Result<std::uint64_t>::Failure(
            CreateSerializationError("serialization.type_mismatch", "archive field is not a uint64", name));
    }

    return foundation::Result<std::uint64_t>::Success(*value);
}

foundation::Result<std::int64_t> InMemoryArchiveReader::ReadInt64(std::string_view name) const
{
    const auto value_result = FindValue(name);
    if (!value_result)
    {
        return foundation::Result<std::int64_t>::Failure(value_result.GetError());
    }

    const auto value = std::get_if<std::int64_t>(&value_result.Value()->storage);
    if (value == nullptr)
    {
        return foundation::Result<std::int64_t>::Failure(
            CreateSerializationError("serialization.type_mismatch", "archive field is not an int64", name));
    }

    return foundation::Result<std::int64_t>::Success(*value);
}

foundation::Result<double> InMemoryArchiveReader::ReadDouble(std::string_view name) const
{
    const auto value_result = FindValue(name);
    if (!value_result)
    {
        return foundation::Result<double>::Failure(value_result.GetError());
    }

    const auto value = std::get_if<double>(&value_result.Value()->storage);
    if (value == nullptr)
    {
        return foundation::Result<double>::Failure(
            CreateSerializationError("serialization.type_mismatch", "archive field is not a double", name));
    }

    return foundation::Result<double>::Success(*value);
}

foundation::Result<bool> InMemoryArchiveReader::ReadBool(std::string_view name) const
{
    const auto value_result = FindValue(name);
    if (!value_result)
    {
        return foundation::Result<bool>::Failure(value_result.GetError());
    }

    const auto value = std::get_if<bool>(&value_result.Value()->storage);
    if (value == nullptr)
    {
        return foundation::Result<bool>::Failure(
            CreateSerializationError("serialization.type_mismatch", "archive field is not a bool", name));
    }

    return foundation::Result<bool>::Success(*value);
}

const ArchiveObject* InMemoryArchiveReader::CurrentObject() const
{
    return stack_.back();
}

foundation::Result<const ArchiveValue*> InMemoryArchiveReader::FindValue(std::string_view name) const
{
    if (name.empty())
    {
        return foundation::Result<const ArchiveValue*>::Failure(
            CreateSerializationError("serialization.invalid_name", "field name must not be empty"));
    }

    const ArchiveObject* current = CurrentObject();
    const auto iterator = current->fields.find(std::string(name));
    if (iterator == current->fields.end())
    {
        return foundation::Result<const ArchiveValue*>::Failure(
            CreateSerializationError("serialization.field_missing", "archive field was not found", name));
    }

    return foundation::Result<const ArchiveValue*>::Success(&iterator->second);
}
} // namespace epidemic::runtime
