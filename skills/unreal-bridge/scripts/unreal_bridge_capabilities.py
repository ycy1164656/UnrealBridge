"""Closed, schema-checked capability routing for the small MCP surface."""

from __future__ import annotations

import hashlib
import inspect
import json
from dataclasses import dataclass
from typing import Any, Callable, get_type_hints

from pydantic import ConfigDict, ValidationError, create_model


def digest(value: Any) -> str:
    return hashlib.sha256(json.dumps(value, sort_keys=True, ensure_ascii=False, default=str,
                                      separators=(",", ":")).encode("utf-8")).hexdigest()


@dataclass(frozen=True)
class HostCapability:
    id: str
    handler: Callable[..., dict[str, Any]]
    domain: str
    risk: str
    execution: str
    save_behavior: str
    short_description: str

    def input_model(self):
        fields = {}
        hints = get_type_hints(self.handler)
        for name, parameter in inspect.signature(self.handler).parameters.items():
            annotation = hints.get(name, Any)
            default = parameter.default if parameter.default is not inspect.Parameter.empty else ...
            fields[name] = (annotation, default)
        return create_model("Capability_" + self.id.replace(":", "_").replace(".", "_"),
                            __config__=ConfigDict(extra="forbid"), **fields)

    def describe(self) -> dict[str, Any]:
        schema = self.input_model().model_json_schema()
        return {"id": self.id, "name": self.id.split(":", 1)[1], "domain": self.domain,
                "provider": "workflow", "kind": "workflow", "risk": self.risk,
                "execution": self.execution, "save_behavior": self.save_behavior,
                "short_description": self.short_description, "input_schema": schema,
                "schema_hash": digest(schema)}

    def call(self, arguments: dict[str, Any]) -> dict[str, Any]:
        try:
            validated = self.input_model().model_validate(arguments)
        except ValidationError as exc:
            return {"success": False, "phase": "schema", "error_code": "invalid_arguments",
                    "errors": exc.errors(include_url=False)}
        return self.handler(**validated.model_dump())


class HostCapabilityRegistry:
    def __init__(self, capabilities: list[HostCapability]):
        self.by_id = {item.id: item for item in capabilities}
        if len(self.by_id) != len(capabilities):
            raise ValueError("duplicate host capability ID")
        self.hash = digest({key: item.describe()["schema_hash"] for key, item in self.by_id.items()})

    def search(self, query: str = "") -> list[dict[str, Any]]:
        terms = query.casefold().split()
        return [item.describe() for item in self.by_id.values()
                if all(term in (item.id + " " + item.short_description).casefold() for term in terms)]
