#pragma once


// File note:
// Header for runtime contracts or module-local helpers. Comments document how each
// function participates in the module API and what state it observes or mutates.
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
} 
