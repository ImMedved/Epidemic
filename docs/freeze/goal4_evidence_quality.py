"""Validate that Goal 4 evidence is specific, typed and traceable.

This gate rejects syntactically valid placeholders, audits raw B01--B08
handoffs before merge, and checks both directions of defect traceability.
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from collections import Counter
from pathlib import Path

sys.dont_write_bytecode = True
from evidence_anchors import registered_test_targets, validate_path_anchor, validate_test_evidence
from module_dossiers import ROOT

FREEZE = ROOT / "docs" / "freeze"
HANDOFF = ROOT / "_goal4_handoff"
FRAMEWORK = "EngineFramework/"
ANCHOR_RE = re.compile(r"^[^\s:]+:\d+(?:-\d+)?::\S.+$")

# Findings admitted by the Bxx audits after the original milestone list.
POST_ADMISSION_GOAL4_FINDINGS = {
    "G4-B02-COND-001", "G4-B02-EFF-001", "G4-B02-EFF-002",
    "G4-B02-EFF-003", "G4-B02-INT-001", "G4-B02-OWN-001",
    "G4-B06-PROG-004", "B08-INT-001", "B08-PRSINT-001",
}

REQUIRED_KINDS = {
    "ARCH-RESPONSIBILITY": ("architecture",),
    "ARCH-SINGLE-OWNER": ("architecture", "state"),
    "ARCH-DIRECT-DEPS": ("architecture",),
    "ARCH-PORTS": ("architecture", "contract"),
    "API-CLASSIFIED": ("contract",),
    "API-PRECONDITIONS": ("contract", "test"),
    "API-SUCCESS": ("contract", "test"),
    "API-FAILURE": ("contract", "fault", "test"),
    "API-OVERLOADS": ("contract",),
    "API-INVALID": ("contract", "test"),
    "STATE-PRIMARY": ("state", "test"),
    "STATE-INDEXES": ("state", "test"),
    "STATE-COUNTERS": ("state", "test"),
    "STATE-NO-FALSE-PUBLISH": ("fault", "state", "test"),
    "STATE-NOOP": ("contract", "test"),
    "LIFE-ALLOWED": ("lifecycle", "test"),
    "LIFE-FORBIDDEN": ("lifecycle", "test"),
    "LIFE-SHUTDOWN": ("lifecycle", "test"),
    "LIFE-RETRY-CLEANUP": ("fault", "lifecycle", "test"),
    "ATOMIC-SINGLE": ("fault", "state", "test"),
    "ATOMIC-MULTI": ("fault", "state", "test"),
    "ATOMIC-EXTERNAL": ("contract", "fault", "test"),
    "ATOMIC-RECONCILE": ("contract", "fault", "state", "test"),
    "PERSIST-SNAPSHOT": ("persistence", "state", "test"),
    "PERSIST-VALIDATE": ("fault", "persistence", "test"),
    "PERSIST-CANDIDATE": ("persistence", "state", "test"),
    "PERSIST-FAILURE": ("fault", "persistence", "state", "test"),
    "PERSIST-CONTINUITY": ("persistence", "state", "test"),
    "TEST-HAPPY": ("test",), "TEST-INVALID": ("test",),
    "TEST-DUPLICATE": ("test",), "TEST-STALE": ("test",),
    "TEST-EMPTY": ("test",), "TEST-BOUNDARY": ("test",),
    "TEST-WRONG-LIFECYCLE": ("test",),
    "TEST-CALLBACK-FAILURE": ("fault", "test"),
    "TEST-REGRESSION": ("test",),
}


def read(path: Path | str) -> dict:
    source = FREEZE / path if isinstance(path, str) else path
    return json.loads(source.read_text(encoding="utf-8"))


def generic_contract(anchor: object) -> bool:
    value = str(anchor)
    if not ANCHOR_RE.fullmatch(value):
        return True
    symbol = value.rsplit("::", 1)[-1].strip().lower()
    return symbol in {
        "responsibility:", "## responsibility", "## public api",
        "## public mutation api", "## public contract review", "## test evidence",
    }


def generic_test(record: object) -> bool:
    if not isinstance(record, dict):
        return True
    anchor = str(record.get("anchor", ""))
    assertions = record.get("assertions")
    if not ANCHOR_RE.fullmatch(anchor) or re.search(r"::int\s+main(?:\(\))?$", anchor):
        return True
    if not isinstance(assertions, list) or not assertions:
        return True
    generic = re.compile(
        r"::(?:if\s*\(|return\b|(?:void\s+)?Check\s*\(|#define\s+CHECK\b|CHECK\s*\()$",
        re.IGNORECASE,
    )
    return all(not isinstance(item, str) or not ANCHOR_RE.fullmatch(item) or generic.search(item)
               for item in assertions)


def generic_na(module: str, rationale: object) -> bool:
    text = str(rationale).strip()
    if not text:
        return True
    lowered = text.lower()
    # A rationale can be module-specific through named domain concepts (for
    # example QuerySnapshot or subscriber dispatch) without repeating the
    # module's directory name. Reject only known template prose.
    return any(phrase in lowered for phrase in (
        "review marks", "not applicable to this module", "no applicable surface",
    ))


def logical_defect_id(record_id: str, record: dict) -> str | None:
    explicit = record.get("logical_id")
    if isinstance(explicit, str) and explicit:
        return explicit.upper()
    joined = f"{record_id} {record.get('defect', '')}".upper()
    match = re.search(r"(?:G4-[A-Z0-9]+(?:-[A-Z0-9]+)*-\d+|B08-[A-Z]+-\d+)", joined)
    return match.group(0) if match else None


def defect_regression_errors(record: dict, targets: set[str]) -> list[str]:
    errors: list[str] = []
    raw_targets = record.get("targets")
    if raw_targets is None:
        raw_targets = [record.get("target")]
    if (not isinstance(raw_targets, list) or not raw_targets or
            any(not isinstance(target, str) or target not in targets for target in raw_targets)):
        return ["invalid target set"]
    if record.get("target") not in raw_targets:
        errors.append("primary target is absent from targets")
    regressions = record.get("regressions")
    if isinstance(regressions, dict):
        regression_items = [{"target": target, "anchor": anchor}
                            for target, anchor in regressions.items()]
    elif isinstance(regressions, list):
        regression_items = regressions
    else:
        regression_items = []
    if not regression_items:
        regression = record.get("regression")
        if len(raw_targets) != 1 or validate_path_anchor(ROOT, regression):
            errors.append("missing exact per-target regressions")
        return errors
    covered: set[str] = set()
    for index, item in enumerate(regression_items):
        if not isinstance(item, dict):
            errors.append(f"regressions[{index}] is not an object")
            continue
        target, anchor = item.get("target"), item.get("anchor")
        if target not in raw_targets:
            errors.append(f"regressions[{index}] has unknown target")
        else:
            covered.add(target)
        errors.extend(f"regressions[{index}]: {error}" for error in validate_path_anchor(ROOT, anchor))
    if covered != set(raw_targets):
        errors.append("regressions do not cover every target")
    return errors


def raw_criterion_errors(module: str, criterion: str, entry: object,
                         targets: set[str]) -> list[str]:
    if not isinstance(entry, dict):
        return ["criterion is not an object"]
    if entry.get("status") == "N/A":
        errors = []
        if entry.get("evidence") != []:
            errors.append("N/A evidence must be an empty list")
        if generic_na(module, entry.get("rationale")):
            errors.append("N/A rationale is empty or generic")
        return errors
    if entry.get("status") != "PASS":
        return ["criterion status must be PASS or N/A"]
    evidence = entry.get("evidence")
    if not isinstance(evidence, list):
        return ["PASS evidence must be an array of typed objects"]
    errors: list[str] = []
    seen: set[str] = set()
    for index, item in enumerate(evidence):
        if not isinstance(item, dict):
            errors.append(f"evidence[{index}] is not an object")
            continue
        kind = item.get("kind")
        if not isinstance(kind, str):
            errors.append(f"evidence[{index}] has no kind")
            continue
        seen.add(kind)
        errors.extend(f"evidence[{index}]: {error}" for error in validate_path_anchor(ROOT, item.get("anchor")))
        if kind == "test":
            errors.extend(f"evidence[{index}]: {error}"
                          for error in validate_test_evidence(ROOT, item, targets))
            if generic_test(item):
                errors.append(f"evidence[{index}] is a generic test placeholder")
    required = set(REQUIRED_KINDS.get(criterion, ()))
    if not required:
        errors.append("unknown criterion")
    elif not required <= seen:
        errors.append(f"missing evidence kinds: {sorted(required - seen)}")
    return errors


def audit_raw_handoffs(counts: Counter, samples: list[str], targets: set[str]) -> None:
    for block in sorted(HANDOFF.glob("B[0-9][0-9]")):
        paths = sorted(block.glob("local_ready*.json"))
        if len(paths) != 1:
            counts["raw_local_ready_file_count"] += 1
            continue
        modules = read(paths[0]).get("modules", {})
        for module, raw in modules.items():
            criteria = raw.get("criteria", raw) if isinstance(raw, dict) else {}
            if set(criteria) != set(REQUIRED_KINDS):
                counts["raw_criterion_set_mismatch"] += 1
            for criterion in REQUIRED_KINDS:
                errors = raw_criterion_errors(module, criterion, criteria.get(criterion), targets)
                if errors:
                    counts["raw_criterion_evidence_error"] += 1
                    if len(samples) < 18:
                        samples.append(f"{block.name} {module}/{criterion}: {errors[0]}")


def official_defect_ids() -> set[str]:
    milestone = (ROOT / "Milestone 4.md").read_text(encoding="utf-8")
    original = set(re.findall(r"^### (G4-[A-Z]+-\d+)", milestone, re.MULTILINE))
    return original | POST_ADMISSION_GOAL4_FINDINGS


def audit() -> tuple[Counter, list[str]]:
    counts: Counter = Counter()
    samples: list[str] = []
    api = read("public_api_anchors.json")["anchors"]
    coverage = read("coverage_manifests.json")
    ledger = read("local_ready_ledger.json")["modules"]
    targets = registered_test_targets(ROOT)
    framework_ids = {key for key, item in coverage["api"].items()
                     if str(item.get("module", "")).startswith(FRAMEWORK)}
    for item_id in framework_ids:
        record = api.get(item_id, {})
        if generic_contract(record.get("contract", {}).get("anchor")):
            counts["generic_api_contract"] += 1
        if generic_test(record.get("test")):
            counts["generic_api_test"] += 1
            if len(samples) < 6:
                samples.append(f"API {item_id}: {record.get('test', {}).get('anchor', 'missing')}")
    for module, item in ledger.items():
        if not module.startswith(FRAMEWORK):
            continue
        if item.get("status") != "LOCAL_READY":
            counts["framework_not_local_ready"] += 1
        for entry in item.get("criteria", {}).values():
            if entry.get("status") == "N/A" and generic_na(module, entry.get("rationale")):
                counts["generic_na_rationale"] += 1
            if entry.get("status") == "PASS" and any(
                    generic_test(evidence) for evidence in entry.get("evidence", [])
                    if isinstance(evidence, dict) and evidence.get("kind") == "test"):
                counts["generic_criterion_test"] += 1
    raw_defects: list[tuple[str, dict]] = []
    for block in sorted(HANDOFF.glob("B[0-9][0-9]")):
        path = block / "defects.json"
        if path.is_file():
            raw_defects.extend(read(path).get("defects", {}).items())
    logical_ids: set[str] = set()
    for record_id, record in raw_defects:
        logical_id = logical_defect_id(record_id, record)
        if logical_id:
            logical_ids.add(logical_id)
        else:
            counts["defect_without_logical_id"] += 1
        if logical_id == "G4-INFRA-001" and record.get("logical_id") != "G4-INFRA-001":
            counts["infra_record_without_logical_id"] += 1
        errors = defect_regression_errors(record, targets)
        if errors:
            counts["defect_regression_error"] += 1
            if len(samples) < 24:
                samples.append(f"defect {record_id}: {errors[0]}")
    tracked = official_defect_ids()
    for missing in sorted(tracked - logical_ids):
        counts["missing_tracked_raw_goal4_defect"] += 1
        samples.append(f"missing tracked raw defect: {missing}")
    for extra in sorted(logical_ids - tracked):
        counts["untracked_raw_goal4_defect"] += 1
        samples.append(f"untracked raw Goal 4 defect: {extra}")

    canonical_ids: set[str] = set()
    for record_id, record in coverage.get("defects", {}).items():
        logical_id = logical_defect_id(record_id, record)
        if logical_id is None:
            continue  # Earlier non-Goal-4 history, e.g. environment equality.
        canonical_ids.add(logical_id)
        errors = defect_regression_errors(record, targets)
        if errors:
            counts["canonical_defect_regression_error"] += 1
            if len(samples) < 28:
                samples.append(f"canonical defect {record_id}: {errors[0]}")
    for missing in sorted(tracked - canonical_ids):
        counts["missing_tracked_canonical_goal4_defect"] += 1
        samples.append(f"missing tracked canonical defect: {missing}")
    for extra in sorted(canonical_ids - tracked):
        counts["untracked_canonical_goal4_defect"] += 1
        samples.append(f"untracked canonical Goal 4 defect: {extra}")
    audit_raw_handoffs(counts, samples, targets)
    doc_root = ROOT / "docs" / "EngineFramework"
    readme = (doc_root / "README.md").read_text(encoding="utf-8")
    for link in re.findall(r"\]\(([^)]+\.md)\)", readme):
        if not (doc_root / link).is_file():
            counts["broken_doc_link"] += 1
            samples.append(f"missing doc link: {link}")
    return counts, samples


def self_test() -> None:
    assert generic_test({"anchor": "x.cpp:4::int main()", "assertions": ["x.cpp:5::return 0;"]})
    assert generic_contract("docs/x.md:7::## Public contract review")
    assert raw_criterion_errors("EngineFramework/X/Widget", "TEST-HAPPY", "docs/x.md", set())
    assert generic_test({"anchor": "x.cpp:4::Scenario", "assertions": []})
    assert generic_na("EngineFramework/X/Widget", "not applicable to this module")
    assert {"G4-A-001", "G4-EXTRA-001"} - {"G4-A-001"} == {"G4-EXTRA-001"}
    multi = {"target": "One", "targets": ["One", "Two"], "regression": "x.cpp:4::Scenario"}
    assert defect_regression_errors(multi, {"One", "Two"})
    print("Goal 4 evidence quality self-test passed (7 negative fixtures).")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return 0
    counts, samples = audit()
    for name, value in sorted(counts.items()):
        print(f"{name}: {value}")
    for sample in samples:
        print(f"  {sample}")
    if args.check and counts:
        print("BLOCKED: Goal 4 evidence requires a per-item semantic review.", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
