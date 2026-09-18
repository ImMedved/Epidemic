from __future__ import annotations

import argparse
import json
import re
import sys
from collections import Counter
from dataclasses import dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
OUTPUT = ROOT / "docs" / "freeze" / "module_dossiers.md"
REVIEWS = ROOT / "docs" / "freeze" / "module_dossier_reviews.json"
DOSSIER_REVIEWS: dict[str, dict[str, dict[str, object]]] = {}
LAYERS = ("EngineBase", "EngineRuntime", "EngineFramework")

MUTATION_VERBS = {
    "acknowledge", "activate", "add", "advance", "allocate", "apply", "attach", "bind",
    "bootstrap", "cancel", "capture", "claim", "clear", "close", "commit", "complete",
    "configure", "connect", "consume", "create", "deallocate", "defer", "dematerialize",
    "demote", "destroy", "detach", "disable", "disconnect", "dispatch", "drain", "emit",
    "enable", "enqueue", "equip", "execute", "expire", "freeze", "initialize", "insert",
    "join", "load", "mark", "materialize", "merge", "move", "open", "pause", "post",
    "process", "publish", "pump", "push", "queue", "reconcile", "register", "release",
    "remove", "request", "reserve", "reset", "resize", "restore", "resume", "rollback",
    "run", "save", "schedule", "seal", "set", "shutdown", "skip", "split", "stage",
    "start", "stop", "submit", "subscribe", "tick", "transfer", "unload", "unregister",
    "update", "upsert", "wait", "write",
}

READ_PREFIXES = {
    "can", "contains", "count", "current", "describe", "diagnostics", "find", "get", "has",
    "is", "latest", "list", "lookup", "make", "peek", "query", "read", "resolve", "size",
    "snapshot", "tryget", "validate", "worker",
}

FIELD_NAMES = (
    "Responsibility",
    "Public headers and types",
    "Dependency list",
    "External ports/callbacks/providers/backends",
    "Authoritative state",
    "Derived/cache/index state",
    "ID spaces, generations, revisions and cursors",
    "State machines",
    "Persistent and transient state",
    "Snapshot/restore contract",
    "Threading contract",
    "Public mutation API",
    "Read/query API for invariants",
    "Local invariants",
    "Hard limits, budgets and complexity bounds",
)

