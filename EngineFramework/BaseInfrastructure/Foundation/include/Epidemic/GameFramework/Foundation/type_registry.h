#pragma once

#include "Epidemic/Foundation/error.h"
#include "Epidemic/Foundation/result.h"
#include "Epidemic/GameFramework/Foundation/ids.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace epidemic::gameplay
{
template <typename TId> class StableTypeRegistry
{
  public:
    struct Entry
    {
        TId id{};
        std::string canonical_name;
    };

    [[nodiscard]] foundation::Result<TId> Register(std::string_view canonical_name)
    {
        if (frozen_)
        {
            return foundation::Result<TId>::Failure(
                foundation::Error::Create("gameplay.registry_frozen", "registry is frozen", std::string(canonical_name)));
        }
        if (!detail::IsCanonicalName(canonical_name))
        {
            return foundation::Result<TId>::Failure(
                foundation::Error::Create("gameplay.invalid_name", "canonical name is invalid", std::string(canonical_name)));
        }

        const TId id = TId::FromString(canonical_name);
        const auto found = entries_.find(id.Raw());
        if (found != entries_.end())
        {
            if (found->second.canonical_name != canonical_name)
            {
                return foundation::Result<TId>::Failure(
                    foundation::Error::Create("gameplay.id_collision", "stable type id collision", std::string(canonical_name)));
            }
            return foundation::Result<TId>::Failure(foundation::Error::Create(
                "gameplay.already_registered", "canonical name is already registered", std::string(canonical_name)));
        }

        entries_.emplace(id.Raw(), Entry{id, std::string(canonical_name)});
        return foundation::Result<TId>::Success(id);
    }

    [[nodiscard]] const Entry* Find(TId id) const noexcept
    {
        const auto found = entries_.find(id.Raw());
        return found == entries_.end() ? nullptr : &found->second;
    }

    [[nodiscard]] const Entry* Find(std::string_view canonical_name) const noexcept
    {
        if (!detail::IsCanonicalName(canonical_name))
        {
            return nullptr;
        }
        const TId id = TId::FromString(canonical_name);
        const auto* entry = Find(id);
        return entry != nullptr && entry->canonical_name == canonical_name ? entry : nullptr;
    }

    void Freeze() noexcept { frozen_ = true; }
    [[nodiscard]] bool IsFrozen() const noexcept { return frozen_; }
    [[nodiscard]] std::size_t Size() const noexcept { return entries_.size(); }

    [[nodiscard]] std::vector<Entry> Entries() const
    {
        std::vector<Entry> result;
        result.reserve(entries_.size());
        for (const auto& [_, entry] : entries_)
        {
            result.push_back(entry);
        }
        std::sort(result.begin(), result.end(), [](const Entry& left, const Entry& right) {
            return left.id.Raw() < right.id.Raw();
        });
        return result;
    }

  private:
    std::unordered_map<std::uint64_t, Entry> entries_;
    bool frozen_ = false;
};
} // namespace epidemic::gameplay