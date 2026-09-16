from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
WORKFLOW = ROOT / ".github" / "workflows" / "architecture-freeze.yml"
RUNNER = "windows-2025-vs2026"
CHECKOUT = "actions/checkout@v7.0.1"
SETUP_PYTHON = "actions/setup-python@v7.0.0"
PYTHON = "3.13.7"
EXPECTED_PROFILES = {
    "base-debug": ("OFF", "OFF", "Debug"),
    "base-release": ("OFF", "OFF", "Release"),
    "runtime-debug": ("ON", "OFF", "Debug"),
    "runtime-release": ("ON", "OFF", "Release"),
    "full-debug": ("ON", "ON", "Debug"),
    "full-release": ("ON", "ON", "Release"),
}
FREEZE_COMMANDS = (
    "python docs/freeze/ci_gate_contract.py --check",
    "python docs/freeze/ci_gate_contract.py --self-test",
    "cmake -P cmake/ArchitectureFreezeSelfTest.cmake",
    "python docs/freeze/module_dossiers.py --check",
    "python docs/freeze/module_dossiers.py --self-test",
    "python docs/freeze/architecture_ownership.py --check",
    "python docs/freeze/architecture_ownership.py --self-test",
    "python docs/freeze/coverage_manifests.py --check",
    "python docs/freeze/coverage_manifests.py --self-test",
    "python docs/freeze/ctest_manifest.py --check",
    "python docs/freeze/ctest_manifest.py --self-test",
    "python docs/freeze/local_ready_contract.py --check",
    "python docs/freeze/local_ready_contract.py --self-test",
    "python docs/freeze/public_api_inventory.py --check",
    "python docs/freeze/public_api_inventory.py --self-test",
    "python docs/freeze/public_surface_manifest.py --check",
    "python docs/freeze/public_surface_manifest.py --self-test",
)


def scalar(value: str) -> object:
    value = value.strip()
    if not value:
        return {}
    if value[0:1] == '"' and value[-1:] == '"':
        return json.loads(value)
    if value[0:1] == "'" and value[-1:] == "'":
        return value[1:-1]
    if value.lower() in {"true", "false"}:
        return value.lower() == "true"
    if value.lower() in {"null", "~"}:
        return None
    if value.isdigit():
        return int(value)
    return value


def yaml_lines(text: str) -> list[tuple[int, str]]:
    source = text.splitlines()
    result: list[tuple[int, str]] = []
    index = 0
    while index < len(source):
        raw = source[index]
        stripped = raw.strip()
        if not stripped or stripped.startswith("#"):
            index += 1
            continue
        indent = len(raw) - len(raw.lstrip(" "))
        if stripped.endswith((": |", ": >")):
            key = stripped.split(":", 1)[0]
            block: list[str] = []
            index += 1
            while index < len(source):
                child = source[index]
                child_indent = len(child) - len(child.lstrip(" "))
                if child.strip() and child_indent <= indent:
                    break
                if child.strip():
                    block.append(child[indent + 2 :].strip())
                index += 1
            result.append((indent, f"{key}: {json.dumps(chr(10).join(block))}"))
            continue
        result.append((indent, stripped))
        index += 1
    return result


def parse_block(lines: list[tuple[int, str]], index: int, indent: int) -> tuple[object, int]:
    is_list = lines[index][1].startswith("- ")
    container: object = [] if is_list else {}
    while index < len(lines) and lines[index][0] == indent:
        content = lines[index][1]
        if is_list:
            if not content.startswith("- "):
                break
            item_text = content[2:].strip()
            if ":" in item_text:
                key, rest = item_text.split(":", 1)
                item: dict[str, object] = {key.strip(): scalar(rest)}
                index += 1
                if index < len(lines) and lines[index][0] > indent:
                    child, index = parse_block(lines, index, lines[index][0])
                    if isinstance(child, dict):
                        item.update(child)
                container.append(item)
            else:
                container.append(scalar(item_text))
                index += 1
        else:
            if content.startswith("- ") or ":" not in content:
                break
            key, rest = content.split(":", 1)
            index += 1
            if rest.strip():
                container[key.strip()] = scalar(rest)
            elif index < len(lines) and lines[index][0] > indent:
                child, index = parse_block(lines, index, lines[index][0])
                container[key.strip()] = child
            else:
                container[key.strip()] = {}
    return container, index


def parse_yaml(text: str) -> dict[str, object]:
    lines = yaml_lines(text)
    if not lines:
        raise ValueError("empty workflow")
    document, index = parse_block(lines, 0, lines[0][0])
    if index != len(lines) or not isinstance(document, dict):
        raise ValueError("unsupported or malformed YAML structure")
    return document


def step_commands(job: dict[str, object]) -> list[str]:
    return [str(step.get("run", "")) for step in job.get("steps", []) if isinstance(step, dict)]