RESPONSIBILITIES = {
    "EngineBase/Foundation": "Defines dependency-free errors, results, typed IDs/handles, paths, hashes, time/frame values and validation primitives.",
    "EngineBase/Memory": "Owns allocator abstractions, allocation tracking, tags, budgets and frame-allocation lifetime.",
    "EngineBase/Diagnostics": "Provides logging, counters, profiling scopes and thread diagnostic context without owning gameplay state.",
    "EngineBase/Core": "Coordinates application/module lifecycle, services, frame phases, events, worker tasks and main-thread dispatch.",
    "EngineBase/Platform": "Abstracts windows, native events, clocks, exit requests and dynamic libraries behind neutral contracts.",
    "EngineBase/Input": "Builds immutable per-frame keyboard and mouse snapshots from platform input events.",
    "EngineBase/RHI": "Defines backend-neutral rendering device, context, swap-chain and frame lifecycle contracts, including a null backend.",
    "EngineBase/RHI_D3D11": "Implements the RHI contracts with D3D11/DXGI and owns the corresponding COM resources.",
    "EngineBase/Support": "Composes EngineBase service bundles and frame wiring for headless, platform, input and graphics configurations.",
    "EngineRuntime/RuntimeFoundation": "Defines Runtime IDs, handles, budgets, operation states, checked time arithmetic and spatial value types.",
    "EngineRuntime/Time": "Owns runtime clock advancement, pause/scale/skip behavior, calendar conversion and scheduled time events.",
    "EngineRuntime/Serialization": "Owns archive values, serializer registration, schema migrations and serialization execution contracts.",
    "EngineRuntime/Resources": "Owns resource identities, dependency graph, loader registry, load state and resource lifetime bookkeeping.",
    "EngineRuntime/Assets": "Owns asset catalog records, revisions, metadata lookup and asset-to-resource resolution.",
    "EngineRuntime/Streaming": "Owns streaming requests, priorities, residency transitions, cancellation and bounded processing.",
    "EngineRuntime/Scene": "Owns scene nodes, hierarchy, transforms, visibility and scene queries independent of rendering.",
    "EngineRuntime/World": "Owns neutral world objects, regions, chunks, placement, residency, persistence tier and materialization state.",
    "EngineRuntime/Simulation": "Owns fixed-step simulation scheduling, systems, pause/step state and per-frame simulation budgets.",
    "EngineRuntime/Physics": "Owns physics bodies, shapes, scene queries, stepping and backend-facing physical state.",
    "EngineRuntime/Navigation": "Owns navigation data, agents, path requests/results and bounded navigation processing.",
    "EngineRuntime/Animation": "Owns animation assets/instances, playback state, parameters and evaluation results.",
    "EngineRuntime/Audio": "Owns audio assets/voices, playback state, listener state and backend-facing audio commands.",
    "EngineRuntime/Environment": "Owns runtime environmental state such as time-of-day/weather inputs and their observable snapshot.",
    "EngineRuntime/Renderer": "Owns renderer resources, views, submissions and frame/presentation coordination over RHI.",
    "EngineRuntime/Persistence": "Owns persistence stores, transactions/checkpoints and serialization-backed save/load orchestration.",
    "EngineRuntime/Support": "Composes the Runtime majors into a service bundle and controls their startup, tick and shutdown order.",
    "EngineFramework/BaseInfrastructure/Foundation": "Defines gameplay IDs, revisions, cursors, journals and shared validation/value contracts.",
    "EngineFramework/BaseInfrastructure/SupportRandom": "Provides deterministic seeded random streams with snapshot/restore continuity.",
    "EngineFramework/BaseInfrastructure/Queries": "Defines and dispatches gameplay query contracts without owning queried domain state.",
    "EngineFramework/BaseInfrastructure/Facts": "Owns gameplay facts/events, subscriptions and revisioned change observation.",
    "EngineFramework/BaseInfrastructure/Time": "Owns gameplay clocks, schedules, time events and deterministic calendar-facing progression.",
    "EngineFramework/RuntimeBoundary/RuntimeBridge": "Maps selected gameplay world concepts to approved Runtime world/physics/environment/navigation contracts.",
    "EngineFramework/GameplayWorldStateOwners/Entities": "Owns gameplay entity records, identity, traits/components and entity change history.",
    "EngineFramework/GameplayWorldStateOwners/Materials": "Owns material definitions, instances/properties and material change history.",
    "EngineFramework/GameplayWorldStateOwners/Conditions": "Owns condition definitions, evaluations and revisioned condition state.",
    "EngineFramework/GameplayWorldStateOwners/Effects": "Owns effect definitions, execution/deferred state and effect handlers.",
    "EngineFramework/GameplayWorldStateOwners/Interaction": "Owns interaction definitions, active interactions and execution state.",
    "EngineFramework/GameplayWorldStateOwners/ItemsInventory": "Owns items, containers, inventories, reservations, bindings and item change history.",
    "EngineFramework/GameplayWorldStateOwners/Equipment": "Owns equipment slots/loadouts and external binding metadata.",
    "EngineFramework/GameplayWorldStateOwners/Ownership": "Owns gameplay ownership relations, transfers and ownership history.",
    "EngineFramework/GameplayWorldStateOwners/Loot": "Owns loot definitions/instances, claims and deterministic loot outcomes.",
    "EngineFramework/GameplayWorldStateOwners/Economy": "Owns accounts, reservations, offers, trades, debts and economic contracts.",
    "EngineFramework/GameplayWorldStateOwners/Processes": "Owns process definitions/executions, reservations and process lifecycle.",
    "EngineFramework/GameplayWorldStateOwners/ResourcesProduction": "Owns resource stockpiles, production definitions/jobs and associated reservations.",
    "EngineFramework/GameplayWorldStateOwners/RolesJobs": "Owns role/job definitions, assignments and role/job lifecycle state.",
    "EngineFramework/GameplayWorldStateOwners/NeedsLife": "Owns needs, life-state progression and need-related change history.",
    "EngineFramework/GameplayWorldStateOwners/Population": "Owns population groups/members and aggregate population state.",
    "EngineFramework/GameplayWorldStateOwners/Society": "Owns social groups, memberships, relations and society-level state.",
    "EngineFramework/GameplayWorldStateOwners/Crime": "Owns crimes, legal records, warrants/sentences and legal history.",
    "EngineFramework/GameplayWorldStateOwners/Perception": "Owns retained observations/perception state and perception queries.",
    "EngineFramework/GameplayWorldStateOwners/Knowledge": "Owns memories, knowledge relations, confidence and knowledge change history.",
    "EngineFramework/GameplayWorldStateOwners/AI": "Owns AI agents, blackboards, intents, scheduling and decision state.",
    "EngineFramework/GameplayWorldStateOwners/Abilities": "Owns ability definitions/instances, executions, reservations and cooldowns.",
    "EngineFramework/GameplayWorldStateOwners/Progression": "Owns progression tracks, grants/unlocks and progression history.",
    "EngineFramework/GameplayWorldStateOwners/Combat": "Owns combatants, encounters/actions, reservations and combat state.",
    "EngineFramework/GameplayWorldStateOwners/Encounters": "Owns encounter definitions/instances, participants and encounter lifecycle.",
    "EngineFramework/GameplayWorldStateOwners/Narrative": "Owns narrative threads, objectives, choices, deliveries and journal state.",
    "EngineFramework/GameplayWorldStateOwners/Dialogue": "Owns dialogue definitions, sessions, choices and dialogue history.",
    "EngineFramework/GameplayWorldStateOwners/World": "Owns semantic gameplay-world records and gameplay-facing world state.",
    "EngineFramework/GameplayWorldStateOwners/Environment": "Owns gameplay environmental state and semantic environment changes.",
    "EngineFramework/GameplayWorldStateOwners/Traversal": "Owns traversal capabilities, grants and traversal-related state.",
    "EngineFramework/GameplayWorldStateOwners/NavigationSemantics": "Owns semantic navigation graph/areas/links above Runtime navigation.",
    "EngineFramework/GameplayWorldStateOwners/Construction": "Owns construction plans, sites, reservations and construction lifecycle.",
    "EngineFramework/GameplayWorldStateOwners/Simulation": "Owns gameplay simulation participants, schedules and semantic simulation state.",
    "EngineFramework/GameplayWorldStateOwners/SaveGame": "Owns save participants, ordered capture/restore and save-game transaction state.",
    "EngineFramework/IntegrationLayer/Integration": "Adapts gameplay queries/facts/time/save contracts and the approved RuntimeTime boundary.",
    "EngineFramework/IntegrationLayer/GameplayIntegration": "Coordinates core gameplay owners into multi-owner gameplay operations.",
    "EngineFramework/IntegrationLayer/ExtendedGameplayIntegration": "Coordinates extended item, equipment, economy and related gameplay workflows.",
    "EngineFramework/IntegrationLayer/InteractionEffectsIntegration": "Coordinates interaction completion with effect execution.",
    "EngineFramework/IntegrationLayer/InteractionTimeIntegration": "Coordinates interactions/effects with gameplay schedules and time events.",
    "EngineFramework/IntegrationLayer/NarrativeIntegration": "Coordinates narrative, dialogue and related gameplay state transitions.",
    "EngineFramework/IntegrationLayer/PerceptionKnowledgeAIIntegration": "Coordinates the perception-to-knowledge-to-AI causal chain.",
    "EngineFramework/IntegrationLayer/PopulationSimulationIntegration": "Coordinates population/needs state with gameplay simulation ticks.",
    "EngineFramework/IntegrationLayer/ProcessResourceSimulationIntegration": "Coordinates processes and resource production with simulation progression.",
    "EngineFramework/IntegrationLayer/SocialLegalIntegration": "Coordinates ownership, society and crime/legal workflows.",
    "EngineFramework/IntegrationLayer/StateIntegration": "Coordinates entity/material/condition/effect state propagation.",
    "EngineFramework/IntegrationLayer/TraversalNavigationConstructionIntegration": "Coordinates traversal, semantic navigation and construction workflows.",
    "EngineFramework/IntegrationLayer/WorldIntegration": "Coordinates semantic gameplay world/environment state with RuntimeBridge materialization.",
}


