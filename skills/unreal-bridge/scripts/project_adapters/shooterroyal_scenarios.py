"""Typed adapter for ShooterRoyal-owned transient scenarios; no game rules here."""
from __future__ import annotations
import copy
import json
import math
import re

SCHEMA = "shooterroyal.scenario.v1"
OBSERVATIONS = {"prediction_begin", "prediction_state", "prediction_end", "prediction_input"}
OPERATIONS = {"context", "prepare", "state", "reset", "release", "action", "takeover"} | OBSERVATIONS
COMMON = {"schema", "operation", "request_id", "world_handle", "player_handle"}
EXTRA = {
    "context": {"set_enabled"},
    "prepare": {"world_generation", "player_identity", "scenario_id", "scenario_version", "seed", "mode", "lease_seconds"},
    "state": {"world_generation", "run_id"},
    "reset": {"world_generation", "run_id", "expected_revision"},
    "release": {"world_generation", "run_id", "expected_revision"},
    "action": {"world_generation", "run_id", "expected_revision", "action"},
    "takeover": {"world_generation", "run_id", "expected_revision"},
    **{op: {"world_generation", "run_id"} for op in OBSERVATIONS},
}


def normalize(request):
    if not isinstance(request, dict) or request.get("schema") != SCHEMA or request.get("operation") not in OPERATIONS:
        raise ValueError("Unsupported scenario request schema/operation")
    op = request["operation"]
    if set(request) - COMMON - EXTRA[op]:
        raise ValueError("Unexpected fields; script, class and asset-path execution are not accepted")
    value = copy.deepcopy(request)
    if len(json.dumps(value, allow_nan=False).encode("utf-8")) > 8192:
        raise ValueError("Scenario request exceeds 8 KiB")
    if not isinstance(value.get("request_id"), str) or not re.fullmatch(r"[A-Za-z0-9_.-]{1,128}", value["request_id"]):
        raise ValueError("Explicit short request_id required")
    for field, prefix in [("world_handle", "ubr:world:"), ("player_handle", "ubr:actor:")]:
        if not isinstance(value.get(field), str) or not value[field].startswith(prefix) or len(value[field]) > 256:
            raise ValueError(f"Explicit opaque {field} required")
    if op != "context" and (not isinstance(value.get("world_generation"), str) or not re.fullmatch(r"[a-fA-F0-9]{32}", value["world_generation"])):
        raise ValueError("Fresh native world_generation required")
    if op == "context" and "set_enabled" in value and type(value["set_enabled"]) is not bool:
        raise ValueError("set_enabled must be Boolean")
    if op == "prepare":
        for field in ("player_identity", "scenario_id"):
            if not isinstance(value.get(field), str) or not 1 <= len(value[field]) <= 2048:
                raise ValueError(f"Explicit {field} required")
        value.setdefault("scenario_version", 1)
        value.setdefault("seed", 1)
        value.setdefault("mode", "automated")
        value.setdefault("lease_seconds", 300)
        if value["mode"] not in ("automated", "interactive"):
            raise ValueError("Unsupported end policy")
        if type(value["seed"]) is not int or not -(2**31) <= value["seed"] < 2**31:
            raise ValueError("seed must be int32")
        if type(value["scenario_version"]) is not int or not 1 <= value["scenario_version"] <= 10000:
            raise ValueError("Invalid scenario_version")
        lease = value["lease_seconds"]
        if type(lease) not in (int, float) or not math.isfinite(lease) or not 1 <= lease <= 3600:
            raise ValueError("Invalid lease_seconds")
    if op not in ("prepare", "context") and (not isinstance(value.get("run_id"), str) or not re.fullmatch(r"[a-fA-F0-9]{32}", value["run_id"])):
        raise ValueError("Explicit native run_id required")
    if op in ("reset", "release", "action", "takeover") and (type(value.get("expected_revision")) is not int or value["expected_revision"] < 1):
        raise ValueError("Read the current revision before mutation")
    if op == "action" and (not isinstance(value.get("action"), str) or not re.fullmatch(r"[A-Za-z0-9_.-]{1,64}", value["action"])):
        raise ValueError("An explicit registered action ID is required")
    return value


