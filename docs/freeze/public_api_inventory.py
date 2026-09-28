from __future__ import annotations

import argparse
import copy
import hashlib
import json
import re
import sys
from collections import Counter
from dataclasses import dataclass
from pathlib import Path

sys.dont_write_bytecode = True

from module_dossiers import ROOT, Module, discover_modules, first_word
from evidence_anchors import registered_test_targets, validate_path_anchor, validate_test_evidence


OUTPUT = ROOT / "docs" / "freeze" / "public_api_inventory.md"
ANCHORS = ROOT / "docs" / "freeze" / "public_api_anchors.json"
ORACLE = ROOT / "docs" / "freeze" / "fixtures" / "public_api_oracle.json"
CLASSIFICATION_OVERRIDES = ROOT / "docs" / "freeze" / "public_api_classification_overrides.json"

MUTATION_PREFIXES = {
    "accept", "acknowledge", "acquire", "activate", "add", "advance", "allocate", "apply", "assign", "attach", "bind",
    "begin", "bootstrap", "cancel", "capture", "change", "claim", "clear", "close", "commit", "compact", "complete", "continue",
    "configure", "connect", "consume", "create", "deallocate", "defer", "dematerialize",
    "declare", "deliver", "demote", "destroy", "detach", "disable", "disconnect", "dispatch", "drain", "emit", "emplace", "end",
    "enable", "enqueue", "equip", "execute", "expire", "freeze", "generate", "grant", "initialize", "insert",
    "join", "learn", "load", "log", "mark", "materialize", "merge", "move", "open", "pause", "place", "play", "post", "prepare", "present", "prune",
    "process", "publish", "pump", "push", "queue", "reconcile", "record", "register", "release", "reschedule",
    "remove", "request", "reserve", "reset", "resize", "restore", "resume", "rollback", "serialize", "share", "show", "simulate", "spawn", "synchronize",
    "run", "save", "schedule", "seal", "set", "shutdown", "skip", "split", "stage", "satisfy",
    "start", "stop", "submit", "subscribe", "tick", "transfer", "unload", "unregister", "unsubscribe",
    "update", "upsert", "wait", "write", "increment", "decrement", "deserialize", "contradict", "decay",
}
LIFECYCLE_PREFIXES = {
    "activate", "bootstrap", "close", "connect", "deactivate", "disable", "disconnect",
    "enable", "freeze", "initialize", "open", "pause", "resume", "shutdown", "start", "stop",
}
QUERY_PREFIXES = {
    "can", "contains", "count", "current", "describe", "diagnostics", "empty", "find", "get",
    "has", "is", "latest", "list", "lookup", "make", "peek", "query", "read", "resolve",
    "size", "snapshot", "tryget", "validate", "worker",
}
FACTORY_PREFIXES = {"build", "create", "failure", "from", "make", "parse", "succeed", "success"}
COMMAND_PREFIXES = {"debug", "error", "fatal", "info", "trace", "warn"}


@dataclass(frozen=True)
class Callable:
    id: str
    module: str
    header: str
    line: int
    scope: str
    name: str
    signature: str
    classification: str


@dataclass
class Scope:
    kind: str
    name: str
    body_depth: int
    public: bool = True
    visible: bool = True


def mask_comments(text: str) -> str:
    def block(match: re.Match[str]) -> str:
        value = match.group(0)
        return "".join("\n" if char == "\n" else " " for char in value)

    text = re.sub(r"/\*.*?\*/", block, text, flags=re.S)
    return re.sub(r"//[^\n]*", lambda match: " " * len(match.group(0)), text)


def mask_preprocessor(text: str) -> str:
    lines = text.splitlines()
    masked: list[str] = []
    continuing = False
    for line in lines:
        directive = continuing or line.lstrip().startswith("#")
        masked.append(" " * len(line) if directive else line)
        continuing = directive and line.rstrip().endswith("\\")
    return "\n".join(masked)


