from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

sys.dont_write_bytecode = True

from module_dossiers import (
    RESPONSIBILITIES,
    ROOT,
    Module,
    discover_modules,
    extract_types,
    matching_types,
    public_text,
    render_list,
)


OUTPUT = ROOT / "docs" / "freeze" / "architecture_ownership_matrix.md"
SOURCE_SUFFIXES = {".h", ".hpp", ".cpp", ".cxx", ".cc"}
INCLUDE_RE = re.compile(r'^\s*#\s*include\s*([<"])([^>"]+)[>"]', re.M)
EXTERNAL_RE = re.compile(
    r"^(?:Windows\.h|windows\.h|d3d(?:11|common|compiler)\.h|dxgi(?:1_[0-9]+)?\.h|wrl/|Xinput\.h)",
    re.I,
)

# These modules define contracts or compose/co-ordinate owners. They must not
# silently acquire an authoritative copy of another module's domain data.
NON_DOMAIN_OWNERS = {
    "EngineBase/Foundation": "Value and error contracts only",
    "EngineBase/Support": "Composition lifetime only",
    "EngineRuntime/RuntimeFoundation": "Runtime value contracts only",
    "EngineRuntime/Support": "Runtime composition lifetime only",
    "EngineFramework/BaseInfrastructure/Foundation": "Gameplay value contracts only",
    "EngineFramework/BaseInfrastructure/Queries": "Query dispatch only; queried owners retain authority",
    "EngineFramework/IntegrationLayer/ExtendedGameplayIntegration": "Cross-domain coordination only; linked services retain authority",
    "EngineFramework/IntegrationLayer/GameplayIntegration": "Cross-domain coordination only; linked services retain authority",
    "EngineFramework/IntegrationLayer/Integration": "Core adapter composition only; linked services retain authority",
    "EngineFramework/IntegrationLayer/InteractionEffectsIntegration": "Interaction/effect delivery coordination only; owners retain authority",
    "EngineFramework/IntegrationLayer/InteractionTimeIntegration": "Interaction/time coordination only; owners retain authority",
    "EngineFramework/IntegrationLayer/NarrativeIntegration": "Narrative delivery coordination only; owners retain authority",
    "EngineFramework/IntegrationLayer/PerceptionKnowledgeAIIntegration": "Perception/knowledge/AI coordination only; owners retain authority",
    "EngineFramework/IntegrationLayer/PopulationSimulationIntegration": "Population/simulation coordination only; owners retain authority",
    "EngineFramework/IntegrationLayer/ProcessResourceSimulationIntegration": "Process/resource coordination only; owners retain authority",
    "EngineFramework/IntegrationLayer/SocialLegalIntegration": "Social/legal coordination only; owners retain authority",
    "EngineFramework/IntegrationLayer/StateIntegration": "State projection coordination only; owners retain authority",
    "EngineFramework/IntegrationLayer/TraversalNavigationConstructionIntegration": "Traversal/navigation/construction coordination only; owners retain authority",
    "EngineFramework/IntegrationLayer/WorldIntegration": "World/runtime projection coordination only; owners retain authority",
}

# Reviewed semantic ownership domains. These identifiers describe the data, not
# the directory that happens to implement it, so duplicate ownership is visible.
AUTHORITY_DOMAINS = {
    "EngineBase/Core": "engine:application-lifecycle-services-and-tasks",
    "EngineBase/Diagnostics": "engine:diagnostic-events-counters-and-profiles",
    "EngineBase/Input": "engine:normalized-input-state",
    "EngineBase/Memory": "engine:memory-allocation-and-tracking",
    "EngineBase/Platform": "engine:platform-window-and-message-state",
    "EngineBase/RHI": "engine:rendering-interface-object-state",
    "EngineBase/RHI_D3D11": "engine:d3d11-backend-object-state",
    "EngineRuntime/Animation": "runtime:animation-resources-and-playback",
    "EngineRuntime/Assets": "runtime:asset-identities-and-load-state",
    "EngineRuntime/Audio": "runtime:audio-resources-and-playback",
    "EngineRuntime/Environment": "runtime:environment-samples-and-volumes",
    "EngineRuntime/Navigation": "runtime:navigation-mesh-path-and-agent-state",
    "EngineRuntime/Persistence": "runtime:persistence-jobs-and-results",
    "EngineRuntime/Physics": "runtime:physics-bodies-shapes-and-queries",
    "EngineRuntime/Renderer": "runtime:render-scenes-views-and-submissions",
    "EngineRuntime/Resources": "runtime:resource-identities-and-residency",
    "EngineRuntime/Scene": "runtime:scene-nodes-and-transforms",
    "EngineRuntime/Serialization": "runtime:serialization-schema-and-codecs",
    "EngineRuntime/Simulation": "runtime:simulation-clock-and-step-state",
    "EngineRuntime/Streaming": "runtime:streaming-requests-and-residency",
    "EngineRuntime/Time": "runtime:clock-and-frame-time",
    "EngineRuntime/World": "runtime:world-object-and-partition-state",
    "EngineFramework/BaseInfrastructure/Facts": "gameplay:facts-and-observation-history",
    "EngineFramework/BaseInfrastructure/SupportRandom": "gameplay:deterministic-random-streams",
    "EngineFramework/BaseInfrastructure/Time": "gameplay:semantic-time-and-schedules",
    "EngineFramework/GameplayWorldStateOwners/AI": "gameplay:ai-agents-plans-and-decisions",
    "EngineFramework/GameplayWorldStateOwners/Abilities": "gameplay:ability-definitions-executions-and-cooldowns",
    "EngineFramework/GameplayWorldStateOwners/Combat": "gameplay:combatants-engagements-and-damage",
    "EngineFramework/GameplayWorldStateOwners/Conditions": "gameplay:condition-definitions-and-instances",
    "EngineFramework/GameplayWorldStateOwners/Construction": "gameplay:construction-projects-and-progress",
    "EngineFramework/GameplayWorldStateOwners/Crime": "gameplay:offences-cases-and-sanctions",
    "EngineFramework/GameplayWorldStateOwners/Dialogue": "gameplay:dialogue-sessions-and-progress",
    "EngineFramework/GameplayWorldStateOwners/Economy": "gameplay:accounts-markets-and-transactions",
    "EngineFramework/GameplayWorldStateOwners/Effects": "gameplay:effect-definitions-executions-and-deliveries",
    "EngineFramework/GameplayWorldStateOwners/Encounters": "gameplay:encounter-definitions-and-instances",
    "EngineFramework/GameplayWorldStateOwners/Entities": "gameplay:entity-identities-and-lifecycle",
    "EngineFramework/GameplayWorldStateOwners/Environment": "gameplay:semantic-environment-state",
    "EngineFramework/GameplayWorldStateOwners/Equipment": "gameplay:equipment-slots-and-loadouts",
    "EngineFramework/GameplayWorldStateOwners/Interaction": "gameplay:interaction-definitions-and-executions",
    "EngineFramework/GameplayWorldStateOwners/ItemsInventory": "gameplay:item-instances-and-inventories",
    "EngineFramework/GameplayWorldStateOwners/Knowledge": "gameplay:subject-knowledge-and-memory",
    "EngineFramework/GameplayWorldStateOwners/Loot": "gameplay:loot-tables-and-roll-results",
    "EngineFramework/GameplayWorldStateOwners/Materials": "gameplay:material-definitions-and-compositions",
    "EngineFramework/GameplayWorldStateOwners/Narrative": "gameplay:narrative-states-storylets-and-choices",
    "EngineFramework/GameplayWorldStateOwners/NavigationSemantics": "gameplay:semantic-routes-and-travel-state",
    "EngineFramework/GameplayWorldStateOwners/NeedsLife": "gameplay:needs-health-life-and-death-state",
    "EngineFramework/GameplayWorldStateOwners/Ownership": "gameplay:asset-and-property-ownership",
    "EngineFramework/GameplayWorldStateOwners/Perception": "gameplay:perception-observations-and-awareness",
    "EngineFramework/GameplayWorldStateOwners/Population": "gameplay:population-units-and-demographics",
    "EngineFramework/GameplayWorldStateOwners/Processes": "gameplay:process-definitions-and-instances",
    "EngineFramework/GameplayWorldStateOwners/Progression": "gameplay:progression-tracks-levels-and-rewards",
    "EngineFramework/GameplayWorldStateOwners/ResourcesProduction": "gameplay:resources-stockpiles-sites-and-production",
    "EngineFramework/GameplayWorldStateOwners/RolesJobs": "gameplay:roles-jobs-and-assignments",
    "EngineFramework/GameplayWorldStateOwners/SaveGame": "gameplay:save-slots-and-checkpoints",
    "EngineFramework/GameplayWorldStateOwners/Simulation": "gameplay:simulation-layers-and-ticks",
    "EngineFramework/GameplayWorldStateOwners/Society": "gameplay:groups-memberships-and-relationships",
    "EngineFramework/GameplayWorldStateOwners/Traversal": "gameplay:traversal-modes-links-and-occupancy",
    "EngineFramework/GameplayWorldStateOwners/World": "gameplay:semantic-world-objects-and-placements",
    "EngineFramework/RuntimeBoundary/RuntimeBridge": "gameplay:runtime-bindings-projections-and-reconciliation",
}

EXTERNAL_BOUNDARIES = {
    "EngineBase/Platform": {
        "prefix": "src/Windows/",
        "headers": {"windows.h"},
        "links": {"user32", "shell32"},
        "contract": "Win32 is confined to the Platform Windows implementation; callers use Platform contracts.",
    },
    "EngineBase/RHI_D3D11": {
        "prefix": "src/",
        "headers": {"windows.h", "d3d11.h", "dxgi.h", "wrl/client.h"},
        "links": {"d3d11", "dxgi"},
        "contract": "Win32, D3D11, DXGI and WRL are confined to the RHI_D3D11 implementation; callers use RHI contracts.",
    },
}


def production_files(module: Module) -> list[Path]:
    files: list[Path] = []
    for directory in (module.root / "include", module.root / "src"):
        if directory.exists():
            files.extend(path for path in directory.rglob("*") if path.is_file() and path.suffix in SOURCE_SUFFIXES)
    return sorted(set(files))


def public_include_name(path: Path) -> str:
    parts = path.parts
    index = parts.index("include")
    return Path(*parts[index + 1 :]).as_posix()


def include_owners(modules: list[Module]) -> dict[str, Module]:
    owners: dict[str, Module] = {}
    for module in modules:
        for header in module.headers:
            name = public_include_name(header)
            if name in owners:
                raise ValueError(f"public include {name} is owned by both {owners[name].key} and {module.key}")
            owners[name] = module
    return owners


def module_includes(module: Module) -> list[tuple[Path, str, bool]]:
    found: list[tuple[Path, str, bool]] = []
    for path in production_files(module):
        text = path.read_text(encoding="utf-8", errors="ignore")
        found.extend((path, name.replace("\\", "/"), delimiter == "<") for delimiter, name in INCLUDE_RE.findall(text))
    return found


def dependency_visibility(module: Module) -> dict[str, str]:
    cmake = (module.root / "CMakeLists.txt").read_text(encoding="utf-8", errors="ignore")
    return parse_dependency_visibility(cmake, module.target)


def parse_dependency_visibility(cmake: str, target: str) -> dict[str, str]:
    pattern = re.compile(rf"target_link_libraries\s*\(\s*{re.escape(target)}\b(.*?)\)", re.S)
    strength = {"PRIVATE": 1, "INTERFACE": 2, "PUBLIC": 3}
    result: dict[str, str] = {}
    for body in pattern.findall(cmake):
        body = re.sub(r"\$<LINK_ONLY:([^>]+)>", r"\1", body)
        visibility = "PUBLIC"
        for token in re.findall(r"[A-Za-z_][A-Za-z0-9_:.+-]*", body):
            if token in strength:
                visibility = token
            elif token not in {"debug", "optimized", "general"}:
                previous = result.get(token)
                if previous is None or strength[visibility] > strength[previous]:
                    result[token] = visibility
    return result


def authority(module: Module) -> tuple[str, str]:
    if module.key in NON_DOMAIN_OWNERS:
        return "none", NON_DOMAIN_OWNERS[module.key]
    return AUTHORITY_DOMAINS[module.key], "Reviewed semantic ownership domain; module-private state stays within its declared responsibility"


def ports(module: Module) -> list[str]:
    types = extract_types(public_text(module))
    result = matching_types(
        types,
        ("Callback", "Provider", "Backend", "Port", "Sink", "Source", "Loader", "Resolver", "Executor", "Factory", "Authority", "Participant"),
    )
    result.extend(name for name in types if name.startswith("I") and len(name) > 1 and name[1].isupper())
    return sorted(set(result))


def inspect(modules: list[Module]) -> tuple[dict[str, dict[str, object]], list[str]]:
    errors: list[str] = []
    evidence: dict[str, dict[str, object]] = {}
    module_keys = {module.key for module in modules}
    ownership_keys = set(NON_DOMAIN_OWNERS) | set(AUTHORITY_DOMAINS)
    if ownership_keys != module_keys or set(NON_DOMAIN_OWNERS) & set(AUTHORITY_DOMAINS):
        return {}, ["reviewed ownership registry and discovered production modules differ or overlap"]
    try:
        owners = include_owners(modules)
    except ValueError as error:
        return {}, [str(error)]
    target_to_module = {module.target: module for module in modules}
    domains: dict[str, str] = {}

    for module in modules:
        responsibility = RESPONSIBILITIES.get(module.key, "").strip()
        if not responsibility:
            errors.append(f"{module.key}: missing responsibility")

        domain, authority_note = authority(module)
        if domain != "none":
            if domain in domains:
                errors.append(f"authoritative domain {domain} is duplicated by {domains[domain]} and {module.key}")
            domains[domain] = module.key

        link_visibility = dependency_visibility(module)
        allowed_targets = {target_to_module[name].key for name in link_visibility if name in target_to_module}
        external_links = {name for name in module.dependencies if name not in target_to_module}
        used_targets: set[str] = set()
        external_uses: list[str] = []
        for path, name, angled in module_includes(module):
            relative = path.relative_to(module.root).as_posix()
            if not angled and (name.startswith("../") or "/../" in name or "/src/" in name.lower()):
                errors.append(f"{module.key}: private/relative cross-boundary include {name} in {relative}")
            owner = owners.get(name)
            if name.startswith("Epidemic/") and owner is None:
                errors.append(f"{module.key}: project include {name} has no production-module owner")
            if owner and owner.key != module.key:
                used_targets.add(owner.key)
                if owner.key not in allowed_targets:
                    errors.append(f"{module.key}: includes {name} from {owner.key} without a direct CMake dependency")
                elif "include" in path.relative_to(module.root).parts and link_visibility.get(owner.target) == "PRIVATE":
                    errors.append(f"{module.key}: public header includes {name} but {owner.target} is a PRIVATE dependency")
            if EXTERNAL_RE.match(name):
                external_uses.append(f"{relative} -> <{name}>")
                boundary = EXTERNAL_BOUNDARIES.get(module.key)
                if not boundary:
                    errors.append(f"{module.key}: external SDK include <{name}> is outside an approved boundary")
                elif not relative.startswith(str(boundary["prefix"])) or name.lower() not in boundary["headers"]:
                    errors.append(f"{module.key}: external SDK include <{name}> is outside its approved implementation path")

        boundary = EXTERNAL_BOUNDARIES.get(module.key)
        if boundary:
            unexpected_links = external_links - set(boundary["links"])
            missing_links = set(boundary["links"]) - external_links
            if unexpected_links:
                errors.append(f"{module.key}: unapproved external link dependencies: {sorted(unexpected_links)}")
            if missing_links:
                errors.append(f"{module.key}: approved external link dependencies are missing: {sorted(missing_links)}")
            external_note = str(boundary["contract"])
            if external_uses:
                external_note += " Implementations: " + "; ".join(external_uses) + "."
            else:
                errors.append(f"{module.key}: declared external boundary has no SDK include")
            external_note += " Approved CMake links: " + render_list(sorted(external_links), "none") + "."
        else:
            if external_links:
                errors.append(f"{module.key}: external link dependencies are outside an approved boundary: {sorted(external_links)}")
            external_note = "No direct Win32/D3D SDK access. External collaboration is limited to public contracts"
            module_ports = ports(module)
            external_note += ": " + render_list(module_ports, "no named port type") + "."

        evidence[module.key] = {
            "module": module,
            "responsibility": responsibility,
            "domain": domain,
            "authority_note": authority_note,
            "used_targets": sorted(used_targets),
            "external_note": external_note,
        }

    if len(modules) != 78:
        errors.append(f"expected 78 production modules, found {len(modules)}")
    if set(RESPONSIBILITIES) != {module.key for module in modules}:
        errors.append("responsibility registry and discovered production modules differ")
    return evidence, errors


def render(modules: list[Module], evidence: dict[str, dict[str, object]]) -> str:
    lines = [
        "# Architecture and ownership matrix",
        "",
        "Generated by `docs/freeze/architecture_ownership.py` from production targets, public headers and source includes.",
        "",
        "Status: `ARCHITECTURE_OWNERSHIP_VERIFIED`. Scope: Goal 1.4, Architecture and ownership only. This is a static architecture gate; behavioral and concurrency readiness remain separate LOCAL_READY criteria.",
        "",
        "The generator enforces exactly 78 responsibility declarations, unique non-empty authoritative domains, direct CMake edges for every cross-module include, PUBLIC/INTERFACE visibility for dependencies exposed by public headers, no production relative/private cross-boundary include, and confinement of Win32/D3D SDK headers and link libraries to approved implementation boundaries.",
        "",
    ]
    current_layer = ""
    for module in modules:
        item = evidence[module.key]
        layer = module.key.split("/", 1)[0]
        if layer != current_layer:
            lines.extend([f"## {layer}", ""])
            current_layer = layer
        links = item["used_targets"]
        lines.extend([
            f"### {module.key}",
            "",
            f"- **Responsibility:** {item['responsibility']}",
            f"- **Authoritative owner:** `{item['domain']}`. {item['authority_note']}.",
            f"- **Observed cross-module includes:** {render_list(links, 'none')}. Every observed edge is a direct CMake dependency.",
            f"- **External boundary:** {item['external_note']}",
            "",
        ])
    return "\n".join(lines).rstrip() + "\n"


def self_test() -> list[str]:
    failures: list[str] = []
    modules = discover_modules()
    evidence, errors = inspect(modules)
    if errors or len(evidence) != 78:
        failures.append("current architecture inventory was rejected")

    fixture_header = modules[0].headers[0]
    duplicate_modules = [
        Module("Fixture/A", modules[0].root, "FixtureA", (fixture_header,), ()),
        Module("Fixture/B", modules[0].root, "FixtureB", (fixture_header,), ()),
    ]
    try:
        include_owners(duplicate_modules)
        failures.append("duplicate public-header ownership was accepted")
    except ValueError:
        pass

    visibility = parse_dependency_visibility(
        "target_link_libraries(Fixture PRIVATE PrivateDep PUBLIC PublicDep INTERFACE $<LINK_ONLY:InterfaceDep>)",
        "Fixture",
    )
    if visibility != {"PrivateDep": "PRIVATE", "PublicDep": "PUBLIC", "InterfaceDep": "INTERFACE"}:
        failures.append(f"dependency visibility fixture mismatch: {visibility}")

    includes = INCLUDE_RE.findall(
        '#include <Epidemic/Foundation/a.h>\n#include "Epidemic/Runtime/b.h"\n'
    )
    if includes != [("<", "Epidemic/Foundation/a.h"), ('"', "Epidemic/Runtime/b.h")]:
        failures.append("multi-include extraction fixture mismatch")
    if not EXTERNAL_RE.match("d3d11.h") or EXTERNAL_RE.match("Epidemic/Foundation/result.h"):
        failures.append("external SDK classification fixture mismatch")

    removed_key = modules[0].key
    removed_responsibility = RESPONSIBILITIES.pop(removed_key)
    try:
        _, missing_errors = inspect(modules)
    finally:
        RESPONSIBILITIES[removed_key] = removed_responsibility
    if not any("missing responsibility" in error for error in missing_errors):
        failures.append("missing responsibility was accepted")

    removed_domain_key = next(iter(AUTHORITY_DOMAINS))
    removed_domain = AUTHORITY_DOMAINS.pop(removed_domain_key)
    try:
        _, ownership_errors = inspect(modules)
    finally:
        AUTHORITY_DOMAINS[removed_domain_key] = removed_domain
    if not any("ownership registry" in error for error in ownership_errors):
        failures.append("missing reviewed ownership domain was accepted")
    return failures


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate or verify the production architecture/ownership matrix.")
    parser.add_argument("--check", action="store_true", help="Verify the committed matrix and architecture rules.")
    parser.add_argument("--self-test", action="store_true", help="Run negative architecture parser fixtures.")
    args = parser.parse_args()

    if args.self_test:
        errors = self_test()
        if errors:
            for error in errors:
                print(f"ERROR: {error}", file=sys.stderr)
            return 1
        print("PASS: architecture validator accepted the production graph and rejected duplicate ownership and missing responsibility fixtures.")
        return 0

    modules = discover_modules()
    evidence, errors = inspect(modules)
    if errors:
        for error in errors:
            print(f"ERROR: {error}", file=sys.stderr)
        return 1
    rendered = render(modules, evidence)

    if args.check:
        if not OUTPUT.exists() or OUTPUT.read_text(encoding="utf-8") != rendered:
            print("ERROR: architecture/ownership matrix is missing or stale; run docs/freeze/architecture_ownership.py", file=sys.stderr)
            return 1
        print("PASS: 78/78 responsibilities and ownership domains; include graph and external boundaries are valid; matrix is current.")
        return 0

    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT.write_text(rendered, encoding="utf-8", newline="\n")
    print(f"Wrote {OUTPUT.relative_to(ROOT)} for {len(modules)} production modules.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
