from __future__ import annotations

import re
from dataclasses import dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "docs" / "milestone2_mutation_api_matrix.md"

HEADER_ROOTS = ("EngineBase", "EngineRuntime", "EngineFramework")
SOURCE_SUFFIXES = (".cpp", ".h", ".hpp")

MUTATION_VERBS = (
    "acknowledge",
    "activate",
    "add",
    "advance",
    "apply",
    "bind",
    "bootstrap",
    "cancel",
    "claim",
    "clear",
    "close",
    "commit",
    "complete",
    "create",
    "defer",
    "delete",
    "dematerialize",
    "demote",
    "destroy",
    "disable",
    "drain",
    "enable",
    "equip",
    "execute",
    "expire",
    "freeze",
    "initialize",
    "insert",
    "join",
    "load",
    "mark",
    "materialize",
    "merge",
    "open",
    "post",
    "process",
    "publish",
    "pump",
    "queue",
    "register",
    "release",
    "remove",
    "request",
    "reserve",
    "resize",
    "restore",
    "rollback",
    "run",
    "save",
    "schedule",
    "seal",
    "set",
    "shutdown",
    "split",
    "stage",
    "start",
    "submit",
    "tick",
    "transfer",
    "unequip",
    "unload",
    "unregister",
    "update",
    "upsert",
    "wait",
)

READ_PREFIXES = (
    "can",
    "contains",
    "count",
    "find",
    "get",
    "has",
    "is",
    "latest",
    "list",
    "lookup",
    "make",
    "query",
    "resolve",
    "size",
    "tryget",
    "validate",
)

STATE_BY_VERB = {
    "register": "definition registry, secondary lookup indexes, freeze/seal lifecycle",
    "unregister": "definition registry, reverse indexes, dependent runtime records",
    "freeze": "registration lifecycle flag and read-only definition indexes",
    "seal": "registration lifecycle flag and read-only definition indexes",
    "create": "primary records, ID generator, secondary indexes, revisions/journal when present",
    "destroy": "primary records, free/generation state, secondary indexes, revisions/journal when present",
    "remove": "primary records, secondary indexes, revisions/journal when present",
    "delete": "primary records, tombstones/indexes, revisions/journal when present",
    "set": "record payload, secondary indexes when keyed fields change, revisions/journal when present",
    "update": "record payload, derived indexes/queues, revisions/journal when present",
    "restore": "candidate snapshot, primary records, indexes, generators, revisions, journal/cursor",
    "commit": "staged state, durable/live state, external participants, revisions/journal",
    "rollback": "staged state, reservations, external participants",
    "cancel": "pending queues, reservations, external participants, revisions/journal when present",
    "schedule": "pending queues, time indexes, ID generator, revisions/journal when present",
    "publish": "event/fact buffers, subscriber queues, journal/cursor when present",
    "submit": "pending queues, staged proposals, budgets, revisions/journal when present",
    "process": "pending queues, checkpoints/cursors, external participants",
    "execute": "handler/provider state, staged effects, callbacks, revisions/journal when present",
    "tick": "time-step state, pending queues, event buffers, revisions/journal when present",
    "load": "resource/store records, dependency indexes, backend handles",
    "unload": "resource/store records, dependency indexes, backend handles",
    "request": "request records, queues, ID/handle generator, result cache",
    "release": "handle ownership, result cache, external/backend resources",
}

FAILURE_BY_VERB = {
    "register": "duplicate/invalid definition, sealed registry, allocation",
    "freeze": "invalid lifecycle, allocation while finalizing indexes",
    "seal": "invalid lifecycle, allocation while finalizing indexes",
    "create": "validation, allocation, stale dependency, overflow",
    "destroy": "stale ID/handle, validation, allocation in journal/index updates",
    "remove": "stale ID/handle, validation, allocation in journal/index updates",
    "delete": "stale ID/handle, validation, allocation in tombstone/index updates",
    "set": "validation, stale ID/handle, allocation, callback/provider failure",
    "update": "validation, stale ID/handle, allocation, callback/provider failure",
    "restore": "invalid snapshot, stale generator scope, allocation, callback/participant failure",
    "commit": "base revision mismatch, allocation, backend/provider commit failure",
    "rollback": "callback/provider rollback failure, allocation in diagnostics",
    "cancel": "stale ID/handle, allocation, provider cancel failure",
    "schedule": "invalid time/budget, overflow, allocation",
    "publish": "allocation, subscriber callback failure/exception",
    "submit": "invalid request, budget limit, allocation, executor failure",
    "process": "callback/provider failure, allocation, stale cursor/checkpoint",
    "execute": "callback/provider failure, allocation, stale target",
    "tick": "overflow, budget limit, allocation, callback/provider failure",
    "load": "backend failure, invalid payload, allocation",
    "unload": "stale handle, backend failure, allocation",
    "request": "invalid request, stale handle, allocation, budget limit",
    "release": "stale handle, allocation in bookkeeping/diagnostics",
}

PART2_RESTORE_TESTS = {
    "GameplayWorldStateOwners/Effects": "EngineFramework/DevelopmentInfrastructure/Tests/effects_tests.cpp",
    "GameplayWorldStateOwners/Encounters": "EngineFramework/DevelopmentInfrastructure/Tests/encounters_tests.cpp",
    "GameplayWorldStateOwners/Entities": "EngineFramework/DevelopmentInfrastructure/Tests/entities_tests.cpp",
    "GameplayWorldStateOwners/Environment": "EngineFramework/DevelopmentInfrastructure/Tests/environment_gameplay_tests.cpp",
    "GameplayWorldStateOwners/Equipment": "EngineFramework/DevelopmentInfrastructure/Tests/equipment_tests.cpp",
    "GameplayWorldStateOwners/Interaction": "EngineFramework/DevelopmentInfrastructure/Tests/interaction_gameplay_tests.cpp",
    "GameplayWorldStateOwners/ItemsInventory": "EngineFramework/DevelopmentInfrastructure/Tests/items_inventory_tests.cpp",
    "GameplayWorldStateOwners/Knowledge": "EngineFramework/DevelopmentInfrastructure/Tests/knowledge_memory_tests.cpp",
    "GameplayWorldStateOwners/Loot": "EngineFramework/DevelopmentInfrastructure/Tests/loot_tests.cpp",
    "GameplayWorldStateOwners/Materials": "EngineFramework/DevelopmentInfrastructure/Tests/materials_tests.cpp",
    "GameplayWorldStateOwners/Narrative": "EngineFramework/DevelopmentInfrastructure/Tests/narrative_tests.cpp",
    "GameplayWorldStateOwners/NavigationSemantics": "EngineFramework/DevelopmentInfrastructure/Tests/navigation_semantics_tests.cpp",
    "GameplayWorldStateOwners/NeedsLife": "EngineFramework/DevelopmentInfrastructure/Tests/needs_life_tests.cpp",
}

EFFECTS_FREEZE_EVIDENCE = {
    "AcknowledgeDeferredBySchedule": (
        "EngineFramework/GameplayWorldStateOwners/Effects/src/effects.cpp:801",
        "EngineFramework/DevelopmentInfrastructure/Tests/effects_tests.cpp:662",
    ),
    "BindDeferredSchedule": (
        "EngineFramework/GameplayWorldStateOwners/Effects/src/effects.cpp:690",
        "EngineFramework/DevelopmentInfrastructure/Tests/effects_tests.cpp:638",
    ),
    "ClearDeferredSchedule": (
        "EngineFramework/GameplayWorldStateOwners/Effects/src/effects.cpp:822",
        "EngineFramework/DevelopmentInfrastructure/Tests/effects_tests.cpp:650",
    ),
    "Defer": (
        "EngineFramework/GameplayWorldStateOwners/Effects/src/effects.cpp:650",
        "EngineFramework/DevelopmentInfrastructure/Tests/effects_tests.cpp:629",
    ),
    "Execute": (
        "EngineFramework/GameplayWorldStateOwners/Effects/src/effects.cpp:429",
        "EngineFramework/DevelopmentInfrastructure/Tests/effects_tests.cpp:621",
    ),
    "Freeze": ("INLINE_HEADER", "EngineFramework/DevelopmentInfrastructure/Tests/effects_tests.cpp:453"),
    "RegisterDefinition": (
        "EngineFramework/GameplayWorldStateOwners/Effects/src/effects.cpp:138",
        "EngineFramework/DevelopmentInfrastructure/Tests/effects_tests.cpp:402",
    ),
    "RegisterHandler": (
        "EngineFramework/GameplayWorldStateOwners/Effects/src/effects.cpp:106",
        "EngineFramework/DevelopmentInfrastructure/Tests/effects_tests.cpp:429",
    ),
    "RestoreSnapshot": (
        "EngineFramework/GameplayWorldStateOwners/Effects/src/effects.cpp:971",
        "EngineFramework/DevelopmentInfrastructure/Tests/effects_tests.cpp:709",
    ),
    "SetTargetStateProvider": (
        "INLINE_HEADER",
        "EngineFramework/DevelopmentInfrastructure/Tests/effects_tests.cpp:453",
    ),
}


@dataclass(frozen=True)
class ApiRow:
    layer: str
    area: str
    scope: str
    api: str
    verb: str
    kind: str
    header: str
    implementation: str
    state: str
    failures: str
    postcondition: str
    tests: str
    evidence: str


def iter_headers() -> list[Path]:
    headers: list[Path] = []
    for root_name in HEADER_ROOTS:
        root = ROOT / root_name
        for path in root.rglob("*.h"):
            if "\\include\\" in str(path) or "/include/" in str(path):
                headers.append(path)
    return sorted(headers)


def strip_line_comment(line: str) -> str:
    return line.split("//", 1)[0]


def normalize_space(value: str) -> str:
    return re.sub(r"\s+", " ", value).strip()


def layer_for(path: Path) -> str:
    return path.relative_to(ROOT).parts[0]


def area_for(path: Path) -> str:
    parts = path.relative_to(ROOT).parts
    if parts[0] == "EngineFramework":
        if len(parts) > 2:
            return "/".join(parts[1:3])
    if len(parts) > 1:
        return parts[1]
    return parts[0]


def method_verb(name: str) -> str:
    cleaned = name.replace("~", "")
    words = re.findall(r"[A-Z]?[a-z]+|[A-Z]+(?![a-z])|\d+", cleaned)
    if words:
        return words[0].lower()
    return cleaned.lower()


def is_mutation_name(name: str) -> bool:
    lowered = name.lower().replace("_", "")
    if lowered.startswith(READ_PREFIXES):
        return False
    verb = method_verb(name)
    return verb in MUTATION_VERBS


def kind_for(verb: str) -> str:
    if verb in {"register", "unregister", "freeze", "seal", "bootstrap", "initialize", "shutdown"}:
        return "Definition/Lifecycle"
    if verb in {"restore", "load", "save"}:
        return "Restore/Persistence"
    if verb in {"commit", "rollback", "stage", "reserve", "release"}:
        return "Commit/External"
    if verb in {"tick", "process", "execute", "publish", "submit", "schedule", "cancel"}:
        return "Runtime/Queue"
    return "Runtime/State"


def source_candidates(header: Path) -> list[Path]:
    rel_parts = header.relative_to(ROOT).parts
    candidates: list[Path] = []
    if "include" in rel_parts:
        include_index = rel_parts.index("include")
        module_root = ROOT.joinpath(*rel_parts[:include_index])
        src_dir = module_root / "src"
        if src_dir.exists():
            candidates.extend(src_dir.rglob("*.cpp"))
            candidates.extend(src_dir.rglob("*.h"))
    layer_root = ROOT / rel_parts[0]
    candidates.extend(layer_root.rglob("*.cpp"))
    seen: set[Path] = set()
    unique: list[Path] = []
    for candidate in candidates:
        if candidate.suffix in SOURCE_SUFFIXES and candidate not in seen:
            seen.add(candidate)
            unique.append(candidate)
    return unique


def anchor(path: Path, line: int | None = None) -> str:
    rel = path.relative_to(ROOT).as_posix()
    if line is None:
        return rel
    return f"{rel}:{line}"


def find_implementation(header: Path, scope: str, method: str) -> str:
    exact_pattern = re.compile(rf"\b{re.escape(scope)}::\s*{re.escape(method)}\s*\(")
    fallback_pattern = re.compile(rf"\b{re.escape(method)}\s*\(")
    fallback_hits: list[str] = []
    for source in source_candidates(header):
        try:
            lines = source.read_text(encoding="utf-8", errors="ignore").splitlines()
        except OSError:
            continue
        for index, line in enumerate(lines, start=1):
            if exact_pattern.search(line):
                return anchor(source, index)
            if fallback_pattern.search(line):
                fallback_hits.append(anchor(source, index))
    if scope.startswith("I"):
        return "ABSTRACT_CONTRACT"
    if len(fallback_hits) == 1:
        return fallback_hits[0]
    if len(fallback_hits) > 1:
        return f"MULTIPLE_IMPL_CANDIDATES({len(fallback_hits)})"
    return "IMPLEMENTATION_LOOKUP_REQUIRED"


def find_tests(area: str, method: str) -> str:
    test_roots = [ROOT / "EngineBase" / "Tests", ROOT / "EngineRuntime", ROOT / "EngineFramework" / "DevelopmentInfrastructure" / "Tests"]
    area_token = area.split("/")[-1].lower()
    method_pattern = re.compile(rf"\b{re.escape(method)}\s*\(")
    hits: list[str] = []
    for test_root in test_roots:
        if not test_root.exists():
            continue
        for test_file in test_root.rglob("*tests.cpp"):
            name_score = area_token in test_file.name.lower()
            try:
                text = test_file.read_text(encoding="utf-8", errors="ignore")
            except OSError:
                continue
            if name_score or method_pattern.search(text):
                hits.append(anchor(test_file))
    return ", ".join(sorted(set(hits))[:3]) if hits else "MISSING_TEST_ANCHOR"


def extract_rows(header: Path) -> list[ApiRow]:
    rows: list[ApiRow] = []
    lines = header.read_text(encoding="utf-8", errors="ignore").splitlines()
    class_stack: list[dict[str, object]] = []
    pending_decl: list[str] = []
    pending_start = 0
    inline_body_depth = 0

    for line_number, raw_line in enumerate(lines, start=1):
        line = strip_line_comment(raw_line)
        if inline_body_depth > 0:
            inline_body_depth += line.count("{") - line.count("}")
            continue

        class_match = re.match(r"\s*(class|struct)\s+(.+)", line)
        if class_match:
            default_public = class_match.group(1) == "struct"
            class_tokens = re.findall(r"\b[A-Za-z_]\w*\b", class_match.group(2))
            class_name = ""
            for token in class_tokens:
                if token == "final" or token == "alignas" or token.endswith("_API") or token.isupper():
                    continue
                class_name = token
                break
            if not class_name:
                continue
            class_stack.append(
                {
                    "name": class_name,
                    "brace_depth": line.count("{") - line.count("}"),
                    "public": default_public,
                }
            )
            continue

        if class_stack:
            top = class_stack[-1]
            stripped = line.strip()
            if stripped in {"public:", "protected:", "private:"}:
                top["public"] = stripped == "public:"
                continue

            if top["public"] and (
                pending_decl or ("(" in line and not stripped.startswith(("#", "using ", "typedef ", "friend ")))
            ):
                if not pending_decl:
                    pending_start = line_number
                pending_decl.append(stripped)
                joined = normalize_space(" ".join(pending_decl))
                if ";" in joined or "{" in joined:
                    pending_decl = []
                    declaration = joined.split("{", 1)[0].split(";", 1)[0].strip()
                    if "{" in joined:
                        inline_body_depth = max(0, joined.count("{") - joined.count("}"))
                    if re.search(r"\)\s*const(?:\s|$)", declaration) and "Execute" not in declaration:
                        continue
                    name_match = re.search(r"(?:~?\w+|operator\s*[^\s(]+)\s*\(", declaration)
                    if not name_match:
                        continue
                    all_names = re.findall(r"(~?\w+|operator\s*[^\s(]+)\s*\(", declaration)
                    if not all_names:
                        continue
                    name = all_names[0]
                    if (
                        name == top["name"]
                        or name.startswith("operator")
                        or name.endswith("_")
                        or name.endswith("Impl")
                        or (name.endswith("Type") and not name.startswith("Register"))
                        or not is_mutation_name(name)
                    ):
                        continue
                    verb = method_verb(name)
                    implementation = (
                        "INLINE_HEADER" if "{" in joined else find_implementation(header, str(top["name"]), name)
                    )
                    tests = find_tests(area_for(header), name)
                    evidence = "UNPROVEN"
                    if name == "RestoreSnapshot" and area_for(header) in PART2_RESTORE_TESTS:
                        tests = PART2_RESTORE_TESTS[area_for(header)]
                        evidence = "PASS"
                    if area_for(header) == "GameplayWorldStateOwners/Effects" and str(top["name"]) == "EffectService":
                        effects_evidence = EFFECTS_FREEZE_EVIDENCE.get(name)
                        if effects_evidence is not None:
                            implementation, tests = effects_evidence
                            evidence = "PASS"
                    if area_for(header) == "GameplayWorldStateOwners/Effects" and str(top["name"]) == "IEffectHandler" and name == "Commit":
                        implementation = "ABSTRACT_CONTRACT"
                        tests = "EngineFramework/DevelopmentInfrastructure/Tests/effects_tests.cpp:621"
                        evidence = "PASS"
                    rows.append(
                        ApiRow(
                            layer=layer_for(header),
                            area=area_for(header),
                            scope=str(top["name"]),
                            api=name,
                            verb=verb,
                            kind=kind_for(verb),
                            header=anchor(header, pending_start),
                            implementation=implementation,
                            state=STATE_BY_VERB.get(verb, "authoritative state, indexes/generators/revisions when present"),
                            failures=FAILURE_BY_VERB.get(verb, "validation, allocation, stale lifecycle/handle, callback failure when present"),
                            postcondition=(
                                "On failure: live pre-state, indexes, generators, revisions, journals/cursors, "
                                "queues and external callback counts remain unchanged. On success: mutation is observable atomically."
                            ),
                            tests=tests,
                            evidence=evidence,
                        )
                    )

            top["brace_depth"] = int(top["brace_depth"]) + line.count("{") - line.count("}")
            while class_stack and int(class_stack[-1]["brace_depth"]) <= 0 and "};" in line:
                class_stack.pop()

    return rows


def write_matrix(rows: list[ApiRow]) -> None:
    by_layer: dict[str, list[ApiRow]] = {}
    for row in rows:
        by_layer.setdefault(row.layer, []).append(row)

    lines: list[str] = []
    lines.append("# Milestone 2 Mutation API Matrix")
    lines.append("")
    lines.append("Generated from public headers by `docs/milestone2_mutation_api_matrix.py`.")
    lines.append("This file is the freeze snapshot for roadmap item 1: every row is a public mutation-like API entrypoint discovered in the current `dev` working tree.")
    lines.append("")
    lines.append("Evidence status is intentionally conservative: `UNPROVEN` means the API is in the frozen matrix but still needs or already relies on a dedicated bounded allocation/pre-state test before it may be promoted to `PASS`.")
    lines.append("")
    lines.append(f"Total mutation-like public API rows: `{len(rows)}`.")
    lines.append("")

    for layer in sorted(by_layer):
        layer_rows = sorted(by_layer[layer], key=lambda row: (row.area, row.scope, row.api, row.header))
        lines.append(f"## {layer}")
        lines.append("")
        lines.append("| Area | Scope | API | Kind | Header | Implementation | State touched | Failure boundaries | Required postcondition | Test anchor | Evidence |")
        lines.append("|---|---|---|---|---|---|---|---|---|---|---:|")
        for row in layer_rows:
            values = [
                row.area,
                row.scope,
                f"`{row.api}`",
                row.kind,
                f"`{row.header}`",
                f"`{row.implementation}`",
                row.state,
                row.failures,
                row.postcondition,
                f"`{row.tests}`",
                f"`{row.evidence}`",
            ]
            escaped = [value.replace("|", "\\|") for value in values]
            lines.append("| " + " | ".join(escaped) + " |")
        lines.append("")

    OUTPUT.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> None:
    rows: list[ApiRow] = []
    for header in iter_headers():
        rows.extend(extract_rows(header))

    unique: dict[tuple[str, str, str, str], ApiRow] = {}
    for row in rows:
        unique[(row.layer, row.area, row.scope, row.api)] = row

    write_matrix(sorted(unique.values(), key=lambda row: (row.layer, row.area, row.scope, row.api, row.header)))


if __name__ == "__main__":
    main()
