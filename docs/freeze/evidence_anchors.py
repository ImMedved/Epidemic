from __future__ import annotations

import re
from pathlib import Path


ANCHOR_RE = re.compile(r"^(?P<path>[^\s:]+)(?::(?P<line>\d+))?::(?P<symbol>\S.+)$")
ADD_TEST_RE = re.compile(r"add_test\s*\(\s*(?:NAME\s+)?([^\s\)]+)", re.I)


def validate_path_anchor(root: Path, value: object) -> list[str]:
    if not isinstance(value, str):
        return ["anchor must be a string"]
    match = ANCHOR_RE.fullmatch(value)
    if not match:
        return ["malformed path[:line]::symbol anchor"]

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

    text = path.read_text(encoding="utf-8", errors="ignore")
    lines = text.splitlines()
    line = match.group("line")
    if line is not None and (int(line) < 1 or int(line) > len(lines)):
        return ["anchor line does not exist"]
    if match.group("symbol") not in text:
        return ["anchor symbol does not exist in the referenced file"]
    return []


def registered_test_targets(root: Path) -> set[str]:
    targets: set[str] = set()
    for cmake in root.rglob("CMakeLists.txt"):
        relative = cmake.relative_to(root)
        if relative.parts and (relative.parts[0] == "build" or relative.parts[0].startswith("build-")):
            continue
        text = cmake.read_text(encoding="utf-8", errors="ignore")
        text = re.sub(r"(?m)^[ \t]*#.*$", "", text)
        targets.update(ADD_TEST_RE.findall(text))
    return targets


def validate_test_evidence(root: Path, evidence: dict[str, object], targets: set[str]) -> list[str]:
    errors: list[str] = []
    target = evidence.get("target")
    if not isinstance(target, str) or not target:
        errors.append("test evidence requires a target")
    elif target not in targets:
        errors.append(f"test target is not registered with CTest: {target}")

    assertions = evidence.get("assertions")
    if not isinstance(assertions, list) or not assertions:
        errors.append("test evidence requires a non-empty assertions list")
    else:
        for index, anchor in enumerate(assertions):
            errors.extend(f"assertions[{index}]: {error}" for error in validate_path_anchor(root, anchor))
    return errors