def build_script(request):
    value = normalize(request)
    payload = json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
    return f'''import json, unreal
from unreal_bridge import World
_sr_req = json.loads({payload!r})
_sr_ref = json.loads(World.validate_actor_reference(actor_handle=_sr_req['player_handle']))
if not _sr_ref.get('ok') or _sr_ref.get('world_handle') != _sr_req['world_handle']:
    raise RuntimeError('StaleHandle: player handle does not belong to the requested World')
_sr_player = unreal.find_object(None, _sr_ref['actor_path'])
_sr_op = _sr_req['operation']
if not isinstance(_sr_player, unreal.PlayerController) or (_sr_op not in ('context','prediction_begin','prediction_state','prediction_end','prediction_input') and not _sr_player.has_authority()):
    raise RuntimeError('ScopeViolation: a server PlayerController is required')
_sr_sub = unreal.SRAutomationScenarioSubsystem.get_for_player(_sr_player)
if not _sr_sub or not _sr_sub.is_scenario_code_available():
    raise RuntimeError('UnsupportedCapability: scenario subsystem is unavailable')
_sr_op = _sr_req['operation']
if _sr_op == 'context':
    if 'set_enabled' in _sr_req:
        if not _sr_player.has_authority():
            raise RuntimeError('ScopeViolation: only authority may change the runtime opt-in')
        unreal.SystemLibrary.execute_console_command(_sr_player, 'sr.Automation.Scenarios.Enabled ' + ('1' if _sr_req['set_enabled'] else '0'))
    _sr_descriptions = [{{'scenario_id':d.scenario_id,'version':d.version,'description':d.description,'actions':list(d.actions),'experience_action':d.experience_action,'expected_result':d.expected_result}} for d in _sr_sub.list_scenarios()]
    print(json.dumps({{'ok':True,'world_generation':_sr_sub.get_world_generation(),'player_identity':_sr_sub.get_scenario_player_identity(_sr_player),'enabled':_sr_sub.is_scenario_access_enabled(),'scenarios':_sr_descriptions}}))
elif _sr_op in ('prediction_begin','prediction_state','prediction_end','prediction_input'):
    _sr_method = {{'prediction_begin':_sr_sub.begin_prediction_observation,'prediction_state':_sr_sub.get_prediction_observation,'prediction_end':_sr_sub.end_prediction_observation,'prediction_input':_sr_sub.queue_prediction_weapon_input}}[_sr_op]
    print(json.dumps(dict(_sr_method(_sr_player,_sr_req['world_generation'],_sr_req['run_id'])),ensure_ascii=False))
else:
    if _sr_op == 'prepare':
        _sr_native = unreal.SRAutomationScenarioRequest(request_id=_sr_req['request_id'],scenario_id=_sr_req['scenario_id'],scenario_version=_sr_req['scenario_version'],expected_world_generation=_sr_req['world_generation'],world_handle=_sr_req['world_handle'],player_identity=_sr_req['player_identity'],seed=_sr_req['seed'],mode=_sr_req['mode'],lease_seconds=_sr_req['lease_seconds'])
        _sr_state = _sr_sub.prepare_scenario(_sr_player, _sr_native)
    elif _sr_op == 'state':
        _sr_state = _sr_sub.get_scenario_state(_sr_player,_sr_req['world_generation'],_sr_req['run_id'])
    elif _sr_op == 'takeover':
        _sr_state = _sr_sub.take_over_scenario(_sr_player,_sr_req['world_generation'],_sr_req['run_id'],_sr_req['expected_revision'])
    else:
        _sr_args = [_sr_player,_sr_req['world_generation'],_sr_req['run_id'],_sr_req['request_id'],_sr_req['expected_revision']]
        if _sr_op == 'action':
            _sr_state = _sr_sub.perform_scenario_action(*_sr_args,_sr_req['action'])
        elif _sr_op == 'reset':
            _sr_state = _sr_sub.reset_scenario(*_sr_args)
        else:
            _sr_state = _sr_sub.release_scenario(*_sr_args)
    _sr_fields = ['success','status','error_code','error','run_id','scenario_id','scenario_version','world_generation','world_handle','player_identity','mode','seed','revision','cleanup_state','experience_action','expected_result']
    _sr_result = {{field:getattr(_sr_state,field) for field in _sr_fields}}
    _sr_result.update(owned_actor_paths=list(_sr_state.owned_actor_paths),evidence=dict(_sr_state.evidence))
    print(json.dumps(_sr_result,ensure_ascii=False))
'''


def submit(server, request, **route):
    normalized = normalize(request)
    return server.bridge_submit_job(build_script(normalized),
        idempotency_key="sr-scenario:" + normalized["request_id"],
        world_handle=normalized["world_handle"], run_timeout=60, **route)
