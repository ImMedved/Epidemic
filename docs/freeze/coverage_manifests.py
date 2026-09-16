from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path

sys.dont_write_bytecode = True

from module_dossiers import ROOT, discover_modules
from public_api_inventory import apply_classification_overrides, load_classification_overrides, scan_modules


OUTPUT = ROOT / "docs" / "freeze" / "coverage_manifests.json"
STATUSES = {"DISCOVERED", "REVIEWED"}
MUTATOR_CLASSES = {"MUTATOR", "LIFECYCLE"}


def surface_id(kind: str, module: str, callable_id: str) -> str:
    return hashlib.sha256(f"{kind}:{module}:{callable_id}".encode("utf-8")).hexdigest()[:16]


def preserved_status(previous: dict[str, object], section: str, item_id: str) -> str:
    record = previous.get(section, {}).get(item_id, {}) if isinstance(previous.get(section), dict) else {}
    return str(record.get("status")) if record.get("status") in STATUSES else "DISCOVERED"


def generate(previous: dict[str, object] | None = None) -> dict[str, object]:
    previous = previous or {}
    callables, errors = apply_classification_overrides(scan_modules(discover_modules()), load_classification_overrides())
    if errors:
        raise ValueError("; ".join(errors))
    api: dict[str, object] = {}
    lifecycle: dict[str, object] = {}
    stale: dict[str, object] = {}
    external: dict[str, object] = {}
    for item in callables:
        old_api = previous.get("api", {}).get(item.id, {}) if isinstance(previous.get("api"), dict) else {}
        obligations = {}
        if item.classification in MUTATOR_CLASSES:
            old_obligations = old_api.get("obligations", {}) if isinstance(old_api, dict) else {}
            obligations = {
                name: old_obligations.get(name, "DISCOVERED") if old_obligations.get(name) in STATUSES else "DISCOVERED"
                for name in ("preconditions", "success", "failure", "noop")
            }
        api[item.id] = {
            "module": item.module,
            "header": item.header,
            "scope": item.scope,
            "signature": item.signature,
            "classification": item.classification,
            "classification_status": "REVIEWED",
            "obligations": obligations,
        }
        if item.classification == "LIFECYCLE":
            identity = surface_id("lifecycle", item.module, item.id)
            lifecycle[identity] = {
                "module": item.module, "callable_id": item.id,
                "status": preserved_status(previous, "lifecycle", identity),
            }
        lowered = item.signature.lower()
        if item.classification in MUTATOR_CLASSES and any(token in lowered for token in ("id", "handle", "ref")):
            identity = surface_id("stale_identity", item.module, item.id)
            stale[identity] = {
                "module": item.module, "callable_id": item.id,
                "status": preserved_status(previous, "stale_identity", identity),
            }
        if item.classification == "CALLBACK" or any(token in lowered for token in ("callback", "backend", "port", "sink")):
            identity = surface_id("external_boundary", item.module, item.id)
            external[identity] = {
                "module": item.module, "callable_id": item.id,
                "status": preserved_status(previous, "external_boundary", identity),
            }
    defects = dict(previous.get("defects", {})) if isinstance(previous.get("defects"), dict) else {}
    defects["environment-defaulted-equality"] = {
        "module": "EngineFramework/GameplayWorldStateOwners/Environment",
        "status": "REVIEWED",
        "defect": "Defaulted environment equality was deleted because GameplayTagSet and GameplayContext had no equality operators.",
        "fix": "EngineFramework/BaseInfrastructure/Foundation/include/Epidemic/GameFramework/Foundation/tags.h:107::operator==(const GameplayTagSet&)",
        "fixes": [
            "EngineFramework/BaseInfrastructure/Foundation/include/Epidemic/GameFramework/Foundation/tags.h:107::operator==(const GameplayTagSet&)",
            "EngineFramework/BaseInfrastructure/Foundation/include/Epidemic/GameFramework/Foundation/gameplay_context.h:26::operator==(const GameplayContext&)",
        ],
        "regression": "EngineFramework/DevelopmentInfrastructure/Tests/environment_gameplay_tests.cpp:39-48::equality_hazard ==",
        "target": "EpidemicGameFrameworkEnvironmentTests",
    }
    return {
        "schema_version": 1,
        "api": dict(sorted(api.items())),
        "lifecycle": dict(sorted(lifecycle.items())),
        "stale_identity": dict(sorted(stale.items())),
        "external_boundary": dict(sorted(external.items())),
        "defects": defects,
    }