@dataclass(frozen=True)
class Module:
    key: str
    root: Path
    target: str
    headers: tuple[Path, ...]
    dependencies: tuple[str, ...]


def clean_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    return re.sub(r"//.*", "", text)


def words(name: str) -> list[str]:
    return [part.lower() for part in re.findall(r"[A-Z]?[a-z]+|[A-Z]+(?![a-z])|\d+", name)]


def first_word(name: str) -> str:
    parts = words(name.replace("~", ""))
    return parts[0] if parts else name.lower()


def discover_modules() -> list[Module]:
    modules: list[Module] = []
    for layer in LAYERS:
        for cmake_path in sorted((ROOT / layer).rglob("CMakeLists.txt")):
            relative = cmake_path.relative_to(ROOT).as_posix()
            if any(part in {"Apps", "Tests", "tests", "DevelopmentInfrastructure"} for part in cmake_path.parts):
                continue
            cmake = cmake_path.read_text(encoding="utf-8", errors="ignore")
            match = re.search(r"add_library\s*\(\s*([A-Za-z_][A-Za-z0-9_]*)", cmake)
            if not match:
                continue
            target = match.group(1)
            root = cmake_path.parent
            key = root.relative_to(ROOT).as_posix()
            headers = tuple(
                sorted(
                    path for path in root.rglob("*")
                    if path.is_file() and path.suffix in {".h", ".hpp"} and "include" in path.parts
                )
            )
            dependencies = parse_dependencies(cmake, target)
            modules.append(Module(key, root, target, headers, dependencies))
    return sorted(modules, key=lambda item: (LAYERS.index(item.key.split("/", 1)[0]), item.key))


def parse_dependencies(cmake: str, target: str) -> tuple[str, ...]:
    dependencies: list[str] = []
    pattern = re.compile(rf"target_link_libraries\s*\(\s*{re.escape(target)}\b(.*?)\)", re.S)
    ignored = {"PUBLIC", "PRIVATE", "INTERFACE", "debug", "optimized", "general"}
    for body in pattern.findall(cmake):
        for token in re.findall(r"(?<![$<{])\b[A-Za-z_][A-Za-z0-9_:.+-]*\b", body):
            if token not in ignored and token != target and token not in dependencies:
                dependencies.append(token)
    return tuple(dependencies)


def public_text(module: Module) -> str:
    return "\n".join(path.read_text(encoding="utf-8", errors="ignore") for path in module.headers)


def all_module_text(module: Module) -> str:
    paths = list(module.headers)
    source = module.root / "src"
    if source.exists():
        paths.extend(path for path in source.rglob("*") if path.suffix in {".h", ".hpp", ".cpp"})
    return "\n".join(path.read_text(encoding="utf-8", errors="ignore") for path in paths)


def extract_types(text: str) -> list[str]:
    found: list[str] = []
    class_stack: list[dict[str, object]] = []
    for raw_line in text.splitlines():
        line = raw_line.split("//", 1)[0]
        stripped = line.strip()
        if class_stack and stripped in {"public:", "protected:", "private:"}:
            class_stack[-1]["public"] = stripped == "public:"
            continue

        visible = not class_stack or (bool(class_stack[-1]["visible"]) and bool(class_stack[-1]["public"]))
        enum_match = re.match(r"\s*enum\s+class\s+([A-Za-z_]\w*)", line)
        if enum_match and visible:
            found.append(enum_match.group(1))

        class_match = re.match(r"\s*(?:template\s*<.*>\s*)?(class|struct)\s+(.+)", line)
        if class_match and not stripped.startswith(("class enum", "struct enum")):
            tokens = re.findall(r"\b[A-Za-z_]\w*\b", class_match.group(2))
            class_name = next(
                (token for token in tokens if len(token) > 1 and token[0].isupper()
                 and not token.isupper() and token not in {"Final"}),
                "",
            )
            if class_name and visible:
                found.append(class_name)
            if class_name and not (";" in line and "{" not in line):
                entry = {
                    "brace_depth": line.count("{") - line.count("}"),
                    "public": class_match.group(1) == "struct",
                    "visible": visible,
                }
                class_stack.append(entry)
                if int(entry["brace_depth"]) <= 0 and "};" in line:
                    class_stack.pop()
            continue

        alias_match = re.match(r"\s*using\s+([A-Za-z_]\w*)\s*=", line)
        if alias_match and visible:
            found.append(alias_match.group(1))

        if class_stack:
            class_stack[-1]["brace_depth"] = (
                int(class_stack[-1]["brace_depth"]) + line.count("{") - line.count("}")
            )
            while class_stack and int(class_stack[-1]["brace_depth"]) <= 0 and "};" in line:
                class_stack.pop()
    found = [name for name in found if len(name) > 1 and name[0].isupper()]
    return sorted(set(found))


def extract_all_types(text: str) -> list[str]:
    cleaned = clean_comments(text)
    found: set[str] = set()
    patterns = (
        r"\benum\s+class\s+([A-Za-z_]\w*)",
        r"\b(?:class|struct)\s+(?:alignas\s*\([^)]*\)\s*)?(?:[A-Z][A-Z0-9_]*\s+)?([A-Za-z_]\w*)",
        r"\busing\s+([A-Za-z_]\w*)\s*=",
    )
    for pattern in patterns:
        found.update(
            name for name in re.findall(pattern, cleaned)
            if len(name) > 1 and name[0].isupper() and name not in {"Final"}
        )
    return sorted(found)


def extract_state_members(text: str) -> list[str]:
    return sorted(set(re.findall(r"\b([a-z][A-Za-z0-9_]*)_\s*(?=[;={\[,])", clean_comments(text))))


def extract_public_callable_names(text: str) -> tuple[list[str], list[str]]:
    lines = text.splitlines()
    class_stack: list[dict[str, object]] = []
    pending: list[str] = []
    inline_body_depth = 0
    candidates: list[tuple[str, str, bool, bool]] = []

    for raw_line in lines:
        line = raw_line.split("//", 1)[0]
        stripped = line.strip()
        if inline_body_depth > 0:
            inline_body_depth += line.count("{") - line.count("}")
            continue

        class_match = re.match(r"\s*(class|struct)\s+(.+)", line)
        if (class_match and not stripped.startswith(("class enum", "struct enum"))
                and not (";" in line and "{" not in line)):
            tokens = re.findall(r"\b[A-Za-z_]\w*\b", class_match.group(2))
            class_name = next(
                (token for token in tokens if len(token) > 1 and token[0].isupper()
                 and not token.isupper() and token not in {"Final"}),
                "",
            )
            if class_name:
                visible = not class_stack or (bool(class_stack[-1]["visible"]) and bool(class_stack[-1]["public"]))
                class_stack.append(
                    {
                        "name": class_name,
                        "brace_depth": line.count("{") - line.count("}"),
                        "public": class_match.group(1) == "struct",
                        "visible": visible,
                    }
                )
                pending = []
                continue

        started_inline_depth = 0
        if class_stack:
            top = class_stack[-1]
            if stripped in {"public:", "protected:", "private:"}:
                top["public"] = stripped == "public:"
            elif bool(top["visible"]) and bool(top["public"]) and (pending or ("(" in line and not stripped.startswith(("#", "using ", "typedef ", "friend ")))):
                pending.append(stripped)
                joined = " ".join(pending)
                if ";" in joined or "{" in joined:
                    pending = []
                    declaration = joined.split("{", 1)[0].split(";", 1)[0].strip()
                    names = re.findall(r"(~?[A-Za-z_]\w*|operator\s*[^\s(]+)\s*\(", declaration)
                    if names:
                        name = names[0]
                        if (
                            name != top["name"]
                            and not name.startswith(("~", "operator"))
                            and not name.endswith(("_", "Impl"))
                        ):
                            candidates.append((
                                str(top["name"]),
                                name,
                                bool(re.search(r"\)\s*const\b", declaration)),
                                bool(re.search(r"\bstatic\b", declaration)),
                            ))
                    if "{" in joined:
                        started_inline_depth = max(0, joined.count("{") - joined.count("}"))
                        inline_body_depth = started_inline_depth

            top["brace_depth"] = (
                int(top["brace_depth"]) + line.count("{") - line.count("}") - started_inline_depth
            )
            while class_stack and int(class_stack[-1]["brace_depth"]) <= 0 and "};" in line:
                class_stack.pop()
        elif pending or ("(" in line and not stripped.startswith(("#", "using ", "typedef ", "friend "))):
            pending.append(stripped)
            joined = " ".join(pending)
            if ";" in joined or "{" in joined:
                pending = []
                declaration = joined.split("{", 1)[0].split(";", 1)[0].strip()
                names = re.findall(r"(~?[A-Za-z_]\w*|operator\s*[^\s(]+)\s*\(", declaration)
                if names:
                    name = names[0]
                    if not name.startswith(("~", "operator")) and not name.endswith(("_", "Impl")):
                        candidates.append(("free", name, False, True))
                if "{" in joined:
                    inline_body_depth = max(0, joined.count("{") - joined.count("}"))

    counts = Counter(candidates)
    mutations: list[str] = []
    reads: list[str] = []
    for (scope, name, is_const, is_static), count in sorted(counts.items()):
        prefix = first_word(name)
        rendered = f"{scope}::{name}" + (f" x{count}" if count > 1 else "")
        if is_const or prefix in READ_PREFIXES or (is_static and prefix not in MUTATION_VERBS):
            reads.append(rendered)
        elif prefix in MUTATION_VERBS or scope != "free":
            mutations.append(rendered)
    return mutations, reads


def matching_types(types: list[str], terms: tuple[str, ...]) -> list[str]:
    lowered_terms = {term.lower() for term in terms}
    return [name for name in types if lowered_terms.intersection(words(name))]


def suffix_types(types: list[str], suffixes: tuple[str, ...]) -> list[str]:
    lowered = tuple(suffix.lower() for suffix in suffixes)
    return [name for name in types if name.lower().endswith(lowered)]


def render_list(items: list[str] | tuple[str, ...], empty: str) -> str:
    return ", ".join(f"`{item}`" for item in items) if items else empty


def header_map(module: Module, types: list[str]) -> str:
    chunks: list[str] = []
    for header in module.headers:
        rel = header.relative_to(ROOT).as_posix()
        local_types = extract_types(header.read_text(encoding="utf-8", errors="ignore"))
        chunks.append(f"`{rel}` ({render_list(local_types, 'no named public types')})")
    return "; ".join(chunks) if chunks else "No public header under an `include` directory."


def threading_contract(module: Module, public: str, complete: str) -> str:
    comments = []
    for line in public.splitlines():
        if "thread" in line.lower() and "//" in line:
            comment = line.split("//", 1)[1].strip()
            if comment and comment not in comments:
                comments.append(comment)
    synchronized = bool(re.search(r"std::(?:shared_)?mutex|std::atomic|std::jthread", complete))
    if comments:
        return "Explicit public/source statements: " + " | ".join(comments[:3])
    if synchronized:
        return "Implementation contains synchronization primitives, but no complete public threading guarantee is documented; only the synchronized operations may be assumed thread-safe."
    if "/IntegrationLayer/" in module.key:
        return "Externally serialized coordinator; callbacks must not re-enter unless the called owner explicitly permits it. No independent worker ownership is exposed."
    if "/GameplayWorldStateOwners/" in module.key:
        return "Externally serialized state owner (normally the gameplay/simulation thread); no concurrent mutation guarantee is exposed."
    return "No explicit public concurrency guarantee; callers must externally serialize mutation until Goal 6 qualifies a stronger contract."


def authoritative_state(module: Module, types: list[str], members: list[str]) -> str:
    state_types = matching_types(types, ("Record", "Definition", "Descriptor", "Snapshot", "State", "Registry", "Service", "Runtime", "Store", "System", "Manager", "Coordinator"))
    member_text = " Storage members discovered in declarations/implementations: " + render_list(members, "none") + "."
    if "/IntegrationLayer/" in module.key:
        return "No duplicate domain ownership; coordinator-local workflow/snapshot state only. Domain state remains authoritative in linked owner modules. Relevant types: " + render_list(state_types, "none exposed") + "." + member_text
    if module.key in {
        "EngineBase/Foundation", "EngineRuntime/RuntimeFoundation",
        "EngineFramework/BaseInfrastructure/Foundation", "EngineFramework/BaseInfrastructure/Queries",
    }:
        return "No long-lived authoritative store; the module defines value types and contracts." + member_text
    return "Module-private storage represented by its service/runtime/record types: " + render_list(state_types, "concrete storage is private and no state record type is exposed") + "." + member_text


def local_invariants(module: Module) -> str:
    if "/IntegrationLayer/" in module.key:
        return "The module must not become a second authoritative owner; a cross-owner failure must leave every participant at its observable pre-state."
    if "/GameplayWorldStateOwners/" in module.key or module.key.endswith("/Facts") or module.key.endswith("/Time"):
        return "Primary records, typed IDs, revisions, indexes, generators and journal cursors must agree; rejected mutations preserve observable pre-state."
    if module.key.startswith("EngineRuntime/"):
        return "Typed IDs/handles, lifecycle state, backend state and revisions remain consistent; a failed operation publishes no partial state."
    return "Public lifecycle/value constraints remain valid after success and failure; the module does not acquire state owned by an upper layer."


def render_module(module: Module, exact_callables: list[object]) -> list[str]:
    public = public_text(module)
    complete = all_module_text(module)
    types = extract_types(public)
    all_types = extract_all_types(complete)
    members = extract_state_members(complete)
    mutations = [
        f"{item.id}:{item.scope}::{item.name} ({item.header}:{item.line})"
        for item in exact_callables if item.classification in {"MUTATOR", "LIFECYCLE", "CALLBACK"}
    ]
    reads = [
        f"{item.id}:{item.scope}::{item.name} ({item.header}:{item.line})"
        for item in exact_callables if item.classification in {"QUERY", "FACTORY"}
    ]
    unclassified = [item for item in exact_callables if item.classification == "UNCLASSIFIED"]
    ports = matching_types(types, ("Callback", "Provider", "Backend", "Port", "Sink", "Source", "Loader", "Resolver", "Executor", "Factory", "Authority", "Participant"))
    ports.extend(name for name in types if name.startswith("I") and len(name) > 1 and name[1].isupper() and name not in ports)
    identity = suffix_types(all_types, ("Id", "IdTag", "Handle", "Generation", "Revision", "Cursor", "Sequence", "Token"))
    machines = suffix_types(all_types, ("State", "Status", "Phase", "Mode", "Tier", "Kind", "Lifecycle"))
    snapshots = matching_types(all_types, ("Snapshot", "Checkpoint", "Archive", "Save"))
    derived = matching_types(all_types, ("Query", "Index", "Cache", "Batch", "Journal", "View", "Diagnostics", "Stats"))
    derived_members = [
        name for name in members
        if any(term in name.lower() for term in ("index", "cache", "journal", "cursor", "lookup", "diagnostic", "pending", "queue", "sorted", "revision", "next_"))
    ]
    limits = sorted(set(re.findall(r"\b(?:max_[a-zA-Z0-9_]+|[a-zA-Z0-9_]*budget[a-zA-Z0-9_]*|[a-zA-Z0-9_]*capacity[a-zA-Z0-9_]*)\b", complete, flags=re.I)))
    snapshot_apis = [
        item for item in mutations + reads
        if any(token in item for token in ("Snapshot", "Restore", "Save", "LoadSnapshot", "Checkpoint"))
    ]
    transient = matching_types(all_types, ("Request", "Pending", "Queue", "Callback", "Backend", "Transaction", "Session"))

    fields = {
        "Responsibility": RESPONSIBILITIES[module.key],
        "Public headers and types": header_map(module, types),
        "Dependency list": render_list(module.dependencies, "No direct CMake link dependency."),
        "External ports/callbacks/providers/backends": render_list(sorted(set(ports)), "No named public external port/callback/provider/backend type."),
        "Authoritative state": authoritative_state(module, all_types, members),
        "Derived/cache/index state": "Types: " + render_list(derived, "none") + ". Storage members: " + render_list(derived_members, "none") + ".",
        "ID spaces, generations, revisions and cursors": render_list(identity, "No module-specific public ID/generation/revision/cursor type."),
        "State machines": render_list(machines, "No named public state/status/phase/mode type."),
        "Persistent and transient state": "Snapshot/checkpoint/archive carriers: " + render_list(snapshots, "none exposed") + ". Transient request/session/queue carriers: " + render_list(transient, "none exposed") + ". Durability is claimed only where the snapshot/restore contract says so.",
        "Snapshot/restore contract": render_list(snapshot_apis, "No module-level public snapshot/restore callable discovered; state is either non-persistent or persistence must be supplied by a composition owner."),
        "Threading contract": threading_contract(module, public, complete),
        "Public mutation API": render_list(mutations, "No mutation/lifecycle/callback public callable discovered.") +
            (f" Exact inventory retains {len(unclassified)} `UNCLASSIFIED` callable(s) for module audit." if unclassified else ""),
        "Read/query API for invariants": render_list(reads, "No query/factory public callable discovered; verification must use returned values or linked owner queries."),
        "Local invariants": local_invariants(module),
        "Hard limits, budgets and complexity bounds": ("Limit/budget/capacity identifiers found in the module: " + render_list(limits, "none") + ". " +
            "No stronger asymptotic complexity guarantee is inferred; exact bounds must be proven by the module-local audit."),
    }

    lines = [f"### {module.key}", "", f"<!-- module: {module.key} -->", "", f"Target: `{module.target}`.", ""]
    for name in FIELD_NAMES:
        review = DOSSIER_REVIEWS[module.key][name]
        value = review.get("correction") or fields[name]
        lines.extend([f"- **{name}:** [{review['status']}] {value}", ""])
    return lines


def normalized_reviews(modules: list[Module], previous: object = None) -> dict[str, object]:
    old_modules = previous.get("modules", {}) if isinstance(previous, dict) else {}
    records: dict[str, object] = {}
    for module in modules:
        old_fields = old_modules.get(module.key, {}) if isinstance(old_modules, dict) else {}
        fields: dict[str, object] = {}
        for name in FIELD_NAMES:
            old = old_fields.get(name, {}) if isinstance(old_fields, dict) else {}
            default = "REVIEWED" if name == "Responsibility" else "DISCOVERED"
            status = old.get("status", default) if isinstance(old, dict) else default
            record: dict[str, object] = {"status": status if status in {"DISCOVERED", "REVIEWED"} else default}
            if isinstance(old, dict) and isinstance(old.get("correction"), str) and old["correction"].strip():
                record["correction"] = old["correction"]
            fields[name] = record
        records[module.key] = fields
    return {"schema_version": 1, "modules": records}


def validate_reviews(modules: list[Module], data: object) -> list[str]:
    expected = normalized_reviews(modules, data)
    if not isinstance(data, dict) or data.get("schema_version") != 1:
        return ["module dossier review registry schema mismatch"]
    if data != expected:
        return ["module dossier review registry is incomplete, stale or malformed"]
    return []


def render(modules: list[Module]) -> str:
    from public_api_inventory import apply_classification_overrides, load_classification_overrides, scan_modules

    exact_by_module: dict[str, list[object]] = {module.key: [] for module in modules}
    exact_callables, override_errors = apply_classification_overrides(scan_modules(modules), load_classification_overrides())
    if override_errors:
        raise ValueError("; ".join(override_errors))
    for callable_entry in exact_callables:
        exact_by_module[callable_entry.module].append(callable_entry)
    header_count = sum(len(module.headers) for module in modules)
    lines = [
        "# Epidemic Engine production module dossiers",
        "",
        "Generated by `docs/freeze/module_dossiers.py` from the current production CMake targets and public headers.",
        "",
        "Status: `BASELINE_INVENTORIED`. This dossier records the current contract surface; it does not promote a module to `LOCAL_READY`. An explicitly missing contract is retained as audit debt for Goals 2-6 rather than guessed.",
        "",
        f"Inventory: `{len(modules)}` production modules and `{header_count}` public headers.",
        "",
        "Callable names are a conservative lexical coverage index. Repeated names are shown with occurrence counts and must be resolved to exact overload contracts during the local audit.",
        "",
    ]
    current_layer = ""
    for module in modules:
        layer = module.key.split("/", 1)[0]
        if layer != current_layer:
            lines.extend([f"## {layer}", ""])
            current_layer = layer
        lines.extend(render_module(module, exact_by_module[module.key]))
    return "\n".join(lines).rstrip() + "\n"


def validate_inventory(modules: list[Module], rendered: str) -> list[str]:
    errors: list[str] = []
    counts = Counter(module.key.split("/", 1)[0] for module in modules)
    expected_counts = {"EngineBase": 9, "EngineRuntime": 17, "EngineFramework": 52}
    if len(modules) != 78 or dict(counts) != expected_counts:
        errors.append(f"module inventory mismatch: total={len(modules)}, layers={dict(counts)}")
    header_count = sum(len(module.headers) for module in modules)
    if header_count != 232:
        errors.append(f"public header inventory mismatch: expected 232, got {header_count}")
    missing_responsibilities = [module.key for module in modules if module.key not in RESPONSIBILITIES]
    extra_responsibilities = sorted(set(RESPONSIBILITIES) - {module.key for module in modules})
    if missing_responsibilities:
        errors.append("missing responsibility entries: " + ", ".join(missing_responsibilities))
    if extra_responsibilities:
        errors.append("stale responsibility entries: " + ", ".join(extra_responsibilities))
    for module in modules:
        marker = f"<!-- module: {module.key} -->"
        if rendered.count(marker) != 1:
            errors.append(f"module marker count for {module.key}: {rendered.count(marker)}")
    for field in FIELD_NAMES:
        count = rendered.count(f"- **{field}:**")
        if count != 78:
            errors.append(f"field count for {field}: expected 78, got {count}")
    return errors


def self_test() -> list[str]:
    modules = discover_modules()
    rendered = render(modules)
    failures: list[str] = []
    if validate_inventory(modules, rendered):
        failures.append("current production inventory was rejected")

    marker = f"<!-- module: {modules[0].key} -->"
    field = f"- **{FIELD_NAMES[0]}:**"
    cases = {
        "missing production module": (modules[:-1], rendered),
        "missing module marker": (modules, rendered.replace(marker, "", 1)),
        "duplicate module marker": (modules, rendered.replace(marker, marker + "\n" + marker, 1)),
        "missing dossier field": (modules, rendered.replace(field, "- **BROKEN_FIELD:**", 1)),
    }
    for name, (candidate_modules, candidate_rendered) in cases.items():
        if not validate_inventory(candidate_modules, candidate_rendered):
            failures.append(f"negative case was accepted: {name}")

    template_fixture = """template <typename T> class Result
{
  public:
    using PublicAlias = T;
  private:
    class Storage {};
    using Variant = T;
};
template <typename Tag> struct BasicId {};
"""
    template_types = extract_types(template_fixture)
    if "Result" not in template_types or "BasicId" not in template_types:
        failures.append("same-line template class/struct public types were not discovered")
    if "Storage" in template_types or "Variant" in template_types:
        failures.append("private nested template implementation types leaked into public type inventory")
    return failures


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate or verify the 78 production-module freeze dossiers.")
    parser.add_argument("--check", action="store_true", help="Verify that the committed dossier matches the current tree.")
    parser.add_argument("--self-test", action="store_true", help="Run negative inventory and format fixtures.")
    args = parser.parse_args()

    modules = discover_modules()
    global DOSSIER_REVIEWS
    existing_reviews = json.loads(REVIEWS.read_text(encoding="utf-8")) if REVIEWS.exists() else None
    review_document = normalized_reviews(modules, existing_reviews)
    if not args.check and not args.self_test:
        REVIEWS.write_text(json.dumps(review_document, indent=2, sort_keys=True) + "\n", encoding="utf-8", newline="\n")
    review_errors = validate_reviews(modules, existing_reviews if existing_reviews is not None else review_document)
    if args.check and review_errors:
        print("\n".join(f"ERROR: {error}" for error in review_errors), file=sys.stderr)
        return 1
    DOSSIER_REVIEWS = review_document["modules"]

    if args.self_test:
        errors = self_test()
        if errors:
            print("\n".join(f"ERROR: {error}" for error in errors), file=sys.stderr)
            return 1
        print("PASS: dossier validator accepted the production inventory and rejected all 4 malformed inventories.")
        return 0

    rendered = render(modules)
    errors = validate_inventory(modules, rendered)
    if errors:
        for error in errors:
            print(f"ERROR: {error}", file=sys.stderr)
        return 1

    if args.check:
        if not OUTPUT.exists():
            print(f"ERROR: missing {OUTPUT.relative_to(ROOT)}", file=sys.stderr)
            return 1
        current = OUTPUT.read_text(encoding="utf-8")
        if current != rendered:
            print("ERROR: module dossier is stale; run docs/freeze/module_dossiers.py", file=sys.stderr)
            return 1
        print("PASS: 78/78 modules, 232/232 public headers, 15/15 fields per module; dossier is current.")
        return 0

    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT.write_text(rendered, encoding="utf-8", newline="\n")
    print(f"Wrote {OUTPUT.relative_to(ROOT)}: 78 modules, 232 headers, 15 fields per module.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
