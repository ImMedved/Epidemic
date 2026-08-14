#pragma once

#include "Epidemic/Foundation/error.h"
#include "Epidemic/Foundation/result.h"
#include "Epidemic/GameFramework/Foundation/gameplay_foundation.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace epidemic::gameplay::time
{
struct CalendarDefinition
{
    std::uint32_t hours_per_day = 24;
    std::uint32_t days_per_month = 30;
    std::uint32_t months_per_year = 12;
};

struct CalendarDate
{
    std::int64_t year = 1;
    std::uint32_t month = 1;
    std::uint32_t day = 1;
    std::uint32_t hour = 0;
    std::uint32_t minute = 0;
    std::uint32_t second = 0;

    [[nodiscard]] constexpr bool operator==(const CalendarDate&) const noexcept = default;
};
enum class RecurrenceKind
{
    Once,
    FixedInterval,
    CalendarPattern,
};

enum class CatchUpPolicy
{
    FireEach,
    FireOnce,
    SkipMissed,
    Aggregate,
};

enum class SchedulePersistence
{
    Session,
    Persistent,
};

struct CalendarPattern
{
    std::uint32_t every_days = 1;
    std::uint32_t hour = 0;
    std::uint32_t minute = 0;
    std::uint32_t second = 0;
};

struct RecurrenceRule
{
    RecurrenceKind kind = RecurrenceKind::Once;
    GameplayDuration interval{};
    CalendarPattern calendar{};
};

struct ClockDefinition
{
    ClockId id{};
    std::string canonical_name;
    std::optional<CalendarDefinition> calendar{};
};

struct ClockState
{
    ClockId id{};
    GameplayTimePoint now{};
    Revision source_revision{};
};

struct ScheduleEntry
{
    ScheduleId id{};
    ClockId clock{};
    GameplayTimePoint due{};
    GameplayObjectRef owner{};
    ActionTypeId action{};
    RecurrenceRule recurrence{};
    CatchUpPolicy catch_up = CatchUpPolicy::FireOnce;
    SchedulePersistence persistence = SchedulePersistence::Session;
};

struct ScheduledTrigger
{
    ScheduleId schedule{};
    ClockId clock{};
    GameplayObjectRef owner{};
    ActionTypeId action{};
    GameplayTimePoint scheduled_for{};
    GameplayTimePoint observed_at{};
    std::uint64_t occurrence_count = 1;
};

struct SchedulerBudget
{
    std::uint64_t max_triggers = 10000;
    std::uint64_t max_catch_up_occurrences = 1000000;
};

struct GameplayTimeDiagnostics
{
    std::uint64_t active_schedules = 0;
    std::uint64_t persistent_schedules = 0;
    std::uint64_t emitted_triggers = 0;
    std::uint64_t cancelled_schedules = 0;
    std::uint64_t catch_up_occurrences = 0;
    std::uint64_t budget_exhaustions = 0;
};

struct GameplayTimeSnapshot
{
    std::vector<ClockState> clocks;
    std::vector<ScheduleEntry> schedules;
    MonotonicIdGenerator<ScheduleId>::Snapshot schedule_ids{};
};

class GameplayTimeService
{
  public:
    GameplayTimeService() = default;

    [[nodiscard]] foundation::Result<ClockId> RegisterClock(
        std::string_view canonical_name,
        std::optional<CalendarDefinition> calendar = std::nullopt);

    [[nodiscard]] foundation::Result<ActionTypeId> RegisterAction(std::string_view canonical_name, GameplayDomainId owner_domain);

    void Freeze() noexcept { frozen_ = true; }
    [[nodiscard]] bool IsFrozen() const noexcept { return frozen_; }

    // Mirrors an authoritative clock owned by a lower layer. This service does not
    // calculate world time scale/pause/skip; EngineRuntime/Time remains the source of truth.
    [[nodiscard]] foundation::Result<void> SynchronizeClock(ClockId clock, GameplayTimePoint now, Revision source_revision);

    [[nodiscard]] const ClockState* FindClock(ClockId clock) const noexcept;
    [[nodiscard]] const ClockDefinition* FindClockDefinition(ClockId clock) const noexcept;

    [[nodiscard]] foundation::Result<ScheduleId> Schedule(
        ClockId clock,
        GameplayTimePoint due,
        GameplayObjectRef owner,
        ActionTypeId action,
        RecurrenceRule recurrence = {},
        CatchUpPolicy catch_up = CatchUpPolicy::FireOnce,
        SchedulePersistence persistence = SchedulePersistence::Session);

    [[nodiscard]] foundation::Result<ScheduleId> ScheduleCalendar(
        ClockId clock,
        GameplayObjectRef owner,
        ActionTypeId action,
        CalendarPattern pattern,
        CatchUpPolicy catch_up = CatchUpPolicy::FireOnce,
        SchedulePersistence persistence = SchedulePersistence::Session);

    [[nodiscard]] foundation::Result<void> Cancel(ScheduleId schedule);
    [[nodiscard]] std::uint64_t CancelOwnedBy(GameplayObjectRef owner);
    [[nodiscard]] foundation::Result<void> Reschedule(ScheduleId schedule, GameplayTimePoint new_due);
    [[nodiscard]] bool HasSchedule(ScheduleId schedule) const noexcept;
    [[nodiscard]] const ScheduleEntry* FindSchedule(ScheduleId schedule) const noexcept;

    [[nodiscard]] foundation::Result<std::vector<ScheduledTrigger>> CollectDue(
        ClockId clock,
        SchedulerBudget budget = {});

    [[nodiscard]] GameplayTimeSnapshot CaptureSnapshot() const;
    [[nodiscard]] foundation::Result<void> RestoreSnapshot(GameplayTimeSnapshot snapshot);
    [[nodiscard]] GameplayTimeDiagnostics GetDiagnostics() const noexcept;

  private:
    struct ActionTypeInfo
    {
        ActionTypeId id{};
        std::string canonical_name;
        GameplayDomainId owner_domain{};
    };

    struct ScheduleKey
    {
        GameplayTimePoint due{};
        ScheduleId id{};

        [[nodiscard]] bool operator<(const ScheduleKey& other) const noexcept
        {
            if (due != other.due)
            {
                return due < other.due;
            }
            return id < other.id;
        }
    };

    [[nodiscard]] foundation::Result<void> ValidateRecurrence(ClockId clock, const RecurrenceRule& recurrence) const;
    [[nodiscard]] foundation::Result<GameplayTimePoint> NextCalendarDue(ClockId clock, CalendarPattern pattern) const;
    [[nodiscard]] std::uint64_t CalculateOccurrences(const ScheduleEntry& entry, GameplayTimePoint now) const noexcept;
    [[nodiscard]] GameplayTimePoint AdvanceDue(const ScheduleEntry& entry, std::uint64_t occurrences) const noexcept;
    void InsertScheduleIndex(const ScheduleEntry& entry);
    void RemoveScheduleIndex(const ScheduleEntry& entry);

    std::unordered_map<ClockId, ClockDefinition> clock_definitions_;
    std::unordered_map<ClockId, ClockState> clocks_;
    std::unordered_map<ActionTypeId, ActionTypeInfo> action_types_;
    std::unordered_map<ScheduleId, ScheduleEntry> schedules_;
    std::unordered_map<ClockId, std::set<ScheduleKey>> schedule_index_;

    MonotonicIdGenerator<ScheduleId> schedule_ids_{ScheduleId::FromString("framework.gameplay_time").High()};
    bool frozen_ = false;

    std::uint64_t emitted_triggers_ = 0;
    std::uint64_t cancelled_schedules_ = 0;
    std::uint64_t catch_up_occurrences_ = 0;
    std::uint64_t budget_exhaustions_ = 0;
};
} // namespace epidemic::gameplay::time