def validate(data: object) -> list[str]:
    if not isinstance(data, dict) or data.get("schema_version") != 1:
        return ["coverage manifest schema mismatch"]
    expected = generate(data)
    errors: list[str] = []
    for section in ("api", "lifecycle", "stale_identity", "external_boundary"):
        actual_section = data.get(section)
        if not isinstance(actual_section, dict) or set(actual_section) != set(expected[section]):
            errors.append(f"{section}: child inventory is incomplete or stale")
    api = data.get("api", {})
    if isinstance(api, dict):
        for item_id, record in api.items():
            if not isinstance(record, dict) or record.get("classification_status") != "REVIEWED":
                errors.append(f"api/{item_id}: classification is not REVIEWED")
            if isinstance(record, dict) and record.get("classification") == "UNCLASSIFIED":
                errors.append(f"api/{item_id}: UNCLASSIFIED is forbidden")
            for name, status in record.get("obligations", {}).items() if isinstance(record, dict) else []:
                if status not in STATUSES:
                    errors.append(f"api/{item_id}/{name}: invalid review status")
    for section in ("lifecycle", "stale_identity", "external_boundary", "defects"):
        records = data.get(section, {})
        if not isinstance(records, dict):
            errors.append(f"{section}: records must be an object")
            continue
        for item_id, record in records.items():
            if not isinstance(record, dict) or record.get("status") not in STATUSES:
                errors.append(f"{section}/{item_id}: invalid review status")
    return errors


def module_coverage_errors(data: dict[str, object], module: str, criterion: str) -> list[str]:
    mapping = {
        "API-PRECONDITIONS": ("api", "preconditions"),
        "API-SUCCESS": ("api", "success"),
        "API-FAILURE": ("api", "failure"),
        "STATE-NOOP": ("api", "noop"),
        "LIFE-ALLOWED": ("lifecycle", None),
        "LIFE-FORBIDDEN": ("lifecycle", None),
        "TEST-STALE": ("stale_identity", None),
        "TEST-CALLBACK-FAILURE": ("external_boundary", None),
        "TEST-REGRESSION": ("defects", None),
    }
    if criterion == "API-CLASSIFIED":
        pending = [item_id for item_id, item in data["api"].items() if item["module"] == module and item["classification_status"] != "REVIEWED"]
    elif criterion in mapping:
        section, obligation = mapping[criterion]
        records = data[section]
        if section == "api":
            pending = [
                item_id for item_id, item in records.items()
                if item["module"] == module and obligation in item["obligations"] and item["obligations"][obligation] != "REVIEWED"
            ]
        else:
            pending = [item_id for item_id, item in records.items() if item.get("module") == module and item.get("status") != "REVIEWED"]
    else:
        return []
    return [f"{module}/{criterion}: {len(pending)} child records remain DISCOVERED"] if pending else []


def self_test() -> list[str]:
    data = generate()
    errors: list[str] = []
    mutators = [(item_id, item) for item_id, item in data["api"].items() if item["obligations"]]
    if len(mutators) < 2:
        return ["fixture requires at least two mutators"]
    module = mutators[0][1]["module"]
    same_module = [(item_id, item) for item_id, item in mutators if item["module"] == module]
    if len(same_module) < 2:
        return ["fixture requires a module with at least two mutators"]
    first_id = same_module[0][0]
    data["api"][first_id]["obligations"]["success"] = "REVIEWED"
    if not module_coverage_errors(data, module, "API-SUCCESS"):
        errors.append("one reviewed mutator incorrectly satisfied a module-wide criterion")
    data["api"].pop(first_id)
    if not validate(data):
        errors.append("missing exact callable child was accepted")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate and validate quantified freeze child inventories.")
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        errors = self_test()
        if errors:
            print("\n".join(f"ERROR: {error}" for error in errors), file=sys.stderr)
            return 1
        print("PASS: incomplete per-item coverage cannot satisfy a module-wide criterion.")
        return 0
    previous = json.loads(OUTPUT.read_text(encoding="utf-8")) if OUTPUT.exists() else None
    generated = generate(previous)
    if args.check:
        if not OUTPUT.exists():
            print("ERROR: coverage manifest is missing", file=sys.stderr)
            return 1
        actual = json.loads(OUTPUT.read_text(encoding="utf-8"))
        errors = validate(actual)
        if actual != generated:
            errors.append("coverage manifest is stale")
        if errors:
            print("\n".join(f"ERROR: {error}" for error in errors), file=sys.stderr)
            return 1
        print("PASS: exact callable and per-surface child inventories are complete.")
        return 0
    OUTPUT.write_text(json.dumps(generated, indent=2, sort_keys=True) + "\n", encoding="utf-8", newline="\n")
    print(f"Wrote {OUTPUT.relative_to(ROOT)} with {len(generated['api'])} callable child records.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
