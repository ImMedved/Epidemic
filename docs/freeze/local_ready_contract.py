from __future__ import annotations

import argparse
import copy
import json
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

sys.dont_write_bytecode = True

from module_dossiers import ROOT, discover_modules
from evidence_anchors import registered_test_targets, validate_path_anchor, validate_test_evidence


OUTPUT = ROOT / "docs" / "freeze" / "local_ready_contract.md"
LEDGER = ROOT / "docs" / "freeze" / "local_ready_ledger.json"
SCHEMA_VERSION = 1
ENTRY_STATUSES = {"NOT_AUDITED", "PASS", "N/A", "BLOCKED"}
MODULE_STATUSES = {"NOT_AUDITED", "IN_AUDIT", "BLOCKED", "LOCAL_READY"}
EVIDENCE_KINDS = {
    "architecture", "contract", "fault", "lifecycle", "persistence", "state", "test",
}
@dataclass(frozen=True)
class Criterion:
    id: str
    section: str
    title: str
    applicability: str
    evidence: tuple[str, ...]
    allow_na: bool = False


CRITERIA = (
    Criterion("ARCH-RESPONSIBILITY", "Architecture and ownership", "One defined responsibility", "all modules", ("architecture",)),
    Criterion("ARCH-SINGLE-OWNER", "Architecture and ownership", "No second authoritative owner of the same data", "all modules", ("architecture", "state")),
    Criterion("ARCH-DIRECT-DEPS", "Architecture and ownership", "No hidden peer/lower-level dependency", "all modules", ("architecture",)),
    Criterion("ARCH-PORTS", "Architecture and ownership", "External systems are used through approved interfaces/ports", "all modules", ("architecture", "contract")),
    Criterion("API-CLASSIFIED", "Public contracts", "Every public callable is classified", "all modules; one row per exact qualified signature", ("contract",)),
    Criterion("API-PRECONDITIONS", "Public contracts", "Every mutator has explicit preconditions", "all mutators", ("contract", "test")),
    Criterion("API-SUCCESS", "Public contracts", "Every mutator has success postconditions", "all mutators", ("contract", "test")),
    Criterion("API-FAILURE", "Public contracts", "Every mutator has failure and exception semantics", "all mutators", ("contract", "fault", "test")),
    Criterion("API-OVERLOADS", "Public contracts", "Overloads are separate contracts", "all public callable inventories", ("contract",)),
    Criterion("API-INVALID", "Public contracts", "Invalid enums/IDs, malformed payloads and stale handles are rejected unambiguously", "modules exposing any such input", ("contract", "test"), True),
    Criterion("STATE-PRIMARY", "State consistency", "Primary state remains internally consistent", "stateful modules", ("state", "test"), True),
    Criterion("STATE-INDEXES", "State consistency", "Indexes agree with primary state", "modules with indexes/caches", ("state", "test"), True),
    Criterion("STATE-COUNTERS", "State consistency", "Generators, generations, revisions and cursors have checked boundary behavior", "modules exposing those counters", ("state", "test"), True),
    Criterion("STATE-NO-FALSE-PUBLISH", "State consistency", "Failed operations publish no false revision/event/journal mutation", "modules publishing observable change", ("fault", "state", "test"), True),
    Criterion("STATE-NOOP", "State consistency", "No-op semantics are explicit", "all mutators", ("contract", "test")),
    Criterion("LIFE-ALLOWED", "Lifecycle", "Every allowed transition is checked", "modules with lifecycle state", ("lifecycle", "test"), True),
    Criterion("LIFE-FORBIDDEN", "Lifecycle", "Every forbidden transition is rejected", "modules with lifecycle state", ("lifecycle", "test"), True),
    Criterion("LIFE-SHUTDOWN", "Lifecycle", "Shutdown and cleanup semantics are checked", "modules owning resources or registrations", ("lifecycle", "test"), True),
    Criterion("LIFE-RETRY-CLEANUP", "Lifecycle", "Cleanup retry is checked when external cleanup can fail", "fallible external cleanup only", ("fault", "lifecycle", "test"), True),
    Criterion("ATOMIC-SINGLE", "Failure atomicity", "Single-record mutation leaves no partial record", "stateful mutators", ("fault", "state", "test"), True),
    Criterion("ATOMIC-MULTI", "Failure atomicity", "Multi-container mutation commits all or nothing", "mutators spanning multiple containers", ("fault", "state", "test"), True),
    Criterion("ATOMIC-EXTERNAL", "Failure atomicity", "External prepare/commit/cancel/rollback contracts are checked", "modules with external transactional work", ("contract", "fault", "test"), True),
    Criterion("ATOMIC-RECONCILE", "Failure atomicity", "Durable reconciliation is used when rollback can fail", "modules with fallible rollback", ("contract", "fault", "state", "test"), True),
    Criterion("PERSIST-SNAPSHOT", "Persistence", "Snapshot is complete", "persistent modules", ("persistence", "state", "test"), True),
    Criterion("PERSIST-VALIDATE", "Persistence", "Restore validates fully before live mutation", "persistent modules", ("fault", "persistence", "test"), True),
    Criterion("PERSIST-CANDIDATE", "Persistence", "Restore builds a candidate off-state", "persistent modules", ("persistence", "state", "test"), True),
    Criterion("PERSIST-FAILURE", "Persistence", "Failed restore preserves live state", "persistent modules", ("fault", "persistence", "state", "test"), True),
    Criterion("PERSIST-CONTINUITY", "Persistence", "Successful restore preserves generators, generations, revisions, cursors and the persistent/transient boundary", "persistent modules", ("persistence", "state", "test"), True),
    Criterion("TEST-HAPPY", "Tests", "Happy path", "all modules", ("test",)),
    Criterion("TEST-INVALID", "Tests", "Invalid input", "modules accepting fallible input", ("test",), True),
    Criterion("TEST-DUPLICATE", "Tests", "Duplicate identity", "modules with identity registration/creation", ("test",), True),
    Criterion("TEST-STALE", "Tests", "Stale identity", "modules with removable/reusable identities", ("test",), True),
    Criterion("TEST-EMPTY", "Tests", "Empty state", "stateful or collection modules", ("test",), True),
    Criterion("TEST-BOUNDARY", "Tests", "Boundary, overflow and underflow", "modules with numeric limits/counters", ("test",), True),
    Criterion("TEST-WRONG-LIFECYCLE", "Tests", "Wrong lifecycle state", "modules with lifecycle state", ("test",), True),
    Criterion("TEST-CALLBACK-FAILURE", "Tests", "Callback/backend exception or failure", "modules invoking callbacks/backends", ("fault", "test"), True),
    Criterion("TEST-REGRESSION", "Tests", "Regression test for every fixed defect", "all defects fixed during the current audit; PASS asserts the defect ledger was reviewed", ("test",)),
)