def local_struct_macros(text: str) -> dict[str, tuple[list[str], str]]:
    lines = text.splitlines()
    macros: dict[str, tuple[list[str], str]] = {}
    index = 0
    while index < len(lines):
        match = re.match(r"\s*#\s*define\s+([A-Za-z_]\w*)\s*\(([^)]*)\)\s*(.*)", lines[index])
        if not match:
            index += 1
            continue
        body_parts = [match.group(3).rstrip().removesuffix("\\")]
        while lines[index].rstrip().endswith("\\") and index + 1 < len(lines):
            index += 1
            body_parts.append(lines[index].rstrip().removesuffix("\\"))
        body = "\n".join(body_parts)
        if re.search(r"\b(?:class|struct)\b", body):
            params = [part.strip() for part in match.group(2).split(",") if part.strip()]
            macros[match.group(1)] = (params, body)
        index += 1
    return macros


def normalize_signature(value: str) -> str:
    value = re.sub(r"\s+", " ", value).strip()
    value = re.sub(r"\s*([(),;={}&*])\s*", r"\1", value)
    return value


def matching_paren(value: str, opening: int) -> int:
    depth = 0
    for index in range(opening, len(value)):
        if value[index] == "(":
            depth += 1
        elif value[index] == ")":
            depth -= 1
            if depth == 0:
                return index
    return -1


def parameter_opening(value: str) -> int:
    value_without_attributes = re.sub(r"\[\[.*?\]\]", lambda match: " " * len(match.group(0)), value)
    operator = re.search(r"operator\s*(?:\(\)|\[\])\s*(\()", value_without_attributes)
    if operator:
        return operator.start(1)
    angle = 0
    square = 0
    for index, char in enumerate(value_without_attributes):
        if char == "<":
            angle += 1
        elif char == ">" and angle:
            angle -= 1
        elif char == "[":
            square += 1
        elif char == "]" and square:
            square -= 1
        elif char == "(" and angle == 0 and square == 0:
            return index
    return -1


def callable_name(before: str) -> str:
    operator = re.search(r"(operator\s*(?:\(\)|\[\]|[^\s(]+|[A-Za-z_:][A-Za-z0-9_:<>]*))\s*$", before)
    if operator:
        return re.sub(r"\s+", "", operator.group(1))
    match = re.search(r"(~?[A-Za-z_]\w*)\s*$", before)
    return match.group(1) if match else ""


def classify(name: str, scope: str, signature: str, is_member: bool) -> str:
    class_name = scope.rsplit("::", 1)[-1] if scope else ""
    if is_member and name == class_name:
        return "CONSTRUCTOR"
    if is_member and name == f"~{class_name}":
        return "DESTRUCTOR"
    suffix = signature[signature.find("(") :]
    is_const = bool(re.search(r"\)const\b", suffix))
    is_static = bool(re.search(r"(?:^|\s)static(?:\s|$)", signature))
    if name.startswith("operator"):
        assignment = {
            "operator=", "operator+=", "operator-=", "operator*=", "operator/=", "operator%=",
            "operator&=", "operator|=", "operator^=", "operator<<=", "operator>>=", "operator++", "operator--",
        }
        if name in assignment:
            return "MUTATOR"
        if not is_member or is_const:
            return "QUERY"
        return "UNCLASSIFIED"
    prefix = first_word(name)
    if prefix in FACTORY_PREFIXES and (is_static or not is_member):
        return "FACTORY"
    if prefix == "on":
        return "CALLBACK"
    if is_member and is_const:
        return "QUERY"
    if prefix in LIFECYCLE_PREFIXES:
        return "LIFECYCLE"
    if prefix in MUTATION_PREFIXES or prefix in COMMAND_PREFIXES:
        return "MUTATOR"
    if prefix in QUERY_PREFIXES:
        return "QUERY"
    return "UNCLASSIFIED"


