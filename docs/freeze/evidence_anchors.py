from __future__ import annotations

import json
import re
from pathlib import Path


ANCHOR_RE = re.compile(
    r"^(?P<path>[^\s:]+):(?P<start>\d+)(?:-(?P<end>\d+))?::(?P<symbol>\S.+)$"
)


def validate_path_anchor(root: Path, value: object) -> list[str]:
    if not isinstance(value, str):
        return ["anchor must be a string"]
    match = ANCHOR_RE.fullmatch(value)
    if not match:
        return ["malformed path:line[-line]::symbol anchor"]

    relative = Path(match.group("path"))
    if relative.is_absolute() or ".." in relative.parts:
        return ["anchor path must be repository-relative"]

    repository = root.resolve()
    path = (repository / relative).resolve()
    try:
        path.relative_to(repository)
    except ValueError:
        return ["anchor path escapes the repository"]
    if not path.is_file():
        return ["anchor file does not exist"]

    lines = path.read_text(encoding="utf-8", errors="ignore").splitlines()
    start = int(match.group("start"))
    end = int(match.group("end") or start)
    if start < 1 or end < start or end > len(lines):
        return ["anchor line or range does not exist"]
    selected = "\n".join(lines[start - 1 : end])
    if match.group("symbol") not in selected:
        return ["anchor symbol does not exist on the referenced line or range"]
    return []


def registered_test_targets(root: Path) -> set[str]:
    manifest = root / "docs" / "freeze" / "ctest_manifest.json"
    if not manifest.is_file():
        return set()
    try:
        data = json.loads(manifest.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return set()
    if data.get("schema_version") != 1 or not isinstance(data.get("profiles"), dict):
        return set()
    targets: set[str] = set()
    for profile in data["profiles"].values():
        if not isinstance(profile, dict) or not isinstance(profile.get("tests"), list):
            continue
        for test in profile["tests"]:
            if isinstance(test, dict) and isinstance(test.get("name"), str):
                targets.add(test["name"])
    return targets


def validate_test_evidence(root: Path, evidence: dict[str, object], targets: set[str]) -> list[str]:
    errors: list[str] = []
    target = evidence.get("target")
    if not isinstance(target, str) or not target:
        errors.append("test evidence requires a target")
    elif target not in targets:
        errors.append(f"test target is not present in the configured CTest manifest: {target}")

    assertions = evidence.get("assertions")
    if not isinstance(assertions, list) or not assertions:
        errors.append("test evidence requires a non-empty assertions list")
    else:
        for index, anchor in enumerate(assertions):
            errors.extend(f"assertions[{index}]: {error}" for error in validate_path_anchor(root, anchor))
    return errors
