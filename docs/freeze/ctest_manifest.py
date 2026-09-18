from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MANIFEST = ROOT / "docs" / "freeze" / "ctest_manifest.json"
EXPECTED_PROFILES = {
    "base-debug": {"runtime": False, "framework": False, "configuration": "Debug", "count": 10},
    "base-release": {"runtime": False, "framework": False, "configuration": "Release", "count": 10},
    "runtime-debug": {"runtime": True, "framework": False, "configuration": "Debug", "count": 30},
    "runtime-release": {"runtime": True, "framework": False, "configuration": "Release", "count": 30},
    "full-debug": {"runtime": True, "framework": True, "configuration": "Debug", "count": 90},
    "full-release": {"runtime": True, "framework": True, "configuration": "Release", "count": 90},
}


def normalize_tests(document: dict[str, object]) -> list[dict[str, object]]:
    tests: list[dict[str, object]] = []
    for test in document.get("tests", []):
        if not isinstance(test, dict) or not isinstance(test.get("name"), str):
            continue
        labels: list[str] = []
        timeout: float | int | None = None
        for prop in test.get("properties", []):
            if not isinstance(prop, dict):
                continue
            if prop.get("name") == "LABELS":
                value = prop.get("value", [])
                labels = sorted(value if isinstance(value, list) else [str(value)])
            elif prop.get("name") == "TIMEOUT" and isinstance(prop.get("value"), (int, float)):
                timeout = prop["value"]
        tests.append({"name": test["name"], "labels": labels, "timeout": timeout})
    return sorted(tests, key=lambda item: str(item["name"]))


def configured_tests(build_dir: Path, configuration: str, ctest: str) -> list[dict[str, object]]:
    process = subprocess.run(
        [ctest, "--test-dir", str(build_dir), "-C", configuration, "--show-only=json-v1"],
        cwd=ROOT,
        check=False,
        capture_output=True,
        text=True,
        encoding="utf-8",
    )
    if process.returncode != 0:
        raise RuntimeError(process.stdout + process.stderr)
    return normalize_tests(json.loads(process.stdout))


def validate_manifest(data: object) -> list[str]:
    if not isinstance(data, dict) or data.get("schema_version") != 1:
        return ["CTest manifest schema mismatch"]
    profiles = data.get("profiles")
    if not isinstance(profiles, dict) or set(profiles) != set(EXPECTED_PROFILES):
        return ["CTest manifest profile set mismatch"]
    errors: list[str] = []
    for name, expected in EXPECTED_PROFILES.items():
        profile = profiles.get(name)
        if not isinstance(profile, dict):
            errors.append(f"{name}: profile record is not an object")
            continue
        for key in ("runtime", "framework", "configuration"):
            if profile.get(key) != expected[key]:
                errors.append(f"{name}: {key} mismatch")
        tests = profile.get("tests")
        if not isinstance(tests, list) or len(tests) != expected["count"]:
            errors.append(f"{name}: expected {expected['count']} tests")
            continue
        if len({item.get("name") for item in tests if isinstance(item, dict)}) != len(tests):
            errors.append(f"{name}: duplicate or malformed test names")
        if any(not isinstance(item, dict) or not isinstance(item.get("labels"), list) for item in tests):
            errors.append(f"{name}: test labels are missing")
        if any(not isinstance(item, dict) or not isinstance(item.get("timeout"), (int, float)) for item in tests):
            errors.append(f"{name}: every test must have an explicit timeout")
    return errors


def self_test() -> list[str]:
    sample = {
        "tests": [
            {"name": "Active", "properties": [{"name": "LABELS", "value": ["unit"]}, {"name": "TIMEOUT", "value": 30.0}]}
        ]
    }
    errors: list[str] = []
    if normalize_tests(sample) != [{"name": "Active", "labels": ["unit"], "timeout": 30.0}]:
        errors.append("CTest JSON normalization failed")
    invalid = {"schema_version": 1, "profiles": {}}
    if not validate_manifest(invalid):
        errors.append("incomplete CTest manifest was accepted")
    without_timeout = {
        "schema_version": 1,
        "profiles": {
            name: {
                "runtime": expected["runtime"],
                "framework": expected["framework"],
                "configuration": expected["configuration"],
                "tests": [{"name": f"test-{index}", "labels": [], "timeout": None} for index in range(expected["count"])],
            }
            for name, expected in EXPECTED_PROFILES.items()
        },
    }
    if not validate_manifest(without_timeout):
        errors.append("tests without explicit timeout were accepted")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description="Record and validate configured CTest manifests.")
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--record", metavar="PROFILE")
    parser.add_argument("--check-profile", metavar="PROFILE")
    parser.add_argument("--build-dir", type=Path)
    parser.add_argument("--configuration")
    parser.add_argument("--ctest", default=shutil.which("ctest") or "ctest")
    args = parser.parse_args()

    if args.self_test:
        errors = self_test()
        if errors:
            print("\n".join(f"ERROR: {error}" for error in errors), file=sys.stderr)
            return 1
        print("PASS: CTest manifest rejects missing profiles, missing tests and missing timeouts.")
        return 0

    if args.record:
        if args.record not in EXPECTED_PROFILES or not args.build_dir or not args.configuration:
            parser.error("--record requires a known profile, --build-dir and --configuration")
        data = json.loads(MANIFEST.read_text(encoding="utf-8")) if MANIFEST.exists() else {
            "schema_version": 1, "profiles": {},
        }
        expected = EXPECTED_PROFILES[args.record]
        data["profiles"][args.record] = {
            "runtime": expected["runtime"],
            "framework": expected["framework"],
            "configuration": expected["configuration"],
            "tests": configured_tests(args.build_dir, args.configuration, args.ctest),
        }
        MANIFEST.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n", encoding="utf-8", newline="\n")
        print(f"Recorded configured CTest profile {args.record}.")
        return 0

    if not MANIFEST.is_file():
        print("ERROR: CTest manifest is missing", file=sys.stderr)
        return 1
    data = json.loads(MANIFEST.read_text(encoding="utf-8"))
    errors = validate_manifest(data)
    if args.check_profile:
        if args.check_profile not in EXPECTED_PROFILES or not args.build_dir or not args.configuration:
            parser.error("--check-profile requires a known profile, --build-dir and --configuration")
        actual = configured_tests(args.build_dir, args.configuration, args.ctest)
        recorded = data.get("profiles", {}).get(args.check_profile, {}).get("tests")
        if actual != recorded:
            errors.append(f"{args.check_profile}: configured CTest set/count/labels/timeouts differ from manifest")
    if errors:
        print("\n".join(f"ERROR: {error}" for error in errors), file=sys.stderr)
        return 1
    print("PASS: exact configured CTest profile manifest matches.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