def declaration_body(value: str, closing: int) -> int:
    paren = square = brace = 0
    quote = ""
    escaped = False
    for index in range(closing + 1, len(value)):
        char = value[index]
        if quote:
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == quote:
                quote = ""
            continue
        if char in {'"', "'"}:
            quote = char
        elif char == "(":
            paren += 1
        elif char == ")" and paren:
            paren -= 1
        elif char == "[":
            square += 1
        elif char == "]" and square:
            square -= 1
        elif char == "{" and paren == 0 and square == 0 and brace == 0:
            suffix = value[closing + 1 : index]
            if re.search(r"\brequires\s*(?:\([^{}]*\))?\s*$", suffix):
                brace = 1
            else:
                return index
        elif char == "{":
            brace += 1
        elif char == "}" and brace:
            brace -= 1
    return -1


def declaration_semicolon(value: str, closing: int) -> int:
    paren = square = brace = 0
    for index in range(closing + 1, len(value)):
        char = value[index]
        if char == "(":
            paren += 1
        elif char == ")" and paren:
            paren -= 1
        elif char == "[":
            square += 1
        elif char == "]" and square:
            square -= 1
        elif char == "{":
            brace += 1
        elif char == "}" and brace:
            brace -= 1
        elif char == ";" and paren == 0 and square == 0 and brace == 0:
            return index
    return -1


def declaration_signature(value: str) -> str:
    opening = parameter_opening(value)
    if opening < 0:
        return normalize_signature(value)
    closing = matching_paren(value, opening)
    if closing < 0:
        return normalize_signature(value)
    body = declaration_body(value, closing)
    semicolon = declaration_semicolon(value, closing)
    terminals = [position for position in (body, semicolon + 1 if semicolon >= 0 else -1) if position >= 0]
    end = min(terminals) if terminals else len(value)
    signature = value[:end].strip()
    initializer = re.search(r"(?<!:):(?!:)", signature[closing + 1 :])
    if initializer:
        signature = signature[: closing + 1 + initializer.start()].strip()
    return normalize_signature(signature)


def callable_identity(module: Module, relative: str, qualified: str, signature: str) -> str:
    return f"{module.key}:{relative}::{qualified}::{signature}"


def parse_declaration(
    value: str, module: Module, header: Path, line: int, scope: str, is_member: bool,
) -> Callable | None:
    signature = declaration_signature(value)
    if not signature or signature.startswith(("#", ":", "using ", "typedef ", "friend ", "enum ", "class ", "struct ")):
        return None
    opening = parameter_opening(signature)
    if opening < 0:
        return None
    closing = matching_paren(signature, opening)
    if closing < 0:
        return None
    before = signature[:opening].strip()
    name = callable_name(before)
    if not name or name in {"if", "for", "while", "switch", "return", "sizeof", "alignof", "requires", "static_assert"}:
        return None
    if ("=" in before and not name.startswith("operator")) or before.endswith("*") or before.endswith("&"):
        return None
    relative = header.relative_to(ROOT).as_posix()
    qualified = f"{scope}::{name}" if scope else name
    identity = callable_identity(module, relative, qualified, signature)
    callable_id = hashlib.sha256(identity.encode("utf-8")).hexdigest()[:16]
    return Callable(
        callable_id, module.key, relative, line, scope, name, signature,
        classify(name, scope, signature, is_member),
    )


