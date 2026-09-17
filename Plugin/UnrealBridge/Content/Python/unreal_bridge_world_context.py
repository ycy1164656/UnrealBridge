"""World scopes for one UnrealBridge job execution slice.

Use a new scope in every polling slice. Native job boundaries also clear all
scopes, including scopes whose Python cleanup was skipped by an exception.
"""
from contextlib import contextmanager
import json
import sys


class WorldScopeError(RuntimeError):
    pass


def _response(value):
    result = json.loads(value)
    if not isinstance(result, dict) or not isinstance(result.get("ok"), bool):
        raise WorldScopeError("Malformed native world response")
    return result


@contextmanager
def world_scope(world_handle):
    from unreal_bridge import World

    if not isinstance(world_handle, str) or not world_handle:
        raise WorldScopeError("A nonempty world handle is required")
    entered = _response(World.begin_world_scope(world_handle=world_handle))
    if not entered["ok"]:
        raise WorldScopeError(f"{entered.get('error_code')}: {entered.get('error')}")
    scope_token = entered.get("scope_token")
    if not isinstance(scope_token, str) or not scope_token:
        raise WorldScopeError("Native world scope did not return a scope token")
    try:
        yield entered
    finally:
        primary = sys.exc_info()[1]
        try:
            ended = _response(World.end_world_scope(scope_token=scope_token))
            if not ended["ok"]:
                raise WorldScopeError(f"{ended.get('error_code')}: {ended.get('error')}")
        except Exception as cleanup_error:
            if primary is None:
                raise
            if hasattr(primary, "add_note"):
                primary.add_note(f"World scope cleanup also failed: {cleanup_error}")
