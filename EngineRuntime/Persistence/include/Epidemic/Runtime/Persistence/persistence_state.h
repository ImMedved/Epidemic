#pragma once

namespace epidemic::runtime
{
enum class PersistenceState
{
    Clean,
    Dirty,
    PendingSave,
    Saving,
    Saved,
    LoadPending,
    Loaded,
    Deleted,
    Tombstoned,
    Conflict,
    Corrupted,
};

enum class PersistentObjectKind
{
    Unknown,
    Object,
    ContainerEntry,
    SurfaceState,
    ZoneOverride,
    AbstractFact,
};
} // namespace epidemic::runtime
