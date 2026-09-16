from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path

sys.dont_write_bytecode = True

from module_dossiers import ROOT, discover_modules


OUTPUT = ROOT / "docs" / "freeze" / "public_surface_manifest.json"


def clean(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    return re.sub(r"//[^\n]*", " ", text)


def normalized_hash(text: str) -> str:
    return hashlib.sha256(re.sub(r"\s+", " ", clean(text)).strip().encode("utf-8")).hexdigest()


def declarations(text: str) -> dict[str, list[str]]:
    source = clean(text)
    classes = sorted(set(
        f"{kind} {name}{re.sub(r'\s+', ' ', bases).strip()}"
        for kind, name, bases in re.findall(
            r"(?<!enum )\b(class|struct)\s+(?:[A-Z][A-Z0-9_]*\s+)?([A-Za-z_]\w*)\s*([^;{]*)(?=\{|;)", source,
        )
    ))
    enums: list[str] = []
    enum_values: list[str] = []
    for match in re.finditer(r"\benum\s+(?:class\s+)?([A-Za-z_]\w*)[^\{;]*\{([^}]*)\}", source, flags=re.S):
        enums.append(match.group(1))
        for value in match.group(2).split(","):
            name = re.match(r"\s*([A-Za-z_]\w*)", value)
            if name:
                enum_values.append(f"{match.group(1)}::{name.group(1)}")
    aliases = sorted(set(
        [f"using {name}" for name in re.findall(r"\busing\s+([A-Za-z_]\w*)\s*=", source)]
        + [f"typedef {name}" for name in re.findall(r"\btypedef\s+[^;]+\s+([A-Za-z_]\w*)\s*;", source)]
    ))
    constants = sorted(set(
        name for name in re.findall(
            r"\b(?:inline\s+)?(?:static\s+)?constexpr\s+[^;(){}=]+\s+([A-Za-z_]\w*)\s*(?:=|\{)", source,
        )
    ))
    templates = sorted(set(re.sub(r"\s+", " ", item).strip() for item in re.findall(r"template\s*<[^;{}]+>", source)))
    constraints = sorted(set(re.sub(r"\s+", " ", item).strip() for item in re.findall(r"\brequires\s+[^;{]+", source)))
    public_fields: list[str] = []
    for block in re.finditer(r"\b(struct|class)\s+([A-Za-z_]\w*)[^;{]*\{([^{}]*)\}\s*;", source, flags=re.S):
        public = block.group(1) == "struct"
        for raw in block.group(3).splitlines():
            line = raw.strip()
            access = re.fullmatch(r"(public|protected|private)\s*:", line)
            if access:
                public = access.group(1) == "public"
                continue
            if not public or "(" in line or not line.endswith(";") or line.startswith(("using ", "typedef ", "static_assert")):
                continue
            field = re.search(r"([A-Za-z_]\w*)\s*(?:\{[^;]*\}|=[^;]*)?;\s*$", line)
            if field:
                public_fields.append(f"{block.group(2)}::{field.group(1)}")
    return {
        "classes_and_inheritance": classes,
        "enums": sorted(set(enums)),
        "enum_values": sorted(set(enum_values)),
        "aliases": aliases,
        "constants": constants,
        "public_fields": sorted(set(public_fields)),
        "templates": templates,
        "constraints": constraints,
    }


def generate() -> dict[str, object]:
    headers: dict[str, object] = {}
    for module in discover_modules():
        for header in module.headers:
            relative = header.relative_to(ROOT).as_posix()
            text = header.read_text(encoding="utf-8", errors="ignore")
            headers[relative] = {
                "module": module.key,
                "normalized_sha256": normalized_hash(text),
                **declarations(text),
            }
    return {
        "schema_version": 1,
        "compatibility_promise": "reviewed C++ source/API compatibility; no general C++ binary ABI promise",
        "compiler_validation": "Every header is compiled by per-target MSVC and ClangCL consumers in CI.",
        "headers": dict(sorted(headers.items())),
    }


def validate(data: object) -> list[str]:
    if not isinstance(data, dict) or data.get("schema_version") != 1:
        return ["public surface manifest schema mismatch"]
    headers = data.get("headers")
    if not isinstance(headers, dict) or len(headers) != 232:
        return ["public surface manifest must contain all 232 public headers"]
    errors: list[str] = []
    required = {
        "module", "normalized_sha256", "classes_and_inheritance", "enums", "enum_values", "aliases",
        "constants", "public_fields", "templates", "constraints",
    }
    for header, record in headers.items():
        if not isinstance(record, dict) or set(record) != required:
            errors.append(f"{header}: non-callable surface fields mismatch")
    return errors


def self_test() -> list[str]:
    fixture = """template <typename T> requires Value<T>\nstruct Base {};\nstruct Item : Base
{
    int value;
  private:
    int hidden;
};
enum class Mode { First = 1, Second };\nusing Alias = Item;\ninline constexpr int Limit = 4;\n"""
    found = declarations(fixture)
    errors: list[str] = []
    for expected, section in (
        ("struct Item: Base", "classes_and_inheritance"), ("Mode::Second", "enum_values"),
        ("using Alias", "aliases"), ("Limit", "constants"), ("Item::value", "public_fields"),
    ):
        if expected not in found[section]:
            errors.append(f"fixture missed {section}: {expected}")
    broken = generate()
    broken["headers"].pop(next(iter(broken["headers"])))
    if not validate(broken):
        errors.append("missing public header was accepted")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description="Freeze non-callable public C++ source contracts.")
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        errors = self_test()
        if errors:
            print("\n".join(f"ERROR: {error}" for error in errors), file=sys.stderr)
            return 1
        print("PASS: non-callable structs, inheritance, fields, enums, aliases, constants and templates are inventoried.")
        return 0
    generated = generate()
    errors = validate(generated)
    if errors:
        print("\n".join(f"ERROR: {error}" for error in errors), file=sys.stderr)
        return 1
    if args.check:
        if not OUTPUT.is_file() or json.loads(OUTPUT.read_text(encoding="utf-8")) != generated:
            print("ERROR: public surface manifest is missing or stale", file=sys.stderr)
            return 1
        print("PASS: 232 public-header hashes and non-callable source contracts match.")
        return 0
    OUTPUT.write_text(json.dumps(generated, indent=2, sort_keys=True) + "\n", encoding="utf-8", newline="\n")
    print(f"Wrote {OUTPUT.relative_to(ROOT)} with 232 public-header records.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