def validate_steps(job_name: str, job: dict[str, object]) -> list[str]:
    errors: list[str] = []
    if job.get("runs-on") != RUNNER:
        errors.append(f"{job_name}: runner must be pinned to {RUNNER}")
    if not isinstance(job.get("timeout-minutes"), int):
        errors.append(f"{job_name}: timeout-minutes is required")
    for step in job.get("steps", []):
        if not isinstance(step, dict):
            errors.append(f"{job_name}: malformed step")
            continue
        condition = step.get("if")
        if condition is False or (isinstance(condition, str) and condition.strip().lower() in {"false", "${{ false }}"}):
            errors.append(f"{job_name}: disabled step is forbidden")
        if step.get("continue-on-error") is True:
            errors.append(f"{job_name}: continue-on-error is forbidden")
        if "run" in step and step.get("shell") != "pwsh":
            errors.append(f"{job_name}: every run step must declare shell: pwsh")
    return errors


def validate_workflow(text: str) -> list[str]:
    try:
        workflow = parse_yaml(text)
    except (ValueError, IndexError) as error:
        return [f"workflow YAML parse failed: {error}"]
    errors: list[str] = []
    triggers = workflow.get("on")
    if not isinstance(triggers, dict) or set(triggers) != {"push", "pull_request"}:
        errors.append("CI must run on exactly push and pull_request")
    jobs = workflow.get("jobs")
    if not isinstance(jobs, dict) or set(jobs) != {"freeze-contract", "layer-matrix", "clang-public-surface"}:
        return errors + ["required CI job set mismatch"]
    for name, job in jobs.items():
        if not isinstance(job, dict):
            errors.append(f"{name}: job must be an object")
        else:
            errors.extend(validate_steps(name, job))

    freeze = jobs["freeze-contract"]
    freeze_steps = freeze.get("steps", [])
    uses = [step.get("uses") for step in freeze_steps if isinstance(step, dict) and "uses" in step]
    if uses != [CHECKOUT, SETUP_PYTHON]:
        errors.append("freeze-contract action versions mismatch")
    python_steps = [step for step in freeze_steps if isinstance(step, dict) and step.get("uses") == SETUP_PYTHON]
    if len(python_steps) != 1 or python_steps[0].get("with", {}).get("python-version") != PYTHON:
        errors.append("freeze-contract Python version is not pinned")
    commands = "\n".join(step_commands(freeze))
    for command in FREEZE_COMMANDS:
        if commands.count(command) != 1:
            errors.append(f"required freeze command must execute exactly once: {command}")

    matrix_job = jobs["layer-matrix"]
    if matrix_job.get("needs") != "freeze-contract":
        errors.append("layer-matrix must depend on freeze-contract")
    include = matrix_job.get("strategy", {}).get("matrix", {}).get("include", [])
    profiles = {
        item.get("profile"): (item.get("runtime"), item.get("framework"), item.get("configuration"))
        for item in include if isinstance(item, dict)
    }
    if profiles != EXPECTED_PROFILES:
        errors.append(f"CI profile matrix mismatch: {profiles}")
    matrix_commands = "\n".join(step_commands(matrix_job))
    for token in (
        '-G "Visual Studio 18 2026" -A x64',
        "-DEPIDEMIC_WARNINGS_AS_ERRORS=ON",
        "-DEPIDEMIC_ARCHITECTURE_FREEZE_CHECKS=ON",
        "--target EpidemicPublicHeaderSelfContainment",
        "ctest_manifest.py --check-profile ${{ matrix.profile }}",
        "ctest --test-dir build/${{ matrix.profile }} -C ${{ matrix.configuration }} --output-on-failure --timeout 300",
    ):
        if matrix_commands.count(token) != 1:
            errors.append(f"matrix command contract mismatch: {token}")

    clang = jobs["clang-public-surface"]
    if clang.get("needs") != "freeze-contract":
        errors.append("Clang profile must depend on freeze-contract")
    clang_commands = "\n".join(step_commands(clang))
    for token in ('-G "Visual Studio 18 2026" -A x64 -T ClangCL', "--target EpidemicPublicHeaderSelfContainment"):
        if clang_commands.count(token) != 1:
            errors.append(f"Clang public-surface command mismatch: {token}")
    return errors


def self_test(text: str) -> list[str]:
    failures: list[str] = []
    if validate_workflow(text):
        failures.append("current workflow was rejected")
    mutants = {
        "disabled freeze step": text.replace("shell: pwsh\n        run: cmake -P", "if: false\n        shell: pwsh\n        run: cmake -P", 1),
        "allowed failure": text.replace("name: Semantic CI contract", "name: Semantic CI contract\n        continue-on-error: true", 1),
        "missing trigger": text.replace("  pull_request:\n", "", 1),
        "floating runner": text.replace(RUNNER, "windows-latest", 1),
        "floating Python": text.replace(PYTHON, "3.x", 1),
        "missing job dependency": text.replace("    needs: freeze-contract\n", "", 1),
        "wrong shell": text.replace("shell: pwsh", "shell: cmd", 1),
        "missing release profile": text.replace("          - profile: base-release", "          - profile: removed-release", 1),
    }
    for name, mutant in mutants.items():
        if not validate_workflow(mutant):
            failures.append(f"negative workflow case was accepted: {name}")
    return failures


def main() -> int:
    parser = argparse.ArgumentParser(description="Semantically validate the architecture-freeze GitHub workflow.")
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
    print("PASS: semantic YAML CI contract, pinned tools, six profiles and adversarial workflow fixtures.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