def scan_text(module: Module, header: Path, text: str) -> list[Callable]:
    scopes: list[Scope] = []
    brace_depth = 0
    pending = ""
    pending_line = 0
    pending_context = ""
    pending_scope: tuple[str, str, bool, bool] | None = None
    found: list[Callable] = []

    for line_number, raw in enumerate(text.splitlines(), 1):
        stripped = raw.strip()
        while scopes and brace_depth < scopes[-1].body_depth:
            scopes.pop()
        classes = [scope for scope in scopes if scope.kind == "class"]
        current_class = classes[-1] if classes else None

        inline_class = re.search(
            r"\b(?:class|struct)\s+([A-Za-z_]\w*)(?:\s*<[^{}]+>)?\s*\{(.*)\}\s*;\s*$", stripped,
        )
        if inline_class and (current_class is None or (current_class.visible and current_class.public)):
            wrapper = f"struct {inline_class.group(1)}\n{{\npublic:\n{inline_class.group(2)}\n}};"
            outer = "::".join(scope.name for scope in scopes if scope.kind in {"namespace", "class"})
            for item in scan_text(module, header, wrapper):
                scope = f"{outer}::{item.scope}" if outer else item.scope
                qualified = f"{scope}::{item.name}" if scope else item.name
                identity = callable_identity(module, item.header, qualified, item.signature)
                found.append(Callable(
                    hashlib.sha256(identity.encode("utf-8")).hexdigest()[:16], item.module, item.header,
                    line_number, scope, item.name, item.signature, item.classification,
                ))
            brace_depth += raw.count("{") - raw.count("}")
            continue

        if current_class and brace_depth == current_class.body_depth and stripped in {"public:", "protected:", "private:"}:
            current_class.public = stripped == "public:"
            pending = ""
            brace_depth += raw.count("{") - raw.count("}")
            continue

        namespace_match = re.search(r"\bnamespace\s+([A-Za-z_]\w*(?:::[A-Za-z_]\w*)*)", stripped)
        class_match = re.search(r"\b(class|struct)\s+(?:[A-Z][A-Z0-9_]*\s+)?([A-Za-z_]\w*)", stripped)
        opening_scope = False
        if pending_scope and "{" in stripped:
            kind, name, public, visible = pending_scope
            scopes.append(Scope(kind, name, brace_depth + 1, public=public, visible=visible))
            pending_scope = None
            opening_scope = True
        elif namespace_match and not class_match and ";" not in stripped:
            if "{" in stripped:
                scopes.append(Scope("namespace", namespace_match.group(1), brace_depth + 1))
            else:
                pending_scope = ("namespace", namespace_match.group(1), True, True)
            opening_scope = True
        elif class_match and ";" not in stripped:
            parent_visible = not current_class or (current_class.visible and current_class.public)
            class_public = class_match.group(1) == "struct"
            if "{" in stripped:
                scopes.append(Scope(
                    "class", class_match.group(2), brace_depth + 1,
                    public=class_public, visible=parent_visible,
                ))
            else:
                pending_scope = ("class", class_match.group(2), class_public, parent_visible)
            opening_scope = True

        classes = [scope for scope in scopes if scope.kind == "class"]
        current_class = classes[-1] if classes else None
        namespace_depth = scopes[-1].body_depth if scopes and scopes[-1].kind == "namespace" else 0
        class_context = bool(
            current_class and brace_depth == current_class.body_depth
            and current_class.visible and current_class.public
        )
        namespace_context = not current_class and brace_depth == namespace_depth
        context = "::".join(scope.name for scope in scopes if scope.kind in {"namespace", "class"})

        if opening_scope or stripped.startswith("#") or not (class_context or namespace_context):
            pending = ""
        elif stripped:
            if pending and context != pending_context:
                pending = ""
            if not pending:
                pending_line = line_number
                pending_context = context
            pending = f"{pending} {stripped}".strip()
            while pending:
                opening = parameter_opening(pending)
                closing = matching_paren(pending, opening) if opening >= 0 else -1
                if opening < 0 and ";" in pending:
                    pending = pending.split(";", 1)[1].strip()
                    pending_line = line_number
                    continue
                semicolon = declaration_semicolon(pending, closing) if closing >= 0 else -1
                body = declaration_body(pending, closing) if closing >= 0 else -1
                if closing < 0 or (semicolon < 0 and body < 0):
                    break
                terminal = semicolon + 1 if semicolon >= 0 and (body < 0 or semicolon < body) else len(pending)
                declaration = pending[:terminal]
                candidate = parse_declaration(
                    declaration, module, header, pending_line, context, is_member=current_class is not None,
                )
                if candidate:
                    found.append(candidate)
                pending = pending[terminal:].strip() if terminal < len(pending) else ""
                pending_line = line_number

        brace_depth += raw.count("{") - raw.count("}")
    return found


