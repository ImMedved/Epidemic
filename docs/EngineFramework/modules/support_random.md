# SupportRandom

Status: B01 Goal 4 review. Local module contract only.

Responsibility: deterministic pseudo-random streams, seed derivation, bounded integer/real sampling and stream snapshot/replay. Public contract: `deterministic_random.h`.

Authoritative state is the explicit stream seed/state/counter. Streams do not share hidden mutable state and do not consult wall clock after construction. Derived sampled values are not stored as authoritative state.

Dependencies: Framework Foundation value/error contracts. No Runtime backend or external callback boundary.

Mutation/query semantics: advancing a stream changes only that stream's deterministic state. Invalid bounds/ranges are rejected. Equal seed plus equal stream state produces the same sequence. Bounded sampling uses rejection/width logic required by the contract instead of observable modulo bias.

Persistence boundary: snapshot/restore covers future sequence state. Whole-engine deterministic replay is Goal 5.

Limits and threading: fixed-width PRNG state and requested numeric domains define the bounds. Instances are synchronous and caller-serialized; separate stream objects are independent.

Local invariants and tests: `random_support_tests.cpp` covers known-vector determinism, seed derivation, independent streams, boundary ranges, invalid ranges, exhaustion/boundary behavior and snapshot/replay continuation.

## Callable-specific closure contracts

Each row is the reviewed contract for one public callable; the ID is the stable inventory key used by the Goal 4 evidence ledger.