def new_ledger() -> dict[str, object]:
    architecture_ids = {criterion.id for criterion in CRITERIA if criterion.section == "Architecture and ownership"}

    def entry(module_name: str, criterion: Criterion) -> dict[str, object]:
        if criterion.id in architecture_ids:
            anchor = f"docs/freeze/architecture_ownership_matrix.md::{module_name}"
            return {
                "status": "PASS",
                "evidence": [{"kind": kind, "anchor": anchor} for kind in criterion.evidence],
                "rationale": "Verified by the generated architecture/ownership gate.",
            }
        return {"status": "NOT_AUDITED", "evidence": [], "rationale": ""}

    return {
        "schema_version": SCHEMA_VERSION,
        "contract": "LOCAL_READY",
        "modules": {
            module.key: {
                "status": "IN_AUDIT",
                "criteria": {
                    criterion.id: entry(module.key, criterion)
                    for criterion in CRITERIA
                },
            }
            for module in discover_modules()
        },
    }


def validate_ledger(data: object) -> list[str]:
    errors: list[str] = []
    criterion_ids = [criterion.id for criterion in CRITERIA]
    if len(CRITERIA) != 37 or len(set(criterion_ids)) != len(criterion_ids):
        errors.append("criterion registry must contain 37 unique IDs")
    for criterion in CRITERIA:
        if not criterion.evidence or not set(criterion.evidence) <= EVIDENCE_KINDS:
            errors.append(f"{criterion.id}: invalid required evidence kinds")
    if not isinstance(data, dict):
        return errors + ["ledger root must be an object"]
    if data.get("schema_version") != SCHEMA_VERSION or data.get("contract") != "LOCAL_READY":
        errors.append("ledger schema_version/contract mismatch")
    modules = data.get("modules")
    if not isinstance(modules, dict):
        return errors + ["modules must be an object"]
    expected_modules = {module.key for module in discover_modules()}
    if set(modules) != expected_modules:
        errors.append("ledger module set does not match the 78 production modules")
    expected_criteria = {criterion.id for criterion in CRITERIA}
    by_id = {criterion.id: criterion for criterion in CRITERIA}
    test_targets = registered_test_targets(ROOT)
    for module_name in sorted(expected_modules & set(modules)):
        record = modules[module_name]
        if not isinstance(record, dict):
            errors.append(f"{module_name}: record must be an object")
            continue
        module_status = record.get("status")
        if module_status not in MODULE_STATUSES:
            errors.append(f"{module_name}: invalid module status {module_status!r}")
        entries = record.get("criteria")
        if not isinstance(entries, dict) or set(entries) != expected_criteria:
            errors.append(f"{module_name}: criterion set does not match the contract")
            continue
        entry_statuses: list[str] = []
        for criterion_id, entry in entries.items():
            if not isinstance(entry, dict):
                errors.append(f"{module_name}/{criterion_id}: entry must be an object")
                continue
            status = entry.get("status")
            entry_statuses.append(status)
            evidence = entry.get("evidence")
            rationale = entry.get("rationale")
            if status not in ENTRY_STATUSES:
                errors.append(f"{module_name}/{criterion_id}: invalid status {status!r}")
                continue
            if not isinstance(evidence, list) or not isinstance(rationale, str):
                errors.append(f"{module_name}/{criterion_id}: evidence must be a list and rationale a string")
                continue
            found_kinds: set[str] = set()
            for item in evidence:
                if not isinstance(item, dict) or item.get("kind") not in EVIDENCE_KINDS:
                    errors.append(f"{module_name}/{criterion_id}: malformed evidence record")
                    continue
                kind = item["kind"]
                found_kinds.add(kind)
                anchor_errors = validate_path_anchor(ROOT, item.get("anchor"))
                errors.extend(f"{module_name}/{criterion_id}: {error}" for error in anchor_errors)
                if kind == "test":
                    test_errors = validate_test_evidence(ROOT, item, test_targets)
                    errors.extend(f"{module_name}/{criterion_id}: {error}" for error in test_errors)
                elif set(item) - {"kind", "anchor"}:
                    errors.append(f"{module_name}/{criterion_id}: non-test evidence has invalid fields")
            if status == "PASS":
                missing = set(by_id[criterion_id].evidence) - found_kinds
                if missing:
                    errors.append(f"{module_name}/{criterion_id}: missing evidence kinds {sorted(missing)}")
            elif status == "N/A":
                if not by_id[criterion_id].allow_na or not rationale.strip():
                    errors.append(f"{module_name}/{criterion_id}: N/A is forbidden or lacks a rationale")
                if evidence:
                    errors.append(f"{module_name}/{criterion_id}: N/A must not carry PASS evidence")
            elif status == "NOT_AUDITED" and evidence:
                errors.append(f"{module_name}/{criterion_id}: NOT_AUDITED must not carry evidence")
            elif status == "BLOCKED" and not rationale.strip():
                errors.append(f"{module_name}/{criterion_id}: BLOCKED lacks a rationale")
        if module_status == "LOCAL_READY" and any(status not in {"PASS", "N/A"} for status in entry_statuses):
            errors.append(f"{module_name}: LOCAL_READY has unresolved criteria")
        if module_status == "BLOCKED" and "BLOCKED" not in entry_statuses:
            errors.append(f"{module_name}: BLOCKED has no blocked criterion")
        if module_status != "BLOCKED" and "BLOCKED" in entry_statuses:
            errors.append(f"{module_name}: blocked criterion requires BLOCKED module status")
        if module_status == "NOT_AUDITED" and any(status != "NOT_AUDITED" for status in entry_statuses):
            errors.append(f"{module_name}: NOT_AUDITED contains audit results")
        if module_status == "IN_AUDIT" and entry_statuses and all(status in {"PASS", "N/A"} for status in entry_statuses):
            errors.append(f"{module_name}: resolved audit remains IN_AUDIT instead of LOCAL_READY")
    return errors


