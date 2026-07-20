#include "in_memory_archive.h"

#include <utility>

namespace epidemic::runtime
{
namespace
{
[[nodiscard]] foundation::Result<void> Invalid(std::string_view code, std::string_view message, std::string_view context = {})
{
    return foundation::Result<void>::Failure(CreateSerializationError(code, message, context));
}

template <typename T>
[[nodiscard]] foundation::Result<T> InvalidValue(std::string_view code, std::string_view message, std::string_view context = {})
{
    return foundation::Result<T>::Failure(CreateSerializationError(code, message, context));
}
} // namespace

InMemoryArchiveWriter::InMemoryArchiveWriter() : root_(std::make_shared<ArchiveObject>())
{
    stack_.push_back(Context{ContextKind::Object, root_, {}});
}

foundation::Result<void> InMemoryArchiveWriter::BeginObject(std::string_view name)
{
    auto object = std::make_shared<ArchiveObject>();
    auto write = WriteValue(name, ArchiveValue{object});
    if (!write)
    {
        return write;
    }
    stack_.push_back(Context{ContextKind::Object, object, {}});
    return foundation::Result<void>::Success();
}

foundation::Result<void> InMemoryArchiveWriter::EndObject()
{
    if (stack_.size() <= 1 || stack_.back().kind != ContextKind::Object)
    {
        return Invalid("serialization.malformed", "EndObject without matching BeginObject");
    }
    stack_.pop_back();
    return foundation::Result<void>::Success();
}

foundation::Result<void> InMemoryArchiveWriter::BeginArray(std::string_view name, std::size_t size)
{
    auto array = std::make_shared<ArchiveArray>();
    array->elements.resize(size);
    auto write = WriteValue(name, ArchiveValue{array});
    if (!write)
    {
        return write;
    }
    stack_.push_back(Context{ContextKind::Array, {}, array});
    return foundation::Result<void>::Success();
}

foundation::Result<void> InMemoryArchiveWriter::BeginArrayElement(std::size_t index)
{
    auto writable = EnsureWritable();
    if (!writable)
    {
        return writable;
    }
    if (stack_.empty() || stack_.back().kind != ContextKind::Array || index >= stack_.back().array->elements.size())
    {
        return Invalid("serialization.invalid_array_index", "array element index is invalid");
    }
    auto object = std::make_shared<ArchiveObject>();
    stack_.back().array->elements[index] = ArchiveValue{object};
    stack_.push_back(Context{ContextKind::ArrayElement, object, {}});
    return foundation::Result<void>::Success();
}

foundation::Result<void> InMemoryArchiveWriter::EndArrayElement()
{
    if (stack_.empty() || stack_.back().kind != ContextKind::ArrayElement)
    {
        return Invalid("serialization.malformed", "EndArrayElement without matching BeginArrayElement");
    }
    stack_.pop_back();
    return foundation::Result<void>::Success();
}

foundation::Result<void> InMemoryArchiveWriter::EndArray()
{
    if (stack_.size() <= 1 || stack_.back().kind != ContextKind::Array)
    {
        return Invalid("serialization.malformed", "EndArray without matching BeginArray");
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

foundation::Result<void> InMemoryArchiveWriter::WriteBytes(std::string_view name, std::span<const std::byte> value)
{
    return WriteValue(name, ArchiveValue{std::vector<std::byte>(value.begin(), value.end())});
}

foundation::Result<void> InMemoryArchiveWriter::WriteNull(std::string_view name)
{
    return WriteValue(name, ArchiveValue{});
}

foundation::Result<SerializedDocument> InMemoryArchiveWriter::Finalize(foundation::StringId type_id, SchemaVersion schema_version)
{
    if (finalized_)
    {
        return InvalidValue<SerializedDocument>("serialization.finalized", "archive writer is already finalized");
    }
    if (!type_id.IsValid())
    {
        return InvalidValue<SerializedDocument>("serialization.invalid_type", "serialized document type id must be valid");
    }
    if (stack_.size() != 1)
    {
        return InvalidValue<SerializedDocument>("serialization.malformed", "archive contains unclosed object or array");
    }

    finalized_ = true;
    auto impl = std::make_shared<SerializedDocument::Impl>();
    impl->type_id = type_id;
    impl->schema_version = schema_version;
    impl->format_version = 1u;
    impl->root = Snapshot();
    return foundation::Result<SerializedDocument>::Success(SerializedDocument{std::move(impl)});
}

ArchiveObjectPtr InMemoryArchiveWriter::Snapshot() const
{
    return std::make_shared<ArchiveObject>(*root_);
}

foundation::Result<void> InMemoryArchiveWriter::EnsureWritable() const
{
    if (finalized_)
    {
        return Invalid("serialization.finalized", "archive writer is finalized");
    }
    return foundation::Result<void>::Success();
}

foundation::Result<void> InMemoryArchiveWriter::WriteValue(std::string_view name, ArchiveValue value)
{
    auto writable = EnsureWritable();
    if (!writable)
    {
        return writable;
    }
    if (name.empty())
    {
        return Invalid("serialization.invalid_name", "field name must not be empty");
    }
    if (stack_.empty() || (stack_.back().kind != ContextKind::Object && stack_.back().kind != ContextKind::ArrayElement))
    {
        return Invalid("serialization.malformed", "current archive context is not an object");
    }
    auto [iterator, inserted] = stack_.back().object->fields.emplace(std::string(name), std::move(value));
    (void)iterator;
    if (!inserted)
    {
        return Invalid("serialization.duplicate_field", "archive field is already written", name);
    }
    return foundation::Result<void>::Success();
}

InMemoryArchiveReader::InMemoryArchiveReader(SerializedDocument document) : document_(std::move(document))
{
    stack_.push_back(Context{ContextKind::Object, document_.impl_ ? document_.impl_->root.get() : nullptr, nullptr});
}

InMemoryArchiveReader::InMemoryArchiveReader(ArchiveObjectPtr root)
{
    auto impl = std::make_shared<SerializedDocument::Impl>();
    impl->format_version = 1u;
    impl->root = std::move(root);
    document_ = SerializedDocument{std::move(impl)};
    stack_.push_back(Context{ContextKind::Object, document_.impl_->root.get(), nullptr});
}

foundation::StringId InMemoryArchiveReader::GetTypeId() const { return document_.GetTypeId(); }
SchemaVersion InMemoryArchiveReader::GetSchemaVersion() const { return document_.GetSchemaVersion(); }
std::uint32_t InMemoryArchiveReader::GetFormatVersion() const { return document_.GetFormatVersion(); }

foundation::Result<void> InMemoryArchiveReader::BeginObject(std::string_view name)
{
    const auto value = FindValue(name);
    if (!value)
    {
        return foundation::Result<void>::Failure(value.GetError());
    }
    const auto object = std::get_if<ArchiveObjectPtr>(&value.Value()->storage);
    if (object == nullptr || !(*object))
    {
        return Invalid("serialization.type_mismatch", "archive field is not an object", name);
    }
    stack_.push_back(Context{ContextKind::Object, object->get(), nullptr});
    return foundation::Result<void>::Success();
}

foundation::Result<void> InMemoryArchiveReader::EndObject()
{
    if (stack_.size() <= 1 || stack_.back().kind != ContextKind::Object)
    {
        return Invalid("serialization.malformed", "EndObject without matching BeginObject");
    }
    stack_.pop_back();
    return foundation::Result<void>::Success();
}

foundation::Result<std::size_t> InMemoryArchiveReader::BeginArray(std::string_view name)
{
    const auto value = FindValue(name);
    if (!value)
    {
        return foundation::Result<std::size_t>::Failure(value.GetError());
    }
    const auto array = std::get_if<ArchiveArrayPtr>(&value.Value()->storage);
    if (array == nullptr || !(*array))
    {
        return InvalidValue<std::size_t>("serialization.type_mismatch", "archive field is not an array", name);
    }
    stack_.push_back(Context{ContextKind::Array, nullptr, array->get()});
    return foundation::Result<std::size_t>::Success((*array)->elements.size());
}

foundation::Result<void> InMemoryArchiveReader::BeginArrayElement(std::size_t index)
{
    if (stack_.empty() || stack_.back().kind != ContextKind::Array || index >= stack_.back().array->elements.size())
    {
        return Invalid("serialization.invalid_array_index", "array element index is invalid");
    }
    const auto object = std::get_if<ArchiveObjectPtr>(&stack_.back().array->elements[index].storage);
    if (object == nullptr || !(*object))
    {
        return Invalid("serialization.type_mismatch", "array element is not an object");
    }
    stack_.push_back(Context{ContextKind::ArrayElement, object->get(), nullptr});
    return foundation::Result<void>::Success();
}

foundation::Result<void> InMemoryArchiveReader::EndArrayElement()
{
    if (stack_.empty() || stack_.back().kind != ContextKind::ArrayElement)
    {
        return Invalid("serialization.malformed", "EndArrayElement without matching BeginArrayElement");
    }
    stack_.pop_back();
    return foundation::Result<void>::Success();
}

foundation::Result<void> InMemoryArchiveReader::EndArray()
{
    if (stack_.size() <= 1 || stack_.back().kind != ContextKind::Array)
    {
        return Invalid("serialization.malformed", "EndArray without matching BeginArray");
    }
    stack_.pop_back();
    return foundation::Result<void>::Success();
}

#define EPIDEMIC_READ_VALUE(name, cpp_type, error_name) \
    const auto value_result = FindValue(name); \
    if (!value_result) { return foundation::Result<cpp_type>::Failure(value_result.GetError()); } \
    const auto value = std::get_if<cpp_type>(&value_result.Value()->storage); \
    if (value == nullptr) { return foundation::Result<cpp_type>::Failure(CreateSerializationError("serialization.type_mismatch", error_name, name)); } \
    return foundation::Result<cpp_type>::Success(*value)

foundation::Result<std::string> InMemoryArchiveReader::ReadString(std::string_view name) const { EPIDEMIC_READ_VALUE(name, std::string, "archive field is not a string"); }
foundation::Result<std::uint64_t> InMemoryArchiveReader::ReadUInt64(std::string_view name) const { EPIDEMIC_READ_VALUE(name, std::uint64_t, "archive field is not a uint64"); }
foundation::Result<std::int64_t> InMemoryArchiveReader::ReadInt64(std::string_view name) const { EPIDEMIC_READ_VALUE(name, std::int64_t, "archive field is not an int64"); }
foundation::Result<double> InMemoryArchiveReader::ReadDouble(std::string_view name) const { EPIDEMIC_READ_VALUE(name, double, "archive field is not a double"); }
foundation::Result<bool> InMemoryArchiveReader::ReadBool(std::string_view name) const { EPIDEMIC_READ_VALUE(name, bool, "archive field is not a bool"); }
foundation::Result<std::vector<std::byte>> InMemoryArchiveReader::ReadBytes(std::string_view name) const { EPIDEMIC_READ_VALUE(name, std::vector<std::byte>, "archive field is not bytes"); }

#undef EPIDEMIC_READ_VALUE

foundation::Result<bool> InMemoryArchiveReader::IsNull(std::string_view name) const
{
    const auto value = FindValue(name);
    if (!value)
    {
        return foundation::Result<bool>::Failure(value.GetError());
    }
    return foundation::Result<bool>::Success(value.Value()->Kind() == ArchiveValueKind::Null);
}

const ArchiveObject* InMemoryArchiveReader::CurrentObject() const
{
    return stack_.back().object;
}

foundation::Result<const ArchiveValue*> InMemoryArchiveReader::FindValue(std::string_view name) const
{
    if (name.empty())
    {
        return InvalidValue<const ArchiveValue*>("serialization.invalid_name", "field name must not be empty");
    }
    if (stack_.empty() || (stack_.back().kind != ContextKind::Object && stack_.back().kind != ContextKind::ArrayElement))
    {
        return InvalidValue<const ArchiveValue*>("serialization.malformed", "current archive context is not an object");
    }
    const ArchiveObject* current = CurrentObject();
    if (current == nullptr)
    {
        return InvalidValue<const ArchiveValue*>("serialization.invalid_document", "serialized document has no root");
    }
    const auto iterator = current->fields.find(std::string(name));
    if (iterator == current->fields.end())
    {
        return InvalidValue<const ArchiveValue*>("serialization.field_missing", "archive field was not found", name);
    }
    return foundation::Result<const ArchiveValue*>::Success(&iterator->second);
}

std::unique_ptr<IArchiveWriter> InMemoryArchiveFactory::CreateWriter() const
{
    return std::make_unique<InMemoryArchiveWriter>();
}

foundation::Result<std::unique_ptr<IArchiveReader>> InMemoryArchiveFactory::CreateReader(const SerializedDocument& document) const
{
    if (!document.IsValid() || !document.impl_->root)
    {
        return InvalidValue<std::unique_ptr<IArchiveReader>>("serialization.invalid_document", "serialized document has no root");
    }
    return foundation::Result<std::unique_ptr<IArchiveReader>>::Success(std::make_unique<InMemoryArchiveReader>(document));
}
} // namespace epidemic::runtime