- `02b4c82924d23ddb` — `epidemic::gameplay::random::RandomSequence` / `[[nodiscard]] std::uint64_t UniformUnchecked(std::uint64_t exclusive_max)noexcept;`: validates its stated preconditions; success publishes the owned mutation atomically, while rejection preserves authoritative state and observable continuity.
- `056bb19c856b5393` — `epidemic::gameplay::random::RandomSequence` / `template <typename TContainer> void ShuffleUnchecked(TContainer&values)noexcept(noexcept(ShuffleUnchecked(std::span{values.data(),values.size()})))`: validates its stated preconditions; success publishes the owned mutation atomically, while rejection preserves authoritative state and observable continuity.
- `1472122e7c96b5f5` — `epidemic::gameplay::random::RandomStream` / `[[nodiscard]] constexpr bool IsValid()const noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `1905ac6939029a19` — `epidemic::gameplay::random` / `[[nodiscard]] std::uint64_t StableMix(std::uint64_t value)noexcept;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `2a0d7f9769babc91` — `epidemic::gameplay::random::RandomSeed` / `[[nodiscard]] constexpr bool operator==(const RandomSeed&)const noexcept=default;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `36bae784939cdd81` — `epidemic::gameplay::random::RandomSequence` / `[[nodiscard]] bool IsExhausted()const noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `375cf6b76b7acd5a` — `epidemic::gameplay::random::RandomSequence` / `[[nodiscard]] std::uint64_t Sequence()const noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `3e87fe202fda6b05` — `epidemic::gameplay::random::RandomSequence` / `[[nodiscard]] std::optional<std::uint64_t> TryUniform(std::uint64_t exclusive_max)noexcept;`: validates its stated preconditions; success publishes the owned mutation atomically, while rejection preserves authoritative state and observable continuity.
- `416dde8e3852a351` — `epidemic::gameplay::random::RandomSequence` / `[[nodiscard]] bool TryRestoreSnapshot(RandomSequenceSnapshot snapshot)noexcept;`: validates its stated preconditions; success publishes the owned mutation atomically, while rejection preserves authoritative state and observable continuity.
- `455168d14e02b563` — `epidemic::gameplay::random::RandomSequence` / `[[nodiscard]] std::optional<double> TryUniform01()noexcept;`: validates its stated preconditions; success publishes the owned mutation atomically, while rejection preserves authoritative state and observable continuity.
- `4dbd3f4a7fbbd43d` — `epidemic::gameplay::random` / `[[nodiscard]] RandomSeed DeriveSeed(RandomSeed root,RandomStream stream,std::uint64_t salt=0)noexcept;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `6e73254db6c5a974` — `epidemic::gameplay::random::RandomSequence` / `RandomSequence(RandomSeed seed,RandomStream stream,std::uint64_t sequence=0);`: constructs or derives a value in its documented domain without exposing partially initialized state.
- `78ed549eb9497450` — `epidemic::gameplay::random::RandomSequence` / `[[nodiscard]] std::optional<std::uint64_t> TryUniformRange(std::uint64_t minimum,std::uint64_t maximum_exclusive)noexcept;`: validates its stated preconditions; success publishes the owned mutation atomically, while rejection preserves authoritative state and observable continuity.
- `7e443a802d1374dd` — `epidemic::gameplay::random::RandomSequence` / `[[nodiscard]] double Uniform01Unchecked()noexcept;`: validates its stated preconditions; success publishes the owned mutation atomically, while rejection preserves authoritative state and observable continuity.
- `86f728a3693a186f` — `epidemic::gameplay::random::RandomSequence` / `[[nodiscard]] std::optional<double> TryUniformReal(double minimum,double maximum)noexcept;`: validates its stated preconditions; success publishes the owned mutation atomically, while rejection preserves authoritative state and observable continuity.
- `916c72b0cad1eba6` — `epidemic::gameplay::random::RandomSequence` / `template <typename T> void ShuffleUnchecked(std::span<T> values)noexcept(std::is_nothrow_swappable_v<T>)`: validates its stated preconditions; success publishes the owned mutation atomically, while rejection preserves authoritative state and observable continuity.
- `9b7cc964b81940f7` — `epidemic::gameplay::random::RandomSequence` / `[[nodiscard]] double UniformRealUnchecked(double minimum,double maximum)noexcept;`: validates its stated preconditions; success publishes the owned mutation atomically, while rejection preserves authoritative state and observable continuity.
- `9f4c2b14671080a2` — `epidemic::gameplay::random::RandomSequence` / `[[nodiscard]] static std::optional<RandomSequence> TryFromSnapshot(RandomSequenceSnapshot snapshot)noexcept;`: constructs or derives a value in its documented domain without exposing partially initialized state.
- `a52b86acc248089c` — `epidemic::gameplay::random::RandomSequence` / `[[nodiscard]] std::uint64_t NextU64Unchecked()noexcept;`: validates its stated preconditions; success publishes the owned mutation atomically, while rejection preserves authoritative state and observable continuity.
- `c0a897d4a7951a44` — `epidemic::gameplay::random::RandomSequence` / `[[nodiscard]] bool RollMicroUnchecked(std::uint32_t chance_micro)noexcept;`: validates its stated preconditions; success publishes the owned mutation atomically, while rejection preserves authoritative state and observable continuity.
- `c858b90a5d26a9a4` — `epidemic::gameplay::random::RandomSequence` / `[[nodiscard]] std::optional<std::size_t> WeightedIndex(std::span<const std::uint64_t> weights)noexcept;`: validates its stated preconditions; success publishes the owned mutation atomically, while rejection preserves authoritative state and observable continuity.
- `cbf30bf0a6aeb789` — `epidemic::gameplay::random::RandomSequence` / `[[nodiscard]] RandomSequenceSnapshot CaptureSnapshot()const noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `d10cdb45868e92b7` — `epidemic::gameplay::random::RandomSequence` / `[[nodiscard]] static constexpr bool IsValidSnapshot(const RandomSequenceSnapshot&snapshot)noexcept`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `e235830a524fd9e5` — `epidemic::gameplay::random::RandomStream` / `[[nodiscard]] static constexpr RandomStream FromString(std::string_view name)noexcept`: constructs or derives a value in its documented domain without exposing partially initialized state.
- `e66e9f68d3f54e01` — `epidemic::gameplay::random::RandomSequence` / `[[nodiscard]] std::optional<bool> TryRollMicro(std::uint32_t chance_micro)noexcept;`: validates its stated preconditions; success publishes the owned mutation atomically, while rejection preserves authoritative state and observable continuity.
- `ed6ade41c581562b` — `epidemic::gameplay::random::RandomSequence` / `[[nodiscard]] std::uint64_t UniformRangeUnchecked(std::uint64_t minimum,std::uint64_t maximum_exclusive)noexcept;`: validates its stated preconditions; success publishes the owned mutation atomically, while rejection preserves authoritative state and observable continuity.
- `fa53d45d29816ed2` — `epidemic::gameplay::random::RandomStream` / `[[nodiscard]] constexpr bool operator==(const RandomStream&)const noexcept=default;`: observes value or service state without publishing a mutation and returns a result consistent with the module invariants.
- `fb96a123a6aa3c13` — `epidemic::gameplay::random::RandomSequence` / `[[nodiscard]] std::optional<std::uint64_t> TryNextU64()noexcept;`: validates its stated preconditions; success publishes the owned mutation atomically, while rejection preserves authoritative state and observable continuity.