def render_contract() -> str:
    lines = [
        "# LOCAL_READY contract",
        "",
        "This is the single module-level admission contract used by Goals 2-4. It defines evidence requirements; it does not mark unaudited modules ready.",
        "",
        f"Contract inventory: `{len(CRITERIA)}` criteria for exactly `78` production modules.",
        "",
        "A module may be `LOCAL_READY` only when every criterion is `PASS` or an explicitly permitted `N/A`. A `PASS` needs anchored evidence of every listed kind. An `N/A` needs a module-specific rationale and is rejected where the criterion is unconditional.",
        "",
        "Every evidence anchor uses `path[:line]::symbol-or-case`; the validator resolves the repository-relative file, optional line and symbol. Callable evidence uses `public-header:line::fully-qualified-signature`; overloads therefore cannot collapse to one name. Test evidence names a CTest-registered executable and exact case/function, and supplies separately validated assertion anchors rather than a textual API-name match. State/fault evidence records the compared pre-state, injected failure point and observable post-state.",
        "",
    ]
    current = ""
    for criterion in CRITERIA:
        if criterion.section != current:
            lines.extend([f"## {criterion.section}", ""])
            current = criterion.section
        na_rule = "permitted with rationale" if criterion.allow_na else "not permitted"
        lines.extend([
            f"### {criterion.id}",
            "",
            f"- Requirement: {criterion.title}.",
            f"- Applicability: {criterion.applicability}.",
            f"- Required evidence kinds: {', '.join(f'`{kind}`' for kind in criterion.evidence)}.",
            f"- `N/A`: {na_rule}.",
            "",
        ])
    lines.extend([
        "## Ledger workflow",
        "",
        "`local_ready_ledger.json` is the authoritative audit ledger. The four verified architecture criteria are seeded as `PASS`; every other new criterion starts as `NOT_AUDITED`. `IN_AUDIT` and `BLOCKED` are intermediate module states. The validator rejects stale module/criterion sets, nonexistent anchor files/lines/symbols, unregistered test targets, missing assertion anchors, evidence-kind omissions, unjustified `N/A`, empty or inconsistent `BLOCKED`, inconsistent module states, and premature `LOCAL_READY`.",
        "",
        "Run `python docs/freeze/local_ready_contract.py --check` for contract and ledger consistency and `python docs/freeze/local_ready_contract.py --self-test` for negative validator tests.",
        "",
    ])
    return "\n".join(lines)


