from __future__ import annotations

import json
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
HANDOFF = ROOT / "_goal4_handoff"
FREEZE = ROOT / "docs" / "freeze"
sys.path.insert(0, str(FREEZE))

from coverage_manifests import generate as generate_coverage_manifest
from evidence_anchors import registered_test_targets, validate_path_anchor, validate_test_evidence
from goal4_evidence_quality import defect_regression_errors, generic_contract, generic_test
from module_dossiers import discover_modules, normalized_reviews
COVERAGE_KINDS = ("api", "defects", "external_boundary", "lifecycle", "stale_identity")
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
    "TEST-HAPPY": ("test",),
    "TEST-INVALID": ("test",),
    "TEST-DUPLICATE": ("test",),
    "TEST-STALE": ("test",),
    "TEST-EMPTY": ("test",),
    "TEST-BOUNDARY": ("test",),
    "TEST-WRONG-LIFECYCLE": ("test",),
    "TEST-CALLBACK-FAILURE": ("fault", "test"),
    "TEST-REGRESSION": ("test",),
}


def load(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def save(path: Path, value: dict) -> None:
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def merge_unique(target: dict, source: dict, label: str) -> None:
    overlap = set(target).intersection(source)
    if overlap:
        raise RuntimeError(f"duplicate {label}: {sorted(overlap)[:3]}")
    target.update(source)


ANCHOR_PATTERN = re.compile(r"^(.*):(\d+)::(.*)$")
line_cache: dict[str, list[str]] = {}
rebased_anchors = 0


def rebase_anchor(value: str) -> str:
    global rebased_anchors
    match = ANCHOR_PATTERN.fullmatch(value)
    if match is None:
        return value
    relative, raw_line, symbol = match.groups()
    path = ROOT / relative
    if not path.is_file():
        return value
    lines = line_cache.setdefault(relative, path.read_text(encoding="utf-8").splitlines())
    line = int(raw_line)
    if 1 <= line <= len(lines) and symbol in lines[line - 1]:
        return value
    hits = [index + 1 for index, text in enumerate(lines) if symbol in text]
    if not hits:
        raise RuntimeError(f"anchor symbol not found: {value}")
    distance = min(abs(hit - line) for hit in hits)
    nearest = [hit for hit in hits if abs(hit - line) == distance]
    if len(nearest) != 1:
        raise RuntimeError(f"anchor rebasing is ambiguous: {value}; candidates={nearest}")
    rebased_anchors += 1
    return f"{relative}:{nearest[0]}::{symbol}"


def rebase_tree(value, field: str = ""):
    if isinstance(value, dict):
        return {key: rebase_tree(item, key) for key, item in value.items()}
    if isinstance(value, list):
        return [rebase_tree(item, field) for item in value]
    if isinstance(value, str) and field in {"anchor", "assertions", "review_evidence"}:
        return rebase_anchor(value)
    return value


blocks = sorted(path for path in HANDOFF.glob("B[0-9][0-9]") if path.is_dir())
if len(blocks) != 8:
    raise RuntimeError(f"expected 8 Goal 4 blocks, found {len(blocks)}")

handoff_anchors: dict = {}
handoff_coverage = {kind: {} for kind in COVERAGE_KINDS}
handoff_dossiers: dict = {}
handoff_local_ready: dict = {}
module_blocks: dict[str, str] = {}

for block in blocks:
    anchors = load(block / "public_api_anchors.json")["anchors"]
    merge_unique(handoff_anchors, anchors, f"API anchor in {block.name}")

    coverage = load(block / "coverage_reviews.json")
    for kind in COVERAGE_KINDS:
        if kind == "defects":
            continue
        merge_unique(handoff_coverage[kind], coverage[kind], f"{kind} review in {block.name}")
    # defects.json is the closure-grade source of truth. coverage_reviews.json
    # retains the original worker projection and may still contain prose-only
    # regression descriptions. Keep the eight physical infrastructure slices
    # distinct while linking them through logical_id=G4-INFRA-001.
    defect_path = block / "defects.json"
    if defect_path.is_file():
        for defect_id, record in load(defect_path)["defects"].items():
            canonical_id = (
                f"G4-INFRA-001/{block.name}"
                if record.get("logical_id") == "G4-INFRA-001"
                else defect_id
            )
            merge_unique(handoff_coverage["defects"], {canonical_id: record},
                         f"defect in {block.name}")

    dossiers = load(block / "dossier_reviews.json")["modules"]
    merge_unique(handoff_dossiers, dossiers, f"dossier module in {block.name}")

    local_ready_paths = sorted(block.glob("local_ready*.json"))
    if len(local_ready_paths) != 1:
        raise RuntimeError(f"expected one local-ready projection in {block.name}")
    local_ready = load(local_ready_paths[0])["modules"]
    merge_unique(handoff_local_ready, local_ready, f"LOCAL_READY module in {block.name}")
    module_blocks.update({module: block.name for module in local_ready})

if set(handoff_anchors) != set(handoff_coverage["api"]):
    raise RuntimeError("handoff API anchors and coverage API IDs differ")
if len(handoff_dossiers) != 52 or set(handoff_dossiers) != set(handoff_local_ready):
    raise RuntimeError("handoff dossier and LOCAL_READY module sets are incomplete or inconsistent")

coverage_path = FREEZE / "coverage_manifests.json"
coverage = load(coverage_path)
old_framework_api_ids = {
    item_id
    for item_id, record in coverage["api"].items()
    if str(record.get("module", "")).startswith("EngineFramework/")
}

for kind in ("api", "external_boundary", "lifecycle", "stale_identity"):
    coverage[kind] = {
        item_id: record
        for item_id, record in coverage[kind].items()
        if not str(record.get("module", "")).startswith("EngineFramework/")
    }
    coverage[kind].update(handoff_coverage[kind])

# B06 carries reviewed obligation metadata inline, while the canonical manifest
# stores only each obligation's status; the evidence itself is in API anchors.
for record in coverage["api"].values():
    obligations = record.get("obligations", {})
    for name, decision in list(obligations.items()):
        if isinstance(decision, dict):
            obligations[name] = decision.get("status", "DISCOVERED")

# Defects are historical keyed records rather than a complete per-module inventory.
# Replace the prior Goal 4 projection with the reviewed physical records while
# preserving unrelated historical defects (for example environment-defaulted-equality).
coverage["defects"] = {
    defect_id: record
    for defect_id, record in coverage["defects"].items()
    if not (
        defect_id.upper().startswith("G4-")
        or defect_id.startswith("B08-")
        or str(record.get("logical_id", "")).upper().startswith("G4-")
    )
}
coverage["defects"].update(handoff_coverage["defects"])
# Promote already named single-function regressions to exact source-line
# anchors.  Descriptive blocks and compound case lists remain audit debt.
for record in coverage["defects"].values():
    regression = str(record.get("regression", ""))
    match = re.fullmatch(r"(EngineFramework/[^:]+\.cpp)::([A-Za-z_][A-Za-z_0-9]*)", regression)
    if match is None:
        continue
    relative, symbol = match.groups()
    path = ROOT / relative
    if not path.is_file():
        continue
    definition = re.compile(r"\b(?:bool|void|int|auto)\s+" + re.escape(symbol) + r"\s*\(")
    hits = [line for line, source in enumerate(path.read_text(encoding="utf-8").splitlines(), 1)
            if definition.search(source)]
    if len(hits) == 1:
        record["regression"] = f"{relative}:{hits[0]}::{symbol}"
registered = registered_test_targets(ROOT)
for defect_id, record in handoff_coverage["defects"].items():
    errors = defect_regression_errors(record, registered)
    if errors:
        raise RuntimeError(f"invalid defect traceability for {defect_id}: {errors}")
save(coverage_path, coverage)

anchors_path = FREEZE / "public_api_anchors.json"
anchors = load(anchors_path)
anchors["anchors"] = {
    item_id: record for item_id, record in anchors["anchors"].items() if item_id not in old_framework_api_ids
}
anchors["anchors"].update(handoff_anchors)
save(anchors_path, anchors)

dossiers_path = FREEZE / "module_dossier_reviews.json"
dossiers = load(dossiers_path)
dossiers["modules"] = {
    module: record for module, record in dossiers["modules"].items() if not module.startswith("EngineFramework/")
}
dossiers["modules"].update(handoff_dossiers)
save(dossiers_path, normalized_reviews(discover_modules(), dossiers))

# Some worker archives had extra test-seam setup lines relative to their recorded
# evidence locations. Rebase only invalid anchors to an unambiguous nearest copy
# of the exact recorded symbol in the integrated tree.
for path in (coverage_path, anchors_path):
    save(path, rebase_tree(load(path)))

# Worker handoffs carry review decisions plus block-specific helper metadata.
# Rebuild the canonical inventory shape after merging so a successful merge is
# immediately accepted by the freeze validators while preserving those decisions.
save(coverage_path, generate_coverage_manifest(load(coverage_path)))

# Normalize the eight block-local LOCAL_READY projections into the canonical
# evidence schema.  Invalid or incomplete worker proof is retained as BLOCKED;
# the merge must never manufacture PASS evidence from a convenient API record.
canonical_anchors = load(anchors_path)["anchors"]
api_by_module: dict[str, list[str]] = {}
for item_id, record in coverage["api"].items():
    api_by_module.setdefault(record["module"], []).append(item_id)

local_ready_path = FREEZE / "local_ready_ledger.json"
local_ready = load(local_ready_path)
registered_targets = registered_test_targets(ROOT)
blocked_modules = 0
for module, raw_module in handoff_local_ready.items():
    raw_criteria = raw_module.get("criteria", raw_module)
    candidates = [canonical_anchors[item_id] for item_id in sorted(api_by_module.get(module, []))]
    reviewed = [item for item in candidates if "contract" in item and "test" in item]
    if not reviewed:
        raise RuntimeError(f"no complete reviewed API anchor for {module}")
    normalized = {}
    source_gaps = []
    for criterion, kinds in REQUIRED_KINDS.items():
        decision = raw_criteria.get(criterion)
        if not isinstance(decision, dict) or decision.get("status") not in {"PASS", "N/A"}:
            raise RuntimeError(f"missing PASS/N/A decision for {module}/{criterion}")
        status = decision["status"]
        if status == "N/A":
            rationale = str(decision.get("rationale", "")).strip()
            if not rationale:
                rationale = (
                    "Serial evidence audit: the raw handoff supplied no module-specific "
                    "N/A rationale. Supply reviewed proof before LOCAL_READY."
                )
                source_gaps.append(criterion)
            normalized[criterion] = {
                "status": "BLOCKED" if criterion in source_gaps else "N/A",
                "evidence": [], "rationale": rationale,
            }
            continue
        raw_evidence = decision.get("evidence")
        source_complete = (
            isinstance(raw_evidence, list)
            and all(isinstance(item, dict) and item.get("kind") in kinds for item in raw_evidence)
            and set(kinds) <= {item["kind"] for item in raw_evidence}
            and all(isinstance(item.get("anchor"), str) and ANCHOR_PATTERN.fullmatch(item["anchor"])
                    and not validate_path_anchor(ROOT, item["anchor"])
                    for item in raw_evidence)
            and all(not generic_test(item)
                    and not validate_test_evidence(ROOT, item, registered_targets)
                    for item in raw_evidence if item["kind"] == "test")
        )
        if not source_complete:
            source_gaps.append(criterion)
        normalized[criterion] = {
            "status": "PASS" if source_complete else "BLOCKED",
            "evidence": rebase_tree(raw_evidence) if source_complete else [],
            "rationale": (str(decision.get("rationale", "")) if source_complete else
                          "Serial evidence audit: raw typed proof is incomplete or invalid; "
                          "the merge does not synthesize substitute PASS evidence."),
        }
    # A schema-valid pointer to a test executable's main() (or a generic
    # assertion) does not demonstrate every callable/criterion.  Keep the
    # worker review and evidence, but do not turn it into a closure claim.
    generic_api = any(generic_test(item.get("test", {})) for item in candidates)
    generic_contracts = any(generic_contract(item.get("contract", {}).get("anchor", ""))
                            for item in candidates)
    generic_na = [criterion for criterion, item in normalized.items()
                  if item["status"] == "N/A" and "review marks" in item["rationale"]]
    generic_criteria = [criterion for criterion, item in normalized.items()
                        if item["status"] == "PASS" and any(
                            generic_test(evidence) for evidence in item["evidence"]
                            if evidence["kind"] == "test")]
    if source_gaps or generic_api or generic_contracts or generic_na or generic_criteria:
        criterion = (next((name for name in source_gaps if not name.startswith("ARCH-")), source_gaps[0])
                     if source_gaps else "API-SUCCESS" if generic_api or generic_contracts else
                     generic_na[0] if generic_na else generic_criteria[0])
        normalized[criterion]["status"] = "BLOCKED"
        normalized[criterion]["evidence"] = []
        normalized[criterion]["rationale"] = (
            "Serial evidence audit: source handoff lacks complete criterion-specific proof, "
            "or generic per-API/criterion anchors or non-module-specific N/A rationale remain. "
            "Supply specific reviewed proof before LOCAL_READY."
        )
        module_status = "BLOCKED"
        blocked_modules += 1
    else:
        module_status = "LOCAL_READY"
    local_ready["modules"][module] = {"status": module_status, "criteria": normalized}
save(local_ready_path, local_ready)

print(
    "Merged Goal 4 evidence: "
    f"{len(handoff_dossiers)} modules, {len(handoff_anchors)} APIs, "
    f"{len(handoff_coverage['defects'])} defect records, "
    f"{len(handoff_local_ready) - blocked_modules} LOCAL_READY / {blocked_modules} BLOCKED modules; "
    f"rebased {rebased_anchors} stale line anchors."
)