def scan_header(module: Module, header: Path) -> list[Callable]:
    original = header.read_text(encoding="utf-8", errors="ignore")
    commented = mask_comments(original)
    macros = local_struct_macros(commented)
    without_macro_invocations = "\n".join(
        " " * len(line)
        if any(re.match(rf"\s*{re.escape(name)}\s*\(.*\)\s*;\s*$", line) for name in macros)
        else line
        for line in commented.splitlines()
    )
    found = scan_text(module, header, mask_preprocessor(without_macro_invocations))
    namespace_match = re.search(r"\bnamespace\s+([A-Za-z_]\w*(?:::[A-Za-z_]\w*)*)", mask_comments(original))
    namespace = namespace_match.group(1) if namespace_match else ""
    definition_lines: set[int] = set()
    lines = original.splitlines()
    continuing = False
    for line_number, line in enumerate(lines, 1):
        if continuing or line.lstrip().startswith("#"):
            definition_lines.add(line_number)
            continuing = line.rstrip().endswith("\\")
    for line_number, line in enumerate(lines, 1):
        if line_number in definition_lines:
            continue
        invocation = re.match(r"\s*([A-Za-z_]\w*)\s*\((.*)\)\s*;\s*$", line)
        if not invocation or invocation.group(1) not in macros:
            continue
        params, body = macros[invocation.group(1)]
        args = [part.strip() for part in invocation.group(2).split(",")]
        if len(params) != len(args):
            continue
        expanded = body
        for param, arg in zip(params, args):
            expanded = re.sub(rf"\b{re.escape(param)}\b", arg, expanded)
        wrapped = f"namespace {namespace}\n{{\n{expanded};\n}}" if namespace else expanded + ";"
        for item in scan_text(module, header, mask_preprocessor(mask_comments(wrapped))):
            qualified = f"{item.scope}::{item.name}" if item.scope else item.name
            identity = callable_identity(module, item.header, qualified, item.signature)
            found.append(Callable(
                hashlib.sha256(identity.encode("utf-8")).hexdigest()[:16], item.module, item.header,
                line_number, item.scope, item.name, item.signature, item.classification,
            ))
    return found


def scan_modules(modules: list[Module]) -> list[Callable]:
    callables = [item for module in modules for header in module.headers for item in scan_header(module, header)]
    unique: dict[str, Callable] = {}
    for item in callables:
        previous = unique.get(item.id)
        if previous is not None and (
            previous.module, previous.header, previous.scope, previous.signature
        ) != (item.module, item.header, item.scope, item.signature):
            raise ValueError(f"callable ID collision: {previous} vs {item}")
        if previous is None or item.line < previous.line:
            unique[item.id] = item
    return sorted(unique.values(), key=lambda item: (item.module, item.header, item.line, item.signature))


def load_classification_overrides() -> dict[str, dict[str, object]]:
    if not CLASSIFICATION_OVERRIDES.is_file():
        return {}
    data = json.loads(CLASSIFICATION_OVERRIDES.read_text(encoding="utf-8"))
    if data.get("schema_version") != 1 or not isinstance(data.get("overrides"), dict):
        raise ValueError("classification override registry schema mismatch")
    return data["overrides"]


def apply_classification_overrides(
    callables: list[Callable], overrides: dict[str, dict[str, object]],
) -> tuple[list[Callable], list[str]]:
    errors: list[str] = []
    known = {item.id for item in callables}
    for callable_id in sorted(set(overrides) - known):
        errors.append(f"stale classification override: {callable_id}")
    allowed = {"CONSTRUCTOR", "DESTRUCTOR", "FACTORY", "LIFECYCLE", "MUTATOR", "QUERY", "CALLBACK"}
    result: list[Callable] = []
    for item in callables:
        record = overrides.get(item.id)
        if record is None:
            result.append(item)
            continue
        classification = record.get("classification") if isinstance(record, dict) else None
        rationale = record.get("rationale") if isinstance(record, dict) else None
        if (
            classification not in allowed
            or record.get("reviewed") is not True
            or not isinstance(rationale, str)
            or not rationale.strip()
        ):
            errors.append(f"{item.id}: classification override must be reviewed with classification and rationale")
            result.append(item)
            continue
        result.append(Callable(
            item.id, item.module, item.header, item.line, item.scope, item.name, item.signature, str(classification),
        ))
    return result, errors


def validate_signatures(callables: list[Callable]) -> list[str]:
    errors: list[str] = []
    for item in callables:
        if (
            item.signature.count("(") != item.signature.count(")")
            or item.signature.count("{") != item.signature.count("}")
            or item.signature.count("[") != item.signature.count("]")
        ):
            errors.append(f"{item.id}: truncated or unbalanced signature: {item.signature}")
    return errors


def validate_oracle() -> list[str]:
    errors: list[str] = []
    if not ORACLE.is_file():
        return ["reviewed public API oracle is missing"]
    data = json.loads(ORACLE.read_text(encoding="utf-8"))
    if data.get("schema_version") != 1 or data.get("reviewed") is not True:
        return ["public API oracle is not explicitly reviewed"]
    source = ROOT / str(data.get("source", ""))
    if not source.is_file():
        return ["public API oracle source is missing"]
    fixture = Module("Oracle", source.parent, "Oracle", (source,), ())
    actual_rows = scan_header(fixture, source)
    actual = [[row.line, row.scope, row.name, row.signature, row.classification] for row in actual_rows]
    expected = data.get("callables")
    if actual != expected:
        errors.append(f"public API oracle mismatch: expected {len(expected or [])} rows, found {len(actual)}")
    found_names = {row.name for row in actual_rows}
    leaked = sorted(set(data.get("excluded_names", [])) & found_names)
    if leaked:
        errors.append(f"public API oracle leaked excluded declarations: {leaked}")
    return errors


def load_anchors() -> dict[str, object]:
    if not ANCHORS.exists():
        return {"schema_version": 1, "anchors": {}}
    return json.loads(ANCHORS.read_text(encoding="utf-8"))


def validate_anchors(data: object, callables: list[Callable]) -> list[str]:
    errors: list[str] = []
    if not isinstance(data, dict) or data.get("schema_version") != 1 or not isinstance(data.get("anchors"), dict):
        return ["anchor registry schema mismatch"]
    known = {item.id for item in callables}
    test_targets = registered_test_targets(ROOT)
    for callable_id, record in data["anchors"].items():
        if callable_id not in known:
            errors.append(f"stale/unknown callable anchor: {callable_id}")
            continue
        if not isinstance(record, dict) or set(record) - {"contract", "test"}:
            errors.append(f"{callable_id}: anchor record has invalid fields")
            continue
        for kind, evidence in record.items():
            if not isinstance(evidence, dict) or evidence.get("reviewed") is not True:
                errors.append(f"{callable_id}/{kind}: evidence must be explicitly reviewed")
                continue
            anchor_errors = validate_path_anchor(ROOT, evidence.get("anchor"))
            errors.extend(f"{callable_id}/{kind}: {error}" for error in anchor_errors)
            if kind == "test":
                test_errors = validate_test_evidence(ROOT, evidence, test_targets)
                errors.extend(f"{callable_id}/test: {error}" for error in test_errors)
                anchor_path = str(evidence.get("anchor", "")).split("::", 1)[0].rsplit(":", 1)[0]
                if "test" not in Path(anchor_path).as_posix().lower():
                    errors.append(f"{callable_id}/test: anchor is not in test infrastructure")
    return errors


def fixture_anchor(relative: str, symbol: str) -> str:
    path = ROOT / relative
    for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if symbol in line:
            return f"{relative}:{line_number}::{symbol}"
    raise ValueError(f"fixture anchor symbol is missing: {relative}::{symbol}")


def render(modules: list[Module], callables: list[Callable], anchor_data: dict[str, object]) -> str:
    anchors = anchor_data["anchors"]
    classifications = Counter(item.classification for item in callables)
    contract_anchors = sum("contract" in record for record in anchors.values())
    test_anchors = sum("test" in record for record in anchors.values())
    lines = [
        "# Public API coverage inventory",
        "",
        "Generated by `docs/freeze/public_api_inventory.py` from public headers. This is a coverage index, not a module audit and not LOCAL_READY evidence by itself.",
        "",
        f"Inventory: `{len(callables)}` exact callable declarations across `78` production modules and `232` public headers.",
        "",
        "Overloads are distinct rows keyed by header, declaration line, fully-qualified scope and normalized declaration. Unknown public non-const callables are retained as `UNCLASSIFIED`. Contract and test/audit evidence is accepted only from the explicit reviewed anchor registry; test source is never searched by API-name text matching.",
        "",
        "Classifications: " + ", ".join(f"`{name}={count}`" for name, count in sorted(classifications.items())) + ".",
        f"Explicit reviewed anchors: `contract={contract_anchors}`, `test/audit={test_anchors}`. Missing anchors remain visible audit debt.",
        "",
    ]
    by_module: dict[str, list[Callable]] = {module.key: [] for module in modules}
    for item in callables:
        by_module[item.module].append(item)
    for module in modules:
        lines.extend([f"## {module.key}", ""])
        if not by_module[module.key]:
            lines.extend(["No public callable declaration; module implementation is reached through another module's public port.", ""])
            continue
        lines.extend(["| ID | Declaration | Classification | Contract anchor | Test/audit anchor |", "|---|---|---|---|---|"])
        for item in by_module[module.key]:
            record = anchors.get(item.id, {})
            contract = record.get("contract", {}).get("anchor", "MISSING")
            test = record.get("test", {}).get("anchor", "MISSING")
            declaration = f"`{item.header}:{item.line}::{item.scope}::{item.signature}`".replace("|", "\\|")
            lines.append(f"| `{item.id}` | {declaration} | `{item.classification}` | `{contract}` | `{test}` |")
    return "\n".join(lines).rstrip() + "\n"