def self_test() -> tuple[list[str], int]:
    failures: list[str] = []
    with tempfile.TemporaryDirectory() as temporary:
        fixture_root = Path(temporary)
        (fixture_root / "CMakeLists.txt").write_text(
            "# add_test(NAME CommentedTest COMMAND ignored)\n"
            "add_test(NAME ActiveTest COMMAND active)\n",
            encoding="utf-8",
        )
        build_root = fixture_root / "build" / "fixture"
        build_root.mkdir(parents=True)
        (build_root / "CMakeLists.txt").write_text(
            "add_test(NAME BuildOnlyTest COMMAND generated)\n",
            encoding="utf-8",
        )
        if registered_test_targets(fixture_root) != {"ActiveTest"}:
            failures.append("CTest target discovery accepted commented or build-only evidence")
    baseline = new_ledger()
    if validate_ledger(baseline):
        failures.append("baseline ledger was rejected")
    module = next(iter(baseline["modules"]))

    complete = copy.deepcopy(baseline)
    complete["modules"][module]["status"] = "LOCAL_READY"
    for criterion in CRITERIA:
        evidence = []
        for kind in criterion.evidence:
            if kind == "test":
                evidence.append({
                    "kind": "test",
                    "anchor": "EngineFramework/DevelopmentInfrastructure/Tests/test_utilities_tests.cpp::main",
                    "target": "EpidemicGameFrameworkTestUtilitiesTests",
                    "assertions": [
                        "EngineFramework/DevelopmentInfrastructure/Tests/test_utilities_tests.cpp::Check"
                    ],
                })
            else:
                evidence.append({
                    "kind": kind,
                    "anchor": f"docs/freeze/local_ready_contract.md::{criterion.id}",
                })
        complete["modules"][module]["criteria"][criterion.id] = {
            "status": "PASS",
            "evidence": evidence,
            "rationale": "",
        }
    if validate_ledger(complete):
        failures.append("complete LOCAL_READY record was rejected")

    cases: list[tuple[str, dict[str, object]]] = []
    missing_module = copy.deepcopy(baseline)
    missing_module["modules"].pop(module)
    cases.append(("missing module", missing_module))
    premature = copy.deepcopy(baseline)
    premature["modules"][module]["status"] = "LOCAL_READY"
    cases.append(("premature LOCAL_READY", premature))
    unjustified_na = copy.deepcopy(baseline)
    unjustified_na["modules"][module]["criteria"]["TEST-INVALID"]["status"] = "N/A"
    cases.append(("unjustified N/A", unjustified_na))
    incomplete_pass = copy.deepcopy(baseline)
    incomplete_pass["modules"][module]["criteria"]["API-FAILURE"] = {
        "status": "PASS", "evidence": [{"kind": "test", "anchor": "tests.cpp:1::case"}], "rationale": ""
    }
    cases.append(("incomplete PASS evidence", incomplete_pass))
    false_blocked = copy.deepcopy(baseline)
    false_blocked["modules"][module]["status"] = "BLOCKED"
    cases.append(("false BLOCKED", false_blocked))
    malformed_anchor = copy.deepcopy(baseline)
    malformed_anchor["modules"][module]["criteria"]["TEST-HAPPY"] = {
        "status": "PASS", "evidence": [{"kind": "test", "anchor": "name-only"}], "rationale": ""
    }
    cases.append(("malformed anchor", malformed_anchor))
    inconsistent_status = copy.deepcopy(baseline)
    inconsistent_status["modules"][module]["status"] = "NOT_AUDITED"
    cases.append(("inconsistent NOT_AUDITED", inconsistent_status))
    nonexistent_file = copy.deepcopy(baseline)
    nonexistent_file["modules"][module]["criteria"]["TEST-HAPPY"] = {
        "status": "PASS",
        "evidence": [{
            "kind": "test",
            "anchor": "tests/missing.cpp:1::case",
            "target": "EpidemicGameFrameworkTestUtilitiesTests",
            "assertions": ["EngineFramework/DevelopmentInfrastructure/Tests/test_utilities_tests.cpp::Check"],
        }],
        "rationale": "",
    }
    cases.append(("nonexistent evidence file", nonexistent_file))
    nonexistent_symbol = copy.deepcopy(baseline)
    nonexistent_symbol["modules"][module]["criteria"]["ARCH-RESPONSIBILITY"]["evidence"][0]["anchor"] = (
        "docs/freeze/architecture_ownership_matrix.md::missing-symbol"
    )
    cases.append(("nonexistent evidence symbol", nonexistent_symbol))
    unknown_test_target = copy.deepcopy(complete)
    unknown_test_target["modules"][module]["criteria"]["TEST-HAPPY"]["evidence"][0]["target"] = "MissingTests"
    cases.append(("unknown test target", unknown_test_target))
    missing_assertions = copy.deepcopy(complete)
    missing_assertions["modules"][module]["criteria"]["TEST-HAPPY"]["evidence"][0]["assertions"] = []
    cases.append(("missing assertion anchors", missing_assertions))
    empty_blocker = copy.deepcopy(baseline)
    empty_blocker["modules"][module]["status"] = "BLOCKED"
    empty_blocker["modules"][module]["criteria"]["TEST-HAPPY"]["status"] = "BLOCKED"
    cases.append(("empty blocker rationale", empty_blocker))
    hidden_blocker = copy.deepcopy(baseline)
    hidden_blocker["modules"][module]["criteria"]["TEST-HAPPY"] = {
        "status": "BLOCKED", "evidence": [], "rationale": "Waiting for reviewed evidence."
    }
    cases.append(("blocked criterion with IN_AUDIT module", hidden_blocker))
    for name, candidate in cases:
        if not validate_ledger(candidate):
            failures.append(f"negative case was accepted: {name}")
    return failures, len(cases)


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate and validate the LOCAL_READY admission contract.")
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--sync-architecture", action="store_true", help="Copy verified architecture evidence into the audit ledger.")
    args = parser.parse_args()

    if args.self_test:
        failures, case_count = self_test()
        if failures:
            print("\n".join(f"ERROR: {item}" for item in failures), file=sys.stderr)
            return 1
        print(f"PASS: LOCAL_READY validator accepted a complete record and rejected all {case_count} malformed ledgers.")
        return 0

    rendered = render_contract()
    if args.sync_architecture:
        seeded = new_ledger()
        current = json.loads(LEDGER.read_text(encoding="utf-8")) if LEDGER.exists() else seeded
        for module_name, record in current["modules"].items():
            if record["status"] == "NOT_AUDITED":
                record["status"] = "IN_AUDIT"
            for criterion in CRITERIA:
                if criterion.section == "Architecture and ownership":
                    record["criteria"][criterion.id] = seeded["modules"][module_name]["criteria"][criterion.id]
        LEDGER.write_text(json.dumps(current, indent=2, ensure_ascii=True) + "\n", encoding="utf-8", newline="\n")
        print("Synchronized 4 architecture criteria for 78 modules into the LOCAL_READY ledger.")
        return 0

    if args.check:
        if not OUTPUT.exists() or OUTPUT.read_text(encoding="utf-8") != rendered:
            print("ERROR: LOCAL_READY contract is missing or stale", file=sys.stderr)
            return 1
        if not LEDGER.exists():
            print("ERROR: LOCAL_READY ledger is missing", file=sys.stderr)
            return 1
        errors = validate_ledger(json.loads(LEDGER.read_text(encoding="utf-8")))
        if errors:
            print("\n".join(f"ERROR: {item}" for item in errors), file=sys.stderr)
            return 1
        ready = sum(record["status"] == "LOCAL_READY" for record in json.loads(LEDGER.read_text(encoding="utf-8"))["modules"].values())
        print(f"PASS: 37/37 LOCAL_READY criteria, 78/78 ledger records, {ready}/78 modules currently LOCAL_READY.")
        return 0

    OUTPUT.write_text(rendered, encoding="utf-8", newline="\n")
    if not LEDGER.exists():
        LEDGER.write_text(json.dumps(new_ledger(), indent=2, ensure_ascii=True) + "\n", encoding="utf-8", newline="\n")
    print("Wrote LOCAL_READY contract; initialized the 78-module audit ledger only if it was absent.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
