#include "Epidemic/GameFramework/Foundation/gameplay_foundation.h"

#include "Epidemic/Foundation/error.h"

namespace epidemic::gameplay
{
foundation::Result<TagId> GameplayTagRegistry::Register(std::string_view canonical_name)
{
    if (frozen_)
    {
        return foundation::Result<TagId>::Failure(
            foundation::Error::Create("gameplay.registry_frozen", "tag registry is frozen", std::string(canonical_name)));
    }
    if (canonical_name.empty() || canonical_name.front() == '.' || canonical_name.back() == '.')
    {
        return foundation::Result<TagId>::Failure(
            foundation::Error::Create("gameplay.invalid_tag", "tag name is invalid", std::string(canonical_name)));
    }

    const TagId requested = TagId::FromString(canonical_name);
    if (const auto existing = entries_.find(requested); existing != entries_.end())
    {
        if (existing->second.canonical_name != canonical_name)
        {
            return foundation::Result<TagId>::Failure(
                foundation::Error::Create("gameplay.id_collision", "tag id collision", std::string(canonical_name)));
        }
        return foundation::Result<TagId>::Failure(
            foundation::Error::Create("gameplay.already_registered", "tag is already registered", std::string(canonical_name)));
    }

    TagId parent{};
    std::size_t segment_end = 0;
    while (true)
    {
        segment_end = canonical_name.find('.', segment_end);
        const auto current_end = segment_end == std::string_view::npos ? canonical_name.size() : segment_end;
        const auto prefix = canonical_name.substr(0, current_end);
        if (prefix.empty() || (segment_end != std::string_view::npos && segment_end + 1 < canonical_name.size() && canonical_name[segment_end + 1] == '.'))
        {
            return foundation::Result<TagId>::Failure(
                foundation::Error::Create("gameplay.invalid_tag", "tag contains an empty hierarchy segment", std::string(canonical_name)));
        }

        const TagId id = TagId::FromString(prefix);
        const auto existing = entries_.find(id);
        if (existing != entries_.end())
        {
            if (existing->second.canonical_name != prefix)
            {
                return foundation::Result<TagId>::Failure(
                    foundation::Error::Create("gameplay.id_collision", "tag id collision", std::string(prefix)));
            }
            parent = id;
        }
        else
        {
            const auto result = RegisterSingle(prefix, parent);
            if (!result)
            {
                return result;
            }
            parent = result.Value();
        }

        if (segment_end == std::string_view::npos)
        {
            return foundation::Result<TagId>::Success(parent);
        }
        ++segment_end;
    }
}

foundation::Result<TagId> GameplayTagRegistry::RegisterSingle(std::string_view canonical_name, TagId parent)
{
    const TagId id = TagId::FromString(canonical_name);
    entries_.emplace(id, Entry{id, parent, std::string(canonical_name)});
    return foundation::Result<TagId>::Success(id);
}

const GameplayTagRegistry::Entry* GameplayTagRegistry::Find(TagId id) const noexcept
{
    const auto found = entries_.find(id);
    return found == entries_.end() ? nullptr : &found->second;
}

bool GameplayTagRegistry::Matches(TagId candidate, TagId required) const noexcept
{
    if (!candidate.IsValid() || !required.IsValid())
    {
        return false;
    }

    TagId current = candidate;
    while (current.IsValid())
    {
        if (current == required)
        {
            return true;
        }
        const auto* entry = Find(current);
        if (entry == nullptr)
        {
            return false;
        }
        current = entry->parent;
    }
    return false;
}
} // namespace epidemic::gameplay