def self_test() -> list[str]:
    fixture = """\
namespace sample
{
struct Demo
{
public:
    Result Cancel(int id);
    Result Cancel(std::string_view id);
    static Demo Create();
    int Size() const;
    void Mystery();
    std::function<void()> callback;
private:
    void Hidden();
};
#define MAKE_ID(name) \\
    struct name \\
    { \\
        bool IsValid() const; \\
    }
MAKE_ID(WidgetId);
}
"""
    temp_root = ROOT / "docs" / "freeze"
    header = temp_root / "_public_api_fixture.hpp"
    module = Module("Fixture", temp_root, "Fixture", (header,), ())
    try:
        header.write_text(fixture, encoding="utf-8", newline="\n")
        rows = scan_header(module, header)
        header.write_text("// line shift must not change callable identities\n\n" + fixture, encoding="utf-8", newline="\n")
        shifted_rows = scan_header(module, header)
    finally:
        if header.exists():
            header.unlink()
    errors: list[str] = []
    errors.extend(validate_oracle())
    if [item.id for item in rows] != [item.id for item in shifted_rows]:
        errors.append("callable IDs changed after a comment-only line shift")
    errors.extend(validate_signatures(rows))
    cancel = [item for item in rows if item.name == "Cancel"]
    if len(cancel) != 2 or len({item.id for item in cancel}) != 2 or any(item.classification != "MUTATOR" for item in cancel):
        errors.append("overloaded Cancel methods were lost, collapsed or misclassified")
    expected = {"Create": "FACTORY", "Size": "QUERY", "Mystery": "UNCLASSIFIED"}
    for name, classification in expected.items():
        matches = [item for item in rows if item.name == name]
        if len(matches) != 1 or matches[0].classification != classification:
            errors.append(f"{name} classification mismatch")
    if any(item.name in {"Hidden", "function", "callback"} for item in rows):
        errors.append("private method or callable data member leaked into inventory")
    macro_rows = [item for item in rows if item.scope.endswith("WidgetId") and item.name == "IsValid"]
    if len(macro_rows) != 1 or macro_rows[0].classification != "QUERY":
        errors.append("public struct macro invocation was not expanded exactly once")
    if any(item.name == "MAKE_ID" or item.scope.endswith("::name") for item in rows):
        errors.append("macro definition/invocation leaked as a callable")
    if len(rows) != 6:
        errors.append(f"fixture expected 6 callables, found {len(rows)}")
    if validate_anchors({"schema_version": 1, "anchors": {}}, rows):
        errors.append("empty explicit anchor registry was rejected")
    good_anchor = {
        "schema_version": 1,
        "anchors": {
            rows[0].id: {
                "test": {
                    "anchor": fixture_anchor("EngineFramework/DevelopmentInfrastructure/Tests/test_utilities_tests.cpp", "int main("),
                    "reviewed": True,
                    "target": "EpidemicGameFrameworkTestUtilitiesTests",
                    "assertions": [
                        fixture_anchor("EngineFramework/DevelopmentInfrastructure/Tests/test_utilities_tests.cpp", "void Check(")
                    ],
                }
            }
        },
    }
    if validate_anchors(good_anchor, rows):
        errors.append("valid reviewed test evidence was rejected")
    bad_anchor = {
        "schema_version": 1,
        "anchors": {
            rows[0].id: {
                "test": {"anchor": "tests.cpp:1::name-match", "reviewed": True, "target": "FixtureTests"}
            }
        },
    }
    if not validate_anchors(bad_anchor, rows):
        errors.append("unreviewed assertion evidence was accepted as a test anchor")
    unknown_target = copy.deepcopy(good_anchor)
    unknown_target["anchors"][rows[0].id]["test"]["target"] = "MissingTests"
    if not validate_anchors(unknown_target, rows):
        errors.append("unregistered test target was accepted as evidence")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate or verify the exact public callable coverage index.")
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        errors = self_test()
        if errors:
            print("\n".join(f"ERROR: {error}" for error in errors), file=sys.stderr)
            return 1
        print("PASS: overload, Cancel, factory priority, UNCLASSIFIED, visibility, Allman, macro and explicit-anchor fixtures.")
        return 0

    modules = discover_modules()
    callables = scan_modules(modules)
    try:
        overrides = load_classification_overrides()
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1
    callables, override_errors = apply_classification_overrides(callables, overrides)
    if not ANCHORS.exists() and not args.check:
        ANCHORS.write_text(json.dumps({"schema_version": 1, "anchors": {}}, indent=2) + "\n", encoding="utf-8", newline="\n")
    anchors = load_anchors()
    errors = override_errors + validate_signatures(callables) + validate_anchors(anchors, callables)
    errors.extend(validate_oracle())
    if len(modules) != 78 or sum(len(module.headers) for module in modules) != 232:
        errors.append("production module/header inventory mismatch")
    identities = [item.id for item in callables]
    if len(identities) != len(set(identities)):
        errors.append("callable IDs are not unique")
    unclassified = [item for item in callables if item.classification == "UNCLASSIFIED"]
    if unclassified:
        errors.append(f"{len(unclassified)} public callables remain UNCLASSIFIED")
    if errors:
        print("\n".join(f"ERROR: {error}" for error in errors), file=sys.stderr)
        return 1
    rendered = render(modules, callables, anchors)
    if args.check:
        if not OUTPUT.exists() or OUTPUT.read_text(encoding="utf-8") != rendered:
            print("ERROR: public API inventory is missing or stale; run docs/freeze/public_api_inventory.py", file=sys.stderr)
            return 1
        print(f"PASS: {len(callables)} exact public callables; stable IDs; balanced signatures; zero UNCLASSIFIED rows.")
        return 0
    OUTPUT.write_text(rendered, encoding="utf-8", newline="\n")
    print(f"Wrote {OUTPUT.relative_to(ROOT)} with {len(callables)} exact public callable rows.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
