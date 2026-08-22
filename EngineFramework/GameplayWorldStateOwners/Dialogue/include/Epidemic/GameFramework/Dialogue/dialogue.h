#pragma once
#include "Epidemic/Foundation/result.h"
#include "Epidemic/GameFramework/Foundation/gameplay_foundation.h"
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace epidemic::gameplay::dialogue
{
#define DLG_TYPE(name)                                                                                                 \
    struct name                                                                                                        \
    {                                                                                                                  \
        TypeId value{};                                                                                                \
        static constexpr name FromString(std::string_view s) noexcept                                                  \
        {                                                                                                              \
            return {TypeId::FromString(s)};                                                                            \
        }                                                                                                              \
        [[nodiscard]] constexpr bool IsValid() const noexcept                                                          \
        {                                                                                                              \
            return value.IsValid();                                                                                    \
        }                                                                                                              \
        [[nodiscard]] constexpr bool operator==(const name &) const noexcept = default;                                \
        [[nodiscard]] constexpr auto operator<=>(const name &) const noexcept = default;                               \
    }
#define DLG_OBJ(name)                                                                                                  \
    struct name                                                                                                        \
    {                                                                                                                  \
        GameplayObjectId value{};                                                                                      \
        static constexpr name FromRaw(std::uint64_t h, std::uint64_t l) noexcept                                       \
        {                                                                                                              \
            return {GameplayObjectId::FromRaw(h, l)};                                                                  \
        }                                                                                                              \
        [[nodiscard]] constexpr bool IsValid() const noexcept                                                          \
        {                                                                                                              \
            return value.IsValid();                                                                                    \
        }                                                                                                              \
        [[nodiscard]] constexpr bool operator==(const name &) const noexcept = default;                                \
        [[nodiscard]] constexpr auto operator<=>(const name &) const noexcept = default;                               \
    }
DLG_TYPE(ConversationDefinitionId);
DLG_TYPE(DialogueNodeId);
DLG_TYPE(DialogueOptionId);
DLG_TYPE(DialogueTopicId);
DLG_TYPE(DialogueConditionTypeId);
DLG_TYPE(DialogueConsequenceTypeId);
DLG_TYPE(DialogueTextKey);
DLG_OBJ(ConversationSessionId);
DLG_OBJ(DialogueConsequenceExecutionId);
#undef DLG_TYPE
#undef DLG_OBJ
struct IdHash
{
    template <class T> [[nodiscard]] std::size_t operator()(const T &id) const noexcept
    {
        return std::hash<decltype(id.value)>{}(id.value);
    }
};

enum class ConversationState
{
    Preparing,
    Active,
    WaitingForChoice,
    WaitingForExternalAction,
    Completed,
    Interrupted,
    Failed
};
enum class DialogueConditionState
{
    Satisfied,
    Unsatisfied,
    Unknown,
    Unavailable
};
enum class DialogueConsequenceState
{
    Pending,
    Applied,
    Deferred,
    Failed
};
enum class DialogueOptionRepeatPolicy
{
    Repeatable,
    OncePerConversation
};
enum class DialogueChangeKind
{
    ConversationStarted,
    NodeEntered,
    OptionSelected,
    ConversationCompleted,
    ConversationInterrupted,
    ConsequencePlanned,
    ConsequenceApplied,
    ConsequenceDeferred,
    ConsequenceFailed
};

struct DialogueTextRef
{
    DialogueTextKey key{};
    std::vector<TypeId> parameter_keys;
};
struct DialogueConditionDefinition
{
    TypeId id{};
    DialogueConditionTypeId type{};
    std::vector<std::byte> payload;
};
struct DialogueConsequenceDefinition
{
    TypeId id{};
    DialogueConsequenceTypeId type{};
    std::vector<std::byte> payload;
    std::int32_t priority = 0;
};
struct DialogueOptionDefinition
{
    DialogueOptionId id{};
    DialogueTextRef text;
    std::vector<TypeId> conditions;
    std::vector<TypeId> consequences;
    DialogueNodeId next_node{};
    DialogueOptionRepeatPolicy repeat_policy = DialogueOptionRepeatPolicy::Repeatable;
    GameplayTagSet tags;
};
struct DialogueNodeDefinition
{
    DialogueNodeId id{};
    DialogueTextRef utterance;
    TypeId speaker_role{};
    std::vector<TypeId> conditions;
    std::vector<DialogueOptionDefinition> options;
    std::vector<TypeId> consequences;
    DialogueNodeId automatic_next{};
};
struct ConversationDefinition
{
    ConversationDefinitionId id{};
    std::string canonical_name;
    GameplayTagSet tags;
    std::vector<TypeId> participant_roles;
    DialogueNodeId entry_node{};
    std::vector<DialogueNodeDefinition> nodes;
    Revision revision{};
};
struct ConversationParticipant
{
    TypeId role{};
    GameplayObjectRef object{};
    [[nodiscard]] bool operator==(const ConversationParticipant &) const noexcept = default;
};
struct ConversationContext
{
    GameplayObjectRef area{};
    GameplayTagSet tags;
    GameplayContext gameplay{};
};
struct ConversationSession
{
    ConversationSessionId id{};
    ConversationDefinitionId definition{};
    std::vector<GameplayObjectRef> participants;
    std::vector<ConversationParticipant> participant_bindings;
    GameplayObjectRef current_speaker{};
    DialogueNodeId current_node{};
    ConversationState state = ConversationState::Preparing;
    ConversationContext context{};
    GameplayTimePoint started_at{};
    std::vector<TypeId> resolved_persistent_options;
    Revision revision{};
};
struct DialogueConditionContext
{
    ConversationSessionId session{};
    GameplayObjectRef actor{};
    GameplayObjectRef listener{};
    ConversationContext context{};
};
struct DialogueConditionResult
{
    DialogueConditionState state = DialogueConditionState::Unknown;
    Revision dependencies_revision{};
};
struct DialogueConsequenceExecution
{
    DialogueConsequenceExecutionId id{};
    ConversationSessionId session{};
    TypeId consequence{};
    DialogueConsequenceState state = DialogueConsequenceState::Pending;
    DialogueNodeId source_node{};
    DialogueOptionId source_option{};
    GameplayContext context{};
    Revision revision{};
};
struct DialogueChange
{
    std::uint64_t sequence = 0;
    DialogueChangeKind kind = DialogueChangeKind::ConversationStarted;
    ConversationSessionId session{};
    DialogueNodeId node{};
    DialogueOptionId option{};
    DialogueConsequenceExecutionId consequence_execution{};
    TypeId consequence{};
    DialogueConsequenceState consequence_state = DialogueConsequenceState::Pending;
    TypeId reason{};
    GameplayContext context{};
    Revision revision{};
};
struct DialogueSnapshot
{
    std::vector<ConversationSession> sessions;
    std::vector<DialogueConsequenceExecution> consequences;
    MonotonicIdGenerator<GameplayObjectId>::Snapshot session_ids{};
    MonotonicIdGenerator<GameplayObjectId>::Snapshot consequence_ids{};
    Revision revision{};
};
struct DialogueDiagnostics
{
    std::uint64_t definitions = 0;
    std::uint64_t active_sessions = 0;
    std::uint64_t options_selected = 0;
    std::uint64_t interruptions = 0;
    std::uint64_t consequences = 0;
};

class IDialogueConditionResolver
{
  public:
    virtual ~IDialogueConditionResolver() = default;
    [[nodiscard]] virtual DialogueConditionResult Evaluate(const DialogueConditionDefinition &,
                                                           const DialogueConditionContext &) const = 0;
};
class IDialogueConsequenceHandler
{
  public:
    virtual ~IDialogueConsequenceHandler() = default;
    [[nodiscard]] virtual DialogueConsequenceState Execute(const DialogueConsequenceDefinition &,
                                                           const DialogueConsequenceExecution &,
                                                           const ConversationSession &) const = 0;
};

class DialogueService
{
  public:
    [[nodiscard]] static constexpr GameplayDomainId Domain() noexcept
    {
        return GameplayDomainId::FromString("framework.dialogue");
    }
    [[nodiscard]] foundation::Result<void> RegisterCondition(DialogueConditionDefinition definition);
    [[nodiscard]] foundation::Result<void> RegisterConsequence(DialogueConsequenceDefinition definition);
    [[nodiscard]] foundation::Result<ConversationDefinitionId> RegisterConversation(ConversationDefinition definition);
    [[nodiscard]] foundation::Result<void> RegisterConditionResolver(DialogueConditionTypeId type,
                                                                     const IDialogueConditionResolver &resolver);
    [[nodiscard]] foundation::Result<void> RegisterConsequenceHandler(DialogueConsequenceTypeId type,
                                                                      const IDialogueConsequenceHandler &handler);
    [[nodiscard]] foundation::Result<void> FreezeDefinitions();
    [[nodiscard]] foundation::Result<ConversationSessionId> StartConversation(
        ConversationDefinitionId definition, std::vector<GameplayObjectRef> participants,
        ConversationContext context = {});
    [[nodiscard]] foundation::Result<ConversationSessionId> StartConversation(
        ConversationDefinitionId definition, std::vector<ConversationParticipant> participants,
        ConversationContext context = {});
    [[nodiscard]] foundation::Result<void> SelectOption(ConversationSessionId session, DialogueOptionId option,
                                                        GameplayObjectRef actor, GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> Advance(ConversationSessionId session, GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> Interrupt(ConversationSessionId session, TypeId reason,
                                                     GameplayContext context = {});
    [[nodiscard]] std::vector<DialogueOptionDefinition> GetAvailableOptions(ConversationSessionId session,
                                                                            GameplayObjectRef actor) const;
    [[nodiscard]] std::optional<ConversationSession> GetSession(ConversationSessionId id) const noexcept;
    [[nodiscard]] const ConversationDefinition *GetDefinition(ConversationDefinitionId id) const noexcept;
    [[nodiscard]] std::vector<DialogueConsequenceExecutionId> ExecutePendingConsequences(std::size_t budget = 128);
    [[nodiscard]] std::vector<DialogueChange> ChangesSince(std::uint64_t sequence) const;
    [[nodiscard]] DialogueSnapshot CaptureSnapshot() const;
    [[nodiscard]] foundation::Result<void> RestoreSnapshot(DialogueSnapshot snapshot);
    [[nodiscard]] DialogueDiagnostics GetDiagnostics() const noexcept;

  private:
    void Bump() noexcept
    {
        ++revision_.value;
    }
    void Record(DialogueChange c);
    [[nodiscard]] const DialogueNodeDefinition *FindNode(const ConversationDefinition &d,
                                                         DialogueNodeId id) const noexcept;
    [[nodiscard]] bool ConditionsPass(const std::vector<TypeId> &ids, const DialogueConditionContext &ctx) const;
    [[nodiscard]] foundation::Result<void> EnterNode(ConversationSession &session, DialogueNodeId node,
                                                     GameplayContext context,
                                                     std::optional<std::vector<DialogueConsequenceExecution>> prepared_consequences = std::nullopt);
    [[nodiscard]] foundation::Result<std::vector<DialogueConsequenceExecution>> PrepareConsequences(
        ConversationSessionId session, const std::vector<TypeId> &ids, DialogueNodeId source_node,
        DialogueOptionId source_option, GameplayContext context);
    void CommitPreparedConsequences(std::vector<DialogueConsequenceExecution> prepared);
    [[nodiscard]] GameplayObjectRef ResolveRole(const ConversationSession &session, TypeId role) const noexcept;
    [[nodiscard]] DialogueConditionContext MakeConditionContext(const ConversationSession &session,
                                                                GameplayObjectRef actor) const noexcept;
    void MaybeCleanupTerminalSession(ConversationSessionId id);
    bool frozen_ = false;
    Revision revision_{};
    std::unordered_map<ConversationDefinitionId, ConversationDefinition, IdHash> definitions_;
    std::unordered_map<TypeId, DialogueConditionDefinition> conditions_;
    std::unordered_map<TypeId, DialogueConsequenceDefinition> consequence_defs_;
    std::unordered_map<DialogueConditionTypeId, const IDialogueConditionResolver *, IdHash> condition_resolvers_;
    std::unordered_map<DialogueConsequenceTypeId, const IDialogueConsequenceHandler *, IdHash> consequence_handlers_;
    std::unordered_map<ConversationSessionId, ConversationSession, IdHash> sessions_;
    std::unordered_map<DialogueConsequenceExecutionId, DialogueConsequenceExecution, IdHash> consequences_;
    MonotonicIdGenerator<GameplayObjectId> session_ids_{0x3600}, consequence_ids_{0x3601};
    std::vector<DialogueChange> changes_;
    std::uint64_t next_change_sequence_ = 1;
    DialogueDiagnostics diagnostics_{};
};
} // namespace epidemic::gameplay::dialogue
