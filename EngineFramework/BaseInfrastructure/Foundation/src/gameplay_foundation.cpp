#include "Epidemic/GameFramework/Foundation/gameplay_foundation.h"

#include "Epidemic/Foundation/error.h"

#include <string>
#include <vector>

namespace epidemic::gameplay
{
foundation::Result<TagId> GameplayTagRegistry::Register(std::string_view canonical_name)
{
    if (frozen_)
    {
        return foundation::Result<TagId>::Failure(
            foundation::Error::Create("gameplay.registry_frozen", "tag registry is frozen", std::string(canonical_name)));
    }
    if (!detail::IsCanonicalName(canonical_name))
    {
        return foundation::Result<TagId>::Failure(
            foundation::Error::Create("gameplay.invalid_tag", "tag name is invalid", std::string(canonical_name)));
    }

    struct PendingTag
    {
        TagId id{};
        TagId parent{};
        std::string canonical_name;
        bool exists = false;
    };

    std::vector<PendingTag> pending;
    TagId parent{};
    std::size_t segment_end = 0;
    while (true)
    {
        segment_end = canonical_name.find('.', segment_end);
        const auto current_end = segment_end == std::string_view::npos ? canonical_name.size() : segment_end;
        const auto prefix = canonical_name.substr(0, current_end);
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
            pending.push_back(PendingTag{id, existing->second.parent, std::string(prefix), true});
        }
        else
        {
            pending.push_back(PendingTag{id, parent, std::string(prefix), false});
            parent = id;
        }

        if (segment_end == std::string_view::npos)
        {
            break;
        }
        ++segment_end;
    }

    if (!pending.empty() && pending.back().exists)
    {
        return foundation::Result<TagId>::Failure(foundation::Error::Create(
            "gameplay.already_registered", "tag is already registered", std::string(canonical_name)));
    }

    for (const auto& tag : pending)
    {
        if (!tag.exists)
        {
            entries_.emplace(tag.id, Entry{tag.id, tag.parent, tag.canonical_name});
        }
    }
    return foundation::Result<TagId>::Success(pending.back().id);
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