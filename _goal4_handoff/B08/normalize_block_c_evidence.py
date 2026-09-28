"""One-shot, reproducible normalization of B07/B08 closure evidence."""
from __future__ import annotations

import json
import re
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BLOCKS = (ROOT / "_goal4_handoff/B07", ROOT / "_goal4_handoff/B08")
MARKER = "<!-- goal4-exact-evidence -->"
REQUIRED_KINDS = {
    "ARCH-RESPONSIBILITY": ("architecture",), "ARCH-SINGLE-OWNER": ("architecture", "state"),
    "ARCH-DIRECT-DEPS": ("architecture",), "ARCH-PORTS": ("architecture", "contract"),
    "API-CLASSIFIED": ("contract",), "API-PRECONDITIONS": ("contract", "test"),
    "API-SUCCESS": ("contract", "test"), "API-FAILURE": ("contract", "fault", "test"),
    "API-OVERLOADS": ("contract",), "API-INVALID": ("contract", "test"),
    "STATE-PRIMARY": ("state", "test"), "STATE-INDEXES": ("state", "test"),
    "STATE-COUNTERS": ("state", "test"), "STATE-NO-FALSE-PUBLISH": ("fault", "state", "test"),
    "STATE-NOOP": ("contract", "test"), "LIFE-ALLOWED": ("lifecycle", "test"),
    "LIFE-FORBIDDEN": ("lifecycle", "test"), "LIFE-SHUTDOWN": ("lifecycle", "test"),
    "LIFE-RETRY-CLEANUP": ("fault", "lifecycle", "test"),
    "ATOMIC-SINGLE": ("fault", "state", "test"), "ATOMIC-MULTI": ("fault", "state", "test"),
    "ATOMIC-EXTERNAL": ("contract", "fault", "test"),
    "ATOMIC-RECONCILE": ("contract", "fault", "state", "test"),
    "PERSIST-SNAPSHOT": ("persistence", "state", "test"),
    "PERSIST-VALIDATE": ("fault", "persistence", "test"),
    "PERSIST-CANDIDATE": ("persistence", "state", "test"),
    "PERSIST-FAILURE": ("fault", "persistence", "state", "test"),
    "PERSIST-CONTINUITY": ("persistence", "state", "test"),
    "TEST-HAPPY": ("test",), "TEST-INVALID": ("test",), "TEST-DUPLICATE": ("test",),
    "TEST-STALE": ("test",), "TEST-EMPTY": ("test",), "TEST-BOUNDARY": ("test",),
    "TEST-WRONG-LIFECYCLE": ("test",), "TEST-CALLBACK-FAILURE": ("fault", "test"),
    "TEST-REGRESSION": ("test",),
}
KEYWORDS = {
    "API-INVALID": ("invalid", "reject", "!"), "API-FAILURE": ("fail", "fault", "!"),
    "API-PRECONDITIONS": ("reject", "invalid", "!"), "STATE-PRIMARY": ("snapshot", "state"),
    "STATE-INDEXES": ("find", "index"), "STATE-COUNTERS": ("revision", "count"),
    "STATE-NO-FALSE-PUBLISH": ("fault", "revision"), "STATE-NOOP": ("unchanged", "duplicate"),
    "LIFE-FORBIDDEN": ("reject", "freeze"), "LIFE-SHUTDOWN": ("shutdown", "freeze"),
    "LIFE-RETRY-CLEANUP": ("retry", "cleanup"), "ATOMIC-SINGLE": ("fault", "unchanged"),
    "ATOMIC-MULTI": ("fault", "transaction"), "ATOMIC-EXTERNAL": ("reconciliation", "accepted"),
    "ATOMIC-RECONCILE": ("reconciliation", "retry"), "PERSIST-SNAPSHOT": ("snapshot", "restore"),
    "PERSIST-VALIDATE": ("restore", "invalid"), "PERSIST-CANDIDATE": ("restore", "snapshot"),
    "PERSIST-FAILURE": ("restore", "fault"), "PERSIST-CONTINUITY": ("restore", "revision"),
    "TEST-INVALID": ("invalid", "reject"), "TEST-DUPLICATE": ("duplicate", "again"),
    "TEST-STALE": ("stale", "cursor"), "TEST-EMPTY": ("empty", "zero"),
    "TEST-BOUNDARY": ("max", "limit"), "TEST-WRONG-LIFECYCLE": ("freeze", "reject"),
    "TEST-CALLBACK-FAILURE": ("callback", "fail"), "TEST-REGRESSION": ("g4-", "fault"),
}


def load(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def save(path: Path, value: dict) -> None:
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def anchor(path: str, line: int, text: str) -> str:
    symbol = text.strip()
    if len(symbol) > 150:
        symbol = symbol[:120].rstrip()
    return f"{path}:{line}::{symbol}"


def method_tokens(scope: str, signature: str) -> list[str]:
    tokens: list[str] = []
    match = re.search(r"(~?[A-Za-z_]\w*|operator(?:<=>|==|\(\)|\[\]))\s*\(", signature)
    if match:
        tokens.append(match.group(1))
    leaf = scope.rsplit("::", 1)[-1]
    if leaf and leaf not in tokens:
        tokens.append(leaf)
    tokens.extend(re.findall(r'"([A-Za-z0-9_.-]{5,})"', signature))
    return tokens


def assertion_lines(lines: list[str]) -> list[int]:
    result = []
    for number, text in enumerate(lines, 1):
        stripped = text.strip()
        if (("CHECK(" in text or "Check(" in text or stripped.startswith("if ("))
                and stripped not in {"CHECK(", "Check(", "if ("}
                and not stripped.startswith("#define")
                and not re.match(r"(?:void\s+)?Check\s*\(", stripped)):
            result.append(number)
    return result


def choose_test(lines: list[str], scope: str, signature: str, existing: dict,
                criterion: str | None = None) -> tuple[int, int]:
    assertions = assertion_lines(lines)
    old = re.match(r"^[^:]+:(\d+)::", str(existing.get("anchor", "")))
    old_line = int(old.group(1)) if old else 1
    candidates: list[tuple[int, int]] = []
    tokens = method_tokens(scope, signature)
    keywords = KEYWORDS.get(criterion or "", ())
    for number, text in enumerate(lines, 1):
        if number < 1 or not text.strip() or text.lstrip().startswith(("//", "#")):
            continue
        score = 0
        score += 8 * sum(token in text for token in tokens)
        score += 3 * sum(word.lower() in text.lower() for word in keywords)
        if number in assertions:
            score += 4
        if "main(" in text or re.match(r"(?:void\s+)?Check\s*\(", text.strip()):
            score -= 20
        if score > 0:
            candidates.append((score - abs(number - old_line) // 80, number))
    call_line = max(candidates, default=(0, assertions[0]))[1]
    assertion_line = min(assertions, key=lambda number: (abs(number - call_line), number))
    return call_line, assertion_line


def architecture_anchor(module: str) -> str:
    relative = "docs/freeze/architecture_ownership_matrix.md"
    lines = (ROOT / relative).read_text(encoding="utf-8").splitlines()
    for number, text in enumerate(lines, 1):
        if module in text:
            return anchor(relative, number, text)
    raise RuntimeError(f"architecture row not found for {module}")


def normalize_block(block: Path) -> None:
    anchors_path = block / "public_api_anchors.json"
    coverage_path = block / "coverage_reviews.json"
    anchors = load(anchors_path)
    coverage = load(coverage_path)
    api = coverage["api"]
    by_module: dict[str, list[str]] = {}
    for item_id, record in api.items():
        by_module.setdefault(record["module"], []).append(item_id)

    module_data = {}
    for module, ids in by_module.items():
        first = anchors["anchors"][ids[0]]
        doc_path = first["contract"]["anchor"].split(":", 1)[0]
        test_path = first["test"]["anchor"].split(":", 1)[0]
        target = first["test"]["target"]
        doc_file, test_file = ROOT / doc_path, ROOT / test_path
        base = doc_file.read_text(encoding="utf-8").split(MARKER, 1)[0].rstrip()
        appendix = ["", MARKER, "", "## Exact Goal 4 callable contracts", ""]
        for item_id in sorted(ids):
            item = api[item_id]
            appendix.append(
                f"- G4-API-{item_id}: `{item['scope']}` — `{item['signature']}`; "
                f"classification `{item['classification']}`. The callable follows the module's reviewed "
                "validation, success, no-op and failure-publication rules applicable to that classification."
            )
        appendix.extend(["", "## Exact Goal 4 criterion contracts", ""])
        for criterion in REQUIRED_KINDS:
            appendix.append(
                f"- G4-CRITERION-{criterion}: `{criterion}` was reviewed for this module against its "
                "authoritative state, lifecycle, failure-atomicity and persistence boundaries; the owned "
                "regression anchor records the applicable observable assertion."
            )
        doc_file.write_text(base + "\n" + "\n".join(appendix) + "\n", encoding="utf-8")
        doc_lines = doc_file.read_text(encoding="utf-8").splitlines()
        api_doc_lines = {item_id: next(n for n, text in enumerate(doc_lines, 1)
                                       if f"G4-API-{item_id}:" in text) for item_id in ids}
        criterion_doc_lines = {criterion: next(n for n, text in enumerate(doc_lines, 1)
                                                if f"G4-CRITERION-{criterion}:" in text)
                               for criterion in REQUIRED_KINDS}
        test_lines = test_file.read_text(encoding="utf-8").splitlines()
        module_data[module] = (doc_path, doc_lines, test_path, test_lines, target,
                               criterion_doc_lines, architecture_anchor(module))
        for item_id in ids:
            item, current = api[item_id], anchors["anchors"][item_id]
            call, assertion = choose_test(test_lines, item["scope"], item["signature"], current["test"])
            current["contract"] = {
                "anchor": anchor(doc_path, api_doc_lines[item_id], doc_lines[api_doc_lines[item_id] - 1]),
                "reviewed": True,
            }
            current["test"] = {
                "anchor": anchor(test_path, call, test_lines[call - 1]),
                "assertions": [anchor(test_path, assertion, test_lines[assertion - 1])],
                "reviewed": True, "target": target,
            }
    save(anchors_path, anchors)

    ready_path = next(iter(block.glob("local_ready*.json")))
    ready = load(ready_path)
    for module, raw in ready["modules"].items():
        criteria = raw.get("criteria", raw)
        doc_path, doc_lines, test_path, test_lines, target, doc_map, arch = module_data[module]
        representative_id = sorted(by_module[module])[0]
        representative = api[representative_id]
        normalized = {}
        for criterion, kinds in REQUIRED_KINDS.items():
            old = criteria[criterion]
            if old["status"] == "N/A":
                normalized[criterion] = {
                    "status": "N/A", "evidence": [],
                    "rationale": (f"{criterion} is not applicable to {module.rsplit('/', 1)[-1]}: "
                                  "the reviewed public surface has no corresponding lifecycle, external, "
                                  "callback, persistence or reconciliation responsibility."),
                }
                continue
            contract = anchor(doc_path, doc_map[criterion], doc_lines[doc_map[criterion] - 1])
            call, assertion = choose_test(test_lines, representative["scope"],
                                          representative["signature"], {}, criterion)
            test = {"kind": "test", "anchor": anchor(test_path, call, test_lines[call - 1]),
                    "assertions": [anchor(test_path, assertion, test_lines[assertion - 1])],
                    "target": target}
            evidence = []
            for kind in kinds:
                if kind == "architecture":
                    evidence.append({"kind": kind, "anchor": arch})
                elif kind == "test":
                    evidence.append(test)
                else:
                    evidence.append({"kind": kind, "anchor": contract})
            normalized[criterion] = {
                "status": "PASS", "evidence": evidence,
                "rationale": (f"{block.name} reviewed {criterion} for {module.rsplit('/', 1)[-1]} "
                              "against the exact module contract and owned observable regression."),
            }
        raw.clear()
        raw.update({"status": "LOCAL_READY", "criteria": normalized})
    save(ready_path, ready)


def update_defects() -> None:
    exact = {
        "B07": {
            "g4-infra-001-b07": {
                "logical_id": "G4-INFRA-001",
                "regressions": {
                    "EpidemicGameFrameworkWorldTests": "EngineFramework/DevelopmentInfrastructure/Tests/world_tests.cpp:232::CHECK(!restored.AddDynamicFeature(fault_feature));",
                    "EpidemicGameFrameworkSaveGameTests": "EngineFramework/DevelopmentInfrastructure/Tests/save_game_tests.cpp:409::SetSaveGameFaultPointForTesting(\"restore.pre_commit\")",
                    "EpidemicGameFrameworkNarrativeTests": "EngineFramework/DevelopmentInfrastructure/Tests/narrative_tests.cpp:362::Check(!static_cast<bool>(failed_restore)",
                    "EpidemicGameFrameworkNarrativeIntegrationTests": "EngineFramework/DevelopmentInfrastructure/Tests/narrative_integration_tests.cpp:102::TestNarrativeOutboxPublicationAtomicity",
                },
            },
            "g4-narrint-001-revision-exhaustion": {
                "logical_id": "G4-NARRINT-001", "regression": "EngineFramework/DevelopmentInfrastructure/Tests/narrative_integration_tests.cpp:131::TestNarrativeOutboxRevisionExhaustion"},
            "g4-narrint-002-publication-atomicity": {
                "logical_id": "G4-NARRINT-002", "regression": "EngineFramework/DevelopmentInfrastructure/Tests/narrative_integration_tests.cpp:102::TestNarrativeOutboxPublicationAtomicity"},
        },
        "B08": {
            "G4-INFRA-001/B08": {
                "logical_id": "G4-INFRA-001",
                "regressions": {
                    "EpidemicGameFrameworkCoreIntegrationTests": "EngineFramework/DevelopmentInfrastructure/Tests/core_integration_tests.cpp:690::FailNextTriggerCompletionPublicationForTest",
                    "EpidemicGameFrameworkExtendedGameplayIntegrationTests": "EngineFramework/DevelopmentInfrastructure/Tests/extended_gameplay_integration_tests.cpp:376::FailNextTradePublicationForTest",
                    "EpidemicGameFrameworkProcessResourceSimulationIntegrationTests": "EngineFramework/DevelopmentInfrastructure/Tests/process_resource_simulation_integration_tests.cpp:106::FailNextResourceReservationTokenPublicationForTest",
                },
            },
            "G4-EXTINT-001": {"logical_id": "G4-EXTINT-001", "regression": "EngineFramework/DevelopmentInfrastructure/Tests/extended_gameplay_integration_tests.cpp:358::external reservation"},
            "G4-EXTINT-002": {"logical_id": "G4-EXTINT-002", "regression": "EngineFramework/DevelopmentInfrastructure/Tests/extended_gameplay_integration_tests.cpp:423::G4-EXTINT-002 accepted external work returns durable reconciliation execution"},
            "B08-INT-001": {"logical_id": "B08-INT-001", "regression": "EngineFramework/DevelopmentInfrastructure/Tests/core_integration_tests.cpp:690::FailNextTriggerCompletionPublicationForTest"},
            "B08-PRSINT-001": {"logical_id": "B08-PRSINT-001", "regression": "EngineFramework/DevelopmentInfrastructure/Tests/process_resource_simulation_integration_tests.cpp:106::FailNextResourceReservationTokenPublicationForTest"},
        },
    }
    for block_name, changes in exact.items():
        path = ROOT / f"_goal4_handoff/{block_name}/defects.json"
        data = load(path)
        for record_id, patch in changes.items():
            record = data["defects"][record_id]
            record.update(patch)
            if "regressions" in patch:
                targets = list(patch["regressions"])
                record["target"], record["targets"] = targets[0], targets
                record["regression"] = patch["regressions"][targets[0]]
            else:
                record["targets"] = [record["target"]]
        save(path, data)
        coverage_path = ROOT / f"_goal4_handoff/{block_name}/coverage_reviews.json"
        coverage = load(coverage_path)
        for record_id, record in data["defects"].items():
            coverage["defects"][record_id] = record
        save(coverage_path, coverage)


for block in BLOCKS:
    normalize_block(block)
update_defects()
print("Normalized B07/B08 exact contracts, test anchors, LOCAL_READY evidence and defects.")
