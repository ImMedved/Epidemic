from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
WORKFLOW = ROOT / ".github" / "workflows" / "architecture-freeze.yml"
EXPECTED_PROFILES = {
    "base": ("OFF", "OFF", "Debug"),
    "runtime": ("ON", "OFF", "Debug"),
    "full": ("ON", "ON", "Debug"),
    "full-release": ("ON", "ON", "Release"),
}
PROFILE_RE = re.compile(
    r"- name:\s*([^\s]+)\s+runtime:\s*(ON|OFF)\s+framework:\s*(ON|OFF)\s+configuration:\s*(Debug|Release)",
    re.M,
)
REQUIRED_COMMANDS = (
    "python docs/freeze/ci_gate_contract.py --check",
    "python docs/freeze/ci_gate_contract.py --self-test",
    "python docs/freeze/module_dossiers.py --check",
    "python docs/freeze/module_dossiers.py --self-test",
    "python docs/freeze/architecture_ownership.py --check",
    "python docs/freeze/architecture_ownership.py --self-test",
    "python docs/freeze/local_ready_contract.py --check",
    "python docs/freeze/local_ready_contract.py --self-test",
    "python docs/freeze/public_api_inventory.py --check",
    "python docs/freeze/public_api_inventory.py --self-test",
    "cmake -P cmake/ArchitectureFreezeSelfTest.cmake",
    "-DEPIDEMIC_WARNINGS_AS_ERRORS=ON",
    "-DEPIDEMIC_ARCHITECTURE_FREEZE_CHECKS=ON",
    "--target EpidemicPublicHeaderSelfContainment",
    "ctest --test-dir build/${{ matrix.name }} -C ${{ matrix.configuration }} --output-on-failure",
)


def validate_workflow(text: str) -> list[str]:
    errors: list[str] = []
    profiles = {name: (runtime, framework, configuration) for name, runtime, framework, configuration in PROFILE_RE.findall(text)}
    if profiles != EXPECTED_PROFILES:
        errors.append(f"CI profile matrix mismatch: {profiles}")
    if not re.search(r"(?m)^on:\s*$", text) or not re.search(r"(?m)^\s+push:\s*$", text) or not re.search(r"(?m)^\s+pull_request:\s*$", text):
        errors.append("CI must run on push and pull_request")
    if "continue-on-error: true" in text.lower():
        errors.append("freeze gate must not allow failures")
    for command in REQUIRED_COMMANDS:
        if text.count(command) != 1:
            errors.append(f"required CI command must occur exactly once: {command}")
    return errors


def self_test(text: str) -> list[str]:
    failures: list[str] = []
    if validate_workflow(text):
        failures.append("current workflow was rejected")
    mutants = {
        "missing profile": text.replace("          - name: base", "          - name: removed-base", 1),
        "wrong profile option": text.replace("            runtime: OFF", "            runtime: ON", 1),
        "allowed failure": text + "\ncontinue-on-error: true\n",
        "missing pull request trigger": text.replace("  pull_request:\n", "", 1),
    }
    for command in REQUIRED_COMMANDS:
        mutants[f"missing command {command}"] = text.replace(command, "removed-command", 1)
    for name, mutant in mutants.items():
        if not validate_workflow(mutant):
            failures.append(f"negative workflow case was accepted: {name}")
    return failures


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate the architecture-freeze CI workflow contract.")
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if not WORKFLOW.is_file():
        print("ERROR: architecture-freeze workflow is missing", file=sys.stderr)
        return 1
    text = WORKFLOW.read_text(encoding="utf-8")
    errors = self_test(text) if args.self_test else validate_workflow(text)
    if errors:
        print("\n".join(f"ERROR: {error}" for error in errors), file=sys.stderr)
        return 1
    if args.self_test:
        print(f"PASS: CI contract rejected all {len(REQUIRED_COMMANDS) + 4} malformed workflow fixtures.")
    else:
        print("PASS: CI matrix, triggers, warning policy and all freeze commands match the local contract.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
