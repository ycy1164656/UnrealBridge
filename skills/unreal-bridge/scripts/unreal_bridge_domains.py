#!/usr/bin/env python3
"""Typed domain registry for the audited UE 5.8 official Toolsets.

The official ToolsetRegistry is intentionally exposed through a small number
of grouped MCP operations.  This module supplies the missing stable contract:

* human-scale domains instead of provider module names;
* exact, unambiguous operation resolution;
* structural JSON-schema validation before an editor request is submitted;
* an execution-plane decision copied from the generated, deny-by-default
  catalog (ReadOnly, RuntimeInteraction, TransactionalSync, or Rejected).

The editor validates the exact tool id and structural schema hash again.  The
client-side validation here is therefore usability and fail-fast protection,
not the security boundary.
"""

from __future__ import annotations

import copy
import re
from collections import Counter, defaultdict
from dataclasses import dataclass
from typing import Any, Dict, Iterable, List, Mapping, Optional, Sequence, Set


def _normal(value: str) -> str:
    return re.sub(r"[^a-z0-9]+", "_", str(value).lower()).strip("_")


DOMAIN_ALIASES = {
    "behavior_tree": "ai",
    "behaviour_tree": "ai",
    "bt": "ai",
    "automation_test": "automation",
    "runtime_ui": "ui",
    "slate": "ui",
    "umg": "ui",
    "gamefeature": "game_feature",
    "gamefeatures": "game_feature",
    "gameplay_ability_system": "gas",
    "gameplay_abilities": "gas",
    "dataregistry": "data_registry",
    "statetree": "state_tree",
    "controlrig": "control_rig",
    "rigvm": "control_rig",
    "worldpartition": "world_partition",
    "datalayer": "data_layer",
    "metasound": "audio",
    "physics_asset": "physics",
}


MODULE_DOMAINS: Mapping[str, Set[str]] = {
    "AIModuleToolset": {"ai"},
    "AutomationTestToolset": {"automation", "validation"},
    "ConfigSettingsToolset": {"config"},
    "ConversationToolset": {"conversation", "ai"},
    "DataflowAgent": {"dataflow"},
    "DataRegistryToolset": {"data", "data_registry"},
    "GameFeaturesToolset": {"game_feature"},
    "GameplayTagsToolset": {"gameplay_tags"},
    "GASToolsets": {"gas"},
    "NiagaraToolsets": {"niagara"},
    "PCGToolset": {"pcg", "world"},
    "PhysicsToolsets": {"physics"},
    "PluginToolset": {"plugin"},
    "SemanticSearchToolset": {"semantic_search"},
    "SlateInspectorToolset": {"ui"},
    "StateTreeToolset": {"state_tree", "ai"},
    "ToolsetRegistry": {"discovery"},
    "UMGToolSet": {"ui"},
    "UnrealBridge": {"discovery"},
    "WorldConditionsToolset": {"world", "world_conditions"},
}


def official_domains(module: str, toolset: str) -> List[str]:
    """Return deterministic capability tags for one official toolset."""
    domains = set(MODULE_DOMAINS.get(module, set()))
    lower = toolset.lower()
    if module == "AnimationAssistantToolset":
        domains.add("animation")
        if "controlrig" in lower:
            domains.add("control_rig")
        if "sequencer" in lower or any(
            fragment in lower
            for fragment in ("conditions", "custom_bindings", "keyframing", "outliner", "import_export")
        ):
            domains.add("sequencer")
    elif module == "EditorToolset":
        domains.add("editor")
        suffix_map = {
            "actor": {"world"},
            "asset": {"asset"},
            "blueprint": {"blueprint"},
            "curve_table": {"data"},
            "data_asset": {"data"},
            "data_table": {"data"},
            "material_instance": {"material"},
            "material": {"material"},
            "object": {"reflection"},
            "primitive": {"mesh"},
            "scene": {"world"},
            "skeletal_mesh": {"mesh", "animation"},
            "static_mesh": {"mesh"},
            "string_table": {"data", "localization"},
            "texture": {"texture"},
            "logs": {"logs", "validation"},
        }
        for fragment, tags in suffix_map.items():
            if fragment in lower:
                domains.update(tags)
    if not domains:
        domains.add(_normal(module) or "official")
    return sorted(domains)


def _json_type(value: Any) -> str:
    if value is None:
        return "null"
    if isinstance(value, bool):
        return "boolean"
    if isinstance(value, int):
        return "integer"
    if isinstance(value, float):
        return "number"
    if isinstance(value, str):
        return "string"
    if isinstance(value, (list, tuple)):
        return "array"
    if isinstance(value, dict):
        return "object"
    return "unknown"


def _type_matches(actual: str, expected: str) -> bool:
    return actual == expected or (actual == "integer" and expected == "number")


def validate_json_schema(
    value: Any,
    schema: Mapping[str, Any],
    *,
    path: str = "$",
    max_errors: int = 20,
) -> List[str]:
    """Validate the structural subset emitted by UE ToolsetRegistry.

    The schemas are self-contained for callable input records.  Unknown JSON
    Schema annotations are deliberately ignored; all structural constraints
    currently emitted by UE 5.8 are enforced.
    """
    errors: List[str] = []

    def add(message: str) -> None:
        if len(errors) < max_errors:
            errors.append(message)

    def visit(item: Any, rule: Mapping[str, Any], location: str) -> None:
        if len(errors) >= max_errors:
            return

        if "allOf" in rule:
            for child in rule.get("allOf") or []:
                if isinstance(child, Mapping):
                    visit(item, child, location)
        for keyword in ("anyOf", "oneOf"):
            branches = rule.get(keyword)
            if isinstance(branches, list) and branches:
                matches = 0
                branch_details: List[List[str]] = []
                for branch in branches:
                    if not isinstance(branch, Mapping):
                        continue
                    branch_errors = validate_json_schema(
                        item, branch, path=location, max_errors=max_errors
                    )
                    branch_details.append(branch_errors)
                    if not branch_errors:
                        matches += 1
                # UE's generated InstancedStruct schemas use oneOf for value
                # shapes that intentionally overlap (for example Niagara int
                # also satisfies the generic number branch).  The adjacent
                # struct refPath is the real discriminator, so require at
                # least one structural match here and let the editor enforce
                # the selected concrete UScriptStruct.
                valid = matches >= 1
                if not valid:
                    detail = "; ".join(
                        group[0] for group in branch_details if group
                    )
                    add(f"{location} does not satisfy {keyword}: {detail}")
                return

        if "const" in rule and item != rule["const"]:
            add(f"{location} must equal {rule['const']!r}")
        enum_values = rule.get("enum")
        if isinstance(enum_values, list) and item not in enum_values:
            add(f"{location} must be one of {enum_values!r}; got {item!r}")

        expected = rule.get("type")
        if expected is None and ("properties" in rule or "required" in rule):
            expected = "object"
        expected_types = (
            [str(candidate) for candidate in expected]
            if isinstance(expected, list)
            else ([str(expected)] if expected else [])
        )
        actual = _json_type(item)
        if expected_types and not any(
            _type_matches(actual, candidate) for candidate in expected_types
        ):
            add(f"{location} expected {expected_types!r}; got {actual}")
            return

        if actual == "object":
            properties = rule.get("properties")
            properties = properties if isinstance(properties, Mapping) else {}
            required = rule.get("required")
            required = required if isinstance(required, list) else []
            for name in required:
                if name not in item:
                    add(f"{location}.{name} is required")
            additional = rule.get("additionalProperties", True)
            for name, child in item.items():
                child_rule = properties.get(name)
                if isinstance(child_rule, Mapping):
                    visit(child, child_rule, f"{location}.{name}")
                elif additional is False:
                    add(f"{location}.{name} is not allowed")
                elif isinstance(additional, Mapping):
                    visit(child, additional, f"{location}.{name}")
        elif actual == "array":
            if isinstance(rule.get("minItems"), int) and len(item) < rule["minItems"]:
                add(f"{location} requires at least {rule['minItems']} items")
            if isinstance(rule.get("maxItems"), int) and len(item) > rule["maxItems"]:
                add(f"{location} permits at most {rule['maxItems']} items")
            if rule.get("uniqueItems"):
                normalized = [repr(candidate) for candidate in item]
                if len(normalized) != len(set(normalized)):
                    add(f"{location} requires unique items")
            child_rule = rule.get("items")
            if isinstance(child_rule, Mapping):
                for index, child in enumerate(item):
                    visit(child, child_rule, f"{location}[{index}]")
        elif actual == "string":
            if isinstance(rule.get("minLength"), int) and len(item) < rule["minLength"]:
                add(f"{location} is shorter than {rule['minLength']}")
            if isinstance(rule.get("maxLength"), int) and len(item) > rule["maxLength"]:
                add(f"{location} is longer than {rule['maxLength']}")
            if isinstance(rule.get("pattern"), str):
                try:
                    if re.search(rule["pattern"], item) is None:
                        add(f"{location} does not match {rule['pattern']!r}")
                except re.error:
                    add(f"{location} schema contains an invalid pattern")
        elif actual in {"integer", "number"}:
            if "minimum" in rule and item < rule["minimum"]:
                add(f"{location} must be >= {rule['minimum']}")
            if "maximum" in rule and item > rule["maximum"]:
                add(f"{location} must be <= {rule['maximum']}")
            if "exclusiveMinimum" in rule and item <= rule["exclusiveMinimum"]:
                add(f"{location} must be > {rule['exclusiveMinimum']}")
            if "exclusiveMaximum" in rule and item >= rule["exclusiveMaximum"]:
                add(f"{location} must be < {rule['exclusiveMaximum']}")

        negative = rule.get("not")
        if isinstance(negative, Mapping) and not validate_json_schema(
            item, negative, path=location, max_errors=max_errors
        ):
            add(f"{location} matches a forbidden schema")

    visit(value, schema, path)
    return errors


@dataclass(frozen=True)
class OfficialOperation:
    id: str
    toolset: str
    tool: str
    module: str
    domains: Sequence[str]
    access: str
    schema_sha256: str
    record: Mapping[str, Any]

    def summary(self) -> Dict[str, Any]:
        return {
            "id": self.id,
            # This value is accepted verbatim by ``resolve`` and therefore by
            # bridge_call_domain_operation.  Returning it explicitly makes
            # list -> describe/call a mechanical, ambiguity-free workflow.
            "operation": self.id,
            "provider": "EpicToolsetRegistry",
            "module": self.module,
            "toolset": self.toolset,
            "tool": self.tool,
            "domains": list(self.domains),
            "access": self.access,
            "schema_sha256": self.schema_sha256,
            "description": self.record.get("description", ""),
            "save_behavior": self.record.get("save_behavior", "Never"),
        }

    def description(self) -> Dict[str, Any]:
        result = self.summary()
        result["input_schema"] = copy.deepcopy(self.record.get("input_schema") or {})
        result["output_schema"] = copy.deepcopy(self.record.get("output_schema") or {})
        result["classification"] = self.record.get("classification")
        return result


class OfficialDomainRegistry:
    """Index and validate the generated compact official-tool catalog."""

    def __init__(self, catalog: Any):
        toolsets: Iterable[Mapping[str, Any]]
        if isinstance(catalog, Mapping):
            toolsets = catalog.get("toolsets") or []
        elif isinstance(catalog, list):
            toolsets = catalog
        else:
            toolsets = []
        operations: List[OfficialOperation] = []
        for toolset_record in toolsets:
            if not isinstance(toolset_record, Mapping):
                continue
            toolset = str(toolset_record.get("name") or "")
            module = str(toolset_record.get("module") or "official")
            domains = official_domains(module, toolset)
            for tool_record in toolset_record.get("tools") or []:
                if not isinstance(tool_record, Mapping):
                    continue
                tool = str(tool_record.get("tool") or "")
                if not toolset or not tool:
                    continue
                operations.append(
                    OfficialOperation(
                        id=f"epic:{toolset}.{tool}",
                        toolset=toolset,
                        tool=tool,
                        module=module,
                        domains=domains,
                        access=str(tool_record.get("bridge_execution") or "Rejected"),
                        schema_sha256=str(tool_record.get("schema_sha256") or ""),
                        record=copy.deepcopy(tool_record),
                    )
                )
        self.operations = sorted(operations, key=lambda operation: operation.id.lower())
        self.by_id = {operation.id.lower(): operation for operation in self.operations}
        self.by_compound = {
            f"{operation.toolset}|{operation.tool}".lower(): operation
            for operation in self.operations
        }
        self.by_domain: Dict[str, List[OfficialOperation]] = defaultdict(list)
        for operation in self.operations:
            for domain in operation.domains:
                self.by_domain[domain].append(operation)

    @staticmethod
    def canonical_domain(domain: str) -> str:
        value = _normal(domain)
        return DOMAIN_ALIASES.get(value, value)

    def domains(self) -> List[Dict[str, Any]]:
        records: List[Dict[str, Any]] = []
        for domain, operations in sorted(self.by_domain.items()):
            counts = Counter(operation.access for operation in operations)
            records.append(
                {
                    "domain": domain,
                    "provider": "EpicToolsetRegistry",
                    "tool_count": len(operations),
                    "access_counts": dict(sorted(counts.items())),
                    "toolsets": sorted({operation.toolset for operation in operations}),
                }
            )
        return records

    def list(
        self,
        domain: str,
        *,
        query: str = "",
        access: Optional[str] = None,
    ) -> List[Dict[str, Any]]:
        canonical = self.canonical_domain(domain)
        terms = [part for part in _normal(query).split("_") if part]
        matches: List[OfficialOperation] = []
        for operation in self.by_domain.get(canonical, []):
            if access and operation.access.lower() != access.lower():
                continue
            haystack = _normal(
                " ".join(
                    (
                        operation.id,
                        operation.toolset,
                        operation.tool,
                        str(operation.record.get("description") or ""),
                    )
                )
            )
            if terms and not all(term in haystack for term in terms):
                continue
            matches.append(operation)
        return [operation.summary() for operation in matches]

    def resolve(self, domain: str, operation_name: str) -> OfficialOperation:
        canonical = self.canonical_domain(domain)
        allowed = self.by_domain.get(canonical)
        if not allowed:
            raise KeyError(f"unknown official domain {domain!r}")
        lowered = operation_name.strip().lower()
        direct = self.by_id.get(lowered) or self.by_compound.get(lowered)
        if direct:
            if canonical not in direct.domains:
                raise KeyError(
                    f"operation {operation_name!r} is not in domain {canonical!r}; "
                    f"valid domains: {list(direct.domains)!r}"
                )
            return direct
        matches = [
            operation
            for operation in allowed
            if operation.tool.lower() == lowered
            or f"{operation.toolset}.{operation.tool}".lower() == lowered
            or str(operation.record.get("name") or "").lower() == lowered
        ]
        if not matches:
            raise KeyError(
                f"unknown operation {operation_name!r} in domain {canonical!r}"
            )
        if len(matches) > 1:
            ids = [operation.id for operation in matches]
            raise KeyError(
                f"operation {operation_name!r} is ambiguous in domain {canonical!r}; "
                f"use one of {ids!r}"
            )
        return matches[0]

    def describe(self, domain: str, operation_name: str) -> Dict[str, Any]:
        return self.resolve(domain, operation_name).description()

    def validate(
        self,
        operation: OfficialOperation,
        arguments: Optional[Mapping[str, Any]],
    ) -> List[str]:
        value: Any = dict(arguments or {})
        schema = operation.record.get("input_schema") or {}
        if not isinstance(schema, Mapping):
            return [f"{operation.id} has an invalid generated input schema"]
        # Tool invocation objects are command contracts, not extensible data
        # records.  UE's generated schemas do not consistently emit
        # additionalProperties=false at the root, so enforce it here while
        # leaving nested polymorphic USTRUCT/FInstancedStruct records alone.
        errors: List[str] = []
        properties = schema.get("properties")
        if isinstance(properties, Mapping):
            errors.extend(
                f"arguments.{name} is not allowed"
                for name in sorted(set(value) - set(properties))
            )
        errors.extend(validate_json_schema(value, schema, path="arguments"))
        return errors[:20]


__all__ = [
    "DOMAIN_ALIASES",
    "OfficialDomainRegistry",
    "OfficialOperation",
    "official_domains",
    "validate_json_schema",
]
