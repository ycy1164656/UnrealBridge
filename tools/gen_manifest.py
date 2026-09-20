#!/usr/bin/env python3
"""
Generate bridge_manifest.json — single source of truth for AST preflight and
the kwargs-only wrapper module.

Two run modes (auto-detected by whether `import unreal` succeeds):

  CLI driver (outside UE):
      python tools/gen_manifest.py [--out PATH] [--bridge PATH]
      Drives a running UE editor via bridge.py to execute the in-UE half,
      captures the JSON output, and writes it to
      .claude/skills/unreal-bridge/scripts/bridge_manifest.json by default.

  In-UE reflection:
      bridge.py exec-file tools/gen_manifest.py
      Walks every unreal.UnrealBridge*Library class plus every
      unreal.Bridge* / unreal.EBridge* enum, and prints the manifest as
      JSON to stdout.

Manifest schema:
    {
      "generated_at": "<ISO 8601 UTC>",
      "ue_version": "5.7.x",
      "libraries": {
        "UnrealBridgeAssetLibrary": {
          "functions": {
            "search_assets": {
              "params": [
                {"name": "query", "type": "str", "default": null, "has_default": false},
                ...
              ],
              "returns": "tuple[list[SoftObjectPath], list[str]]",
              "doc": "Full-featured keyword search..."
            }
          }
        }
      },
      "enums": {
        "BridgeAssetSearchScope": ["ALL_ASSETS", "PROJECT", "CUSTOM_PACKAGE_PATH"]
      }
    }
"""

import json
import hashlib
import keyword as _keyword
import os
import sys

try:
    import unreal  # noqa: F401
    _IN_UE = True
except ImportError:
    _IN_UE = False


# ── In-UE half: reflect the live UnrealBridge* surface ─────────────────────

def _build_manifest_in_ue() -> dict:
    """Walk unreal.UnrealBridge*Library classes + Bridge* enums; return a manifest dict."""
    # All UnrealBridge*Library classes inherit from BlueprintFunctionLibrary →
    # UObject → _ObjectBase, which contributes ~50 generic helpers (cast,
    # get_class, call_method, get_editor_property, …). Those are NOT bridge
    # functions; subtract them so the manifest only carries our UFUNCTIONs.
    inherited_names = _collect_inherited_method_names()

    native_registry = _load_native_registry()
    registry_libraries = native_registry.get("libraries", {})
    libraries = {}
    for name in sorted(dir(unreal)):
        if not name.startswith("UnrealBridge") or not name.endswith("Library"):
            continue
        cls = getattr(unreal, name, None)
        if cls is None or not isinstance(cls, type):
            continue
        funcs = {}
        for fn_name in sorted(dir(cls)):
            if fn_name.startswith("_"):
                continue
            if fn_name in inherited_names:
                continue
            fn = getattr(cls, fn_name, None)
            if fn is None or not callable(fn):
                continue
            entry = _introspect_function(fn, fn_name)
            if entry is not None:
                native_functions = registry_libraries.get(name, {}).get("functions", {})
                native_entry = native_functions.get(fn_name)
                if native_entry is None:
                    normalized = _normalized_symbol(fn_name)
                    native_entry = next(
                        (value for key, value in native_functions.items()
                         if _normalized_symbol(key) == normalized),
                        None,
                    )
                entry = _merge_native_signature(entry, native_entry)
                funcs[fn_name] = entry
        if funcs:
            libraries[name] = {"functions": funcs}

    enums = {}
    for name in sorted(dir(unreal)):
        # UE Python strips the `E` prefix on enums but the user's reference docs
        # also use `BridgeXxx` form — keep both names if both surface.
        if not (name.startswith("Bridge") or name.startswith("EBridge")):
            continue
        cls = getattr(unreal, name, None)
        if cls is None or not isinstance(cls, type):
            continue
        members = _enum_members(cls)
        if members:
            enums[name] = members

    structs = _collect_struct_fields(enums)

    registry_hash = ""
    try:
        registry_hash = str(unreal.UnrealBridgeRegistryLibrary.get_tool_registry_hash())
    except Exception:
        pass

    return {
        "generated_at": _utc_now(),
        "ue_version": _ue_version_string(),
        "project_path": _project_path(),
        "protocol_version": native_registry.get("protocol_version", 1),
        "plugin_version": native_registry.get("plugin_version", "unknown"),
        "registry_version": native_registry.get("registry_version", 0),
        "registry_hash": registry_hash,
        "libraries": libraries,
        "enums": enums,
        "structs": structs,
    }


def _load_native_registry() -> dict:
    """Load the C++ UFunction/FProperty registry; never fall back to guessed types."""
    try:
        raw = unreal.UnrealBridgeRegistryLibrary.get_tool_registry_json()
        value = json.loads(str(raw))
        return value if isinstance(value, dict) else {}
    except Exception:
        return {}


def _normalized_symbol(value: str) -> str:
    return "".join(ch.lower() for ch in str(value) if ch.isalnum())


def _merge_native_signature(entry: dict, native_entry: "dict | None") -> dict:
    """Merge authoritative FProperty data into the Python-callable signature."""
    entry = dict(entry)
    if not native_entry:
        for param in entry.get("params", []):
            param["type"] = param.get("type") or "Any"
            param.setdefault("json_schema", {})
        entry["returns"] = entry.get("returns") or "Any"
        entry.setdefault("risk", "Mutating")
        entry.setdefault("execution", "GameThreadShort")
        entry.setdefault("save_behavior", "Never")
        entry.setdefault("supports_dry_run", False)
        entry.setdefault("supports_idempotency", False)
        entry.setdefault("introduced_version", "unknown")
        entry.setdefault("provider", "UnrealBridge")
        entry.setdefault("engine_min", "5.3.0")
        return entry

    native_inputs = native_entry.get("inputs", [])
    by_name = {
        _normalized_symbol(item.get("name", "")): item
        for item in native_inputs
    }
    merged_params = []
    for param in entry.get("params", []):
        merged = dict(param)
        native = by_name.get(_normalized_symbol(param.get("name", "")), {})
        merged["type"] = native.get("python_type") or merged.get("type") or "Any"
        merged["cpp_type"] = native.get("cpp_type", "")
        merged["kind"] = native.get("kind", "unknown")
        merged["json_schema"] = native.get("json_schema", {})
        merged_params.append(merged)
    entry["params"] = merged_params

    input_schema = dict(native_entry.get("input_schema", {}))
    input_schema["required"] = [
        param.get("name", "") for param in merged_params
        if not param.get("has_default")
    ]
    entry["input_schema"] = input_schema

    outputs = native_entry.get("outputs", [])
    output_types = [item.get("python_type") or "Any" for item in outputs]
    if len(output_types) == 1:
        entry["returns"] = output_types[0]
    elif output_types:
        entry["returns"] = f"tuple[{', '.join(output_types)}]"
    else:
        entry["returns"] = "None"

    entry["description"] = native_entry.get("description", "")
    entry["output_schema"] = native_entry.get("output_schema", {})
    for field in (
        "risk", "execution", "save_behavior", "supports_dry_run",
        "supports_idempotency", "introduced_version", "provider", "engine_min",
    ):
        entry[field] = native_entry.get(field)
    return entry


# Inherited method set on every UE Python USTRUCT (FStructBase). Keep in sync if
# UE adds new wrapper methods — easy to spot: `dir(unreal.Vector)` and subtract
# the Vector-specific fields. None of these are bridge-struct fields.
_USTRUCT_INHERITED_METHODS = {
    "assign", "cast", "copy", "export_text", "get_editor_property",
    "import_text", "set_editor_properties", "set_editor_property",
    "static_struct", "to_tuple",
}


def _collect_struct_fields(enums: dict) -> dict:
    """For every `unreal.Bridge*` USTRUCT, list its field names by subtracting the
    inherited UE Python wrapper methods from `dir(cls)`. Used by preflight to
    catch attribute-confusion errors on bridge struct returns (e.g. agent does
    `summary.parent_class_name` when the field is `parent_class_path`)."""
    structs = {}
    struct_base = getattr(unreal, "StructBase", None)
    if struct_base is None:
        return structs
    for name in sorted(dir(unreal)):
        if not name.startswith("Bridge"):
            continue
        if name in enums:
            continue  # Bridge enums share the prefix; skip them
        cls = getattr(unreal, name, None)
        if cls is None or not isinstance(cls, type):
            continue
        try:
            if not issubclass(cls, struct_base):
                continue
        except TypeError:
            continue
        # Field names = direct attributes minus inherited methods
        fields = sorted([
            a for a in dir(cls)
            if not a.startswith("_") and a not in _USTRUCT_INHERITED_METHODS
        ])
        if fields:
            structs[name] = fields
    return structs


def _project_path() -> str:
    """Absolute path to the loaded .uproject file (for wrapper mirroring)."""
    try:
        proj_dir = unreal.SystemLibrary.get_project_directory()
        proj_name = unreal.SystemLibrary.get_game_name()
        return f"{proj_dir.rstrip('/')}/{proj_name}.uproject"
    except Exception:
        return ""


def _collect_inherited_method_names() -> set:
    """Return the set of method names contributed by UE's generic Python
    base classes (BlueprintFunctionLibrary, Object, _ObjectBase, …).

    Strategy: take the dir() of BlueprintFunctionLibrary itself — every
    UnrealBridge*Library inherits from it. Names present on the bare base
    class are NOT bridge functions and should not appear in the manifest.
    """
    base_names = set()
    base = getattr(unreal, "BlueprintFunctionLibrary", None)
    if base is not None and isinstance(base, type):
        base_names.update(n for n in dir(base) if not n.startswith("_"))
    # Also subtract anything on Object / _WrapperBase / _ObjectBase if reachable.
    for cand in ("Object",):
        c = getattr(unreal, cand, None)
        if c is not None and isinstance(c, type):
            base_names.update(n for n in dir(c) if not n.startswith("_"))
    return base_names


def _introspect_function(fn, name: str):
    """Extract param names + types + defaults + return type from a UE-bound callable.

    Strategy: try `inspect.signature` first (works for some bindings), fall
    back to parsing __doc__ which UE generates as
        `X.foo(arg1, arg2, ...) -> RetType -- summary`
    Returns None when the callable isn't shaped like a UFUNCTION binding (e.g.
    inherited Python builtins like __init_subclass__).
    """
    import inspect
    import re

    doc = (getattr(fn, "__doc__", "") or "").strip()
    summary = doc.split("\n", 1)[0] if doc else ""

    # --- Path 1: inspect.signature (rare for native UE bindings, but try) ---
    try:
        sig = inspect.signature(fn)
        params = []
        for pname, p in sig.parameters.items():
            if pname in ("self", "cls"):
                continue
            params.append({
                "name": pname,
                "type": _type_repr(p.annotation, p.empty),
                "default": _default_repr(p.default) if p.default is not p.empty else None,
                "has_default": p.default is not p.empty,
            })
        returns = _type_repr(sig.return_annotation, sig.empty)
        if params or returns or summary:
            return {"params": params, "returns": returns, "doc": summary}
    except (ValueError, TypeError):
        pass  # Fall through to docstring parser

    # --- Path 2: parse the UE-generated docstring ---
    if not doc:
        return None

    # Match a leading signature line. UE forms:
    #   X.foo(arg1, arg2=default) -> RetType -- summary
    #   foo(arg1) -> RetType -- summary
    first_line = doc.split("\n", 1)[0].strip()
    m = re.match(
        r"(?:[A-Za-z_]\w*\.)?(\w+)\s*\(([^)]*)\)\s*(?:->\s*([^-\n]+?))?\s*(?:--\s*(.*))?$",
        first_line,
    )
    if not m:
        return None
    _matched_name, arg_str, ret_str, summary_str = m.groups()
    ret_str = (ret_str or "").strip()
    summary_str = (summary_str or summary or "").strip()

    params = []
    for raw in _split_top_level(arg_str):
        raw = raw.strip()
        if not raw:
            continue
        if "=" in raw:
            n, d = raw.split("=", 1)
            params.append({
                "name": n.strip(),
                "type": "",
                "default": d.strip(),
                "has_default": True,
            })
        else:
            params.append({"name": raw, "type": "", "default": None, "has_default": False})

    return {"params": params, "returns": ret_str, "doc": summary_str}


def _split_top_level(s: str):
    """Split a comma-separated arg list, respecting balanced (), [], {}."""
    parts, buf, depth = [], [], 0
    for ch in s:
        if ch in "([{":
            depth += 1
            buf.append(ch)
        elif ch in ")]}":
            depth -= 1
            buf.append(ch)
        elif ch == "," and depth == 0:
            parts.append("".join(buf))
            buf = []
        else:
            buf.append(ch)
    if buf:
        parts.append("".join(buf))
    return parts


def _enum_members(cls) -> list:
    """Heuristically detect enum members on a UE-bound enum class.

    UE Python convention: enum members are UPPER_CASE attributes whose value
    has an integer-like `.value` or is itself an int. Filter out non-members
    aggressively to avoid mistaking metaclass attrs for enum entries.
    """
    members = []
    for m in dir(cls):
        if m.startswith("_"):
            continue
        if not (m.isupper() or "_" in m and m.replace("_", "").isupper()):
            continue
        try:
            val = getattr(cls, m)
        except Exception:
            continue
        # Members are typically Enum instances or ints.
        if isinstance(val, int):
            members.append(m)
            continue
        if hasattr(val, "value"):
            try:
                int(val.value)
                members.append(m)
                continue
            except (TypeError, ValueError):
                pass
        # Exact-name comparison — Enum instance repr equals its name in UE Python
        if hasattr(val, "name") and getattr(val, "name", None) == m:
            members.append(m)
    return members


def _type_repr(annotation, empty_sentinel) -> str:
    """Stringify a parameter annotation; empty when unannotated."""
    if annotation is empty_sentinel:
        return ""
    try:
        if hasattr(annotation, "__name__"):
            return annotation.__name__
        return str(annotation)
    except Exception:
        return ""


def _default_repr(d) -> str:
    try:
        return repr(d)
    except Exception:
        return "<unrepr>"


def _utc_now() -> str:
    import datetime
    return datetime.datetime.now(datetime.timezone.utc).isoformat(timespec="seconds")


def _ue_version_string() -> str:
    try:
        return str(unreal.SystemLibrary.get_engine_version())
    except Exception:
        return "unknown"


# ── CLI driver: only fires when not running inside UE ──────────────────────

def _cli() -> int:
    import argparse
    import subprocess

    parser = argparse.ArgumentParser(
        description="Generate bridge_manifest.json by introspecting a running UE editor."
    )
    parser.add_argument("--out", help="Output path (default: <repo>/.claude/skills/unreal-bridge/scripts/bridge_manifest.json)")
    parser.add_argument("--wrapper-out", help="Wrapper module output path (default: <repo>/Plugin/UnrealBridge/Content/Python/unreal_bridge.py)")
    parser.add_argument("--no-wrapper", action="store_true", help="Skip generating the kwargs-only wrapper module")
    parser.add_argument("--bridge", help="Path to bridge.py (default: auto-detect relative to this script)")
    parser.add_argument("--timeout", type=int, default=60, help="Bridge call timeout in seconds (default: 60)")
    parser.add_argument("--project", required=True, help="Exact absolute .uproject path; output identity must match")
    parser.add_argument("--endpoint", help="Optional verified host:port; project identity is still checked")
    parser.add_argument("--deploy", action="store_true", help="Conflict-check and deploy only generated wrapper/meta to the selected project")
    args = parser.parse_args()

    here = os.path.dirname(os.path.abspath(__file__))
    repo = os.path.dirname(here)  # tools/ → repo root
    bridge = args.bridge or os.path.join(
        repo, ".claude", "skills", "unreal-bridge", "scripts", "bridge.py"
    )
    out = args.out or os.path.join(
        repo, ".claude", "skills", "unreal-bridge", "scripts", "bridge_manifest.json"
    )

    if not os.path.isfile(bridge):
        print(f"ERROR: bridge.py not found at {bridge}", file=sys.stderr)
        return 1

    # Manifest generation bootstraps newly-added Registry UFUNCTIONs, so the
    # previous manifest cannot be allowed to reject the generator itself.
    cmd = [
        sys.executable, bridge, "--json", "--no-preflight", "--manifest-bootstrap",
        "--project", os.path.abspath(args.project),
    ]
    if args.endpoint:
        cmd.extend(["--endpoint", args.endpoint])
    cmd.extend(["exec-file", os.path.abspath(__file__)])
    try:
        # Force UTF-8 + replace on decode errors. `text=True` alone defaults to
        # the active locale (GBK on zh-CN Windows), which dies on the UTF-8
        # bytes UE Python emits for non-ASCII docstrings or log lines.
        res = subprocess.run(cmd, capture_output=True, text=True,
                             encoding="utf-8", errors="replace",
                             timeout=args.timeout)
    except subprocess.TimeoutExpired:
        print(f"ERROR: bridge call timed out after {args.timeout}s", file=sys.stderr)
        return 1

    if res.returncode != 0:
        print(f"ERROR: bridge call failed (exit {res.returncode})", file=sys.stderr)
        if res.stderr:
            print(res.stderr, file=sys.stderr)
        if res.stdout:
            print(res.stdout, file=sys.stderr)
        return 1

    try:
        outer = json.loads(res.stdout)
    except json.JSONDecodeError:
        print(f"ERROR: bridge returned non-JSON:\n{res.stdout[:500]}", file=sys.stderr)
        return 1

    if not outer.get("success"):
        print(f"ERROR: in-UE script failed:\n{outer.get('error', '?')}", file=sys.stderr)
        return 1

    manifest_text = (outer.get("output") or "").strip()
    if not manifest_text:
        print("ERROR: in-UE script printed no output", file=sys.stderr)
        return 1

    # The script may print log lines BEFORE the JSON. Take the last line that
    # parses as JSON (the manifest is a single-line dump).
    last_json = None
    for line in reversed(manifest_text.splitlines()):
        line = line.strip()
        if not line:
            continue
        try:
            last_json = json.loads(line)
            break
        except json.JSONDecodeError:
            continue
    if last_json is None:
        print(f"ERROR: no JSON line in script output:\n{manifest_text[:500]}", file=sys.stderr)
        return 1

    if not manifest_matches_project(last_json, args.project):
        print("ERROR: generator returned a different or unidentified project; no output was written", file=sys.stderr)
        return 1

    canonical_manifest = json.dumps(
        last_json, ensure_ascii=False, sort_keys=True, separators=(",", ":")
    ).encode("utf-8")
    last_json["manifest_hash"] = hashlib.sha256(canonical_manifest).hexdigest()

    _write_generated(out, json.dumps(last_json, indent=2, ensure_ascii=False, sort_keys=True) + "\n", repo)

    runtime_meta = {
        "protocol_version": last_json.get("protocol_version", 1),
        "plugin_version": last_json.get("plugin_version", "unknown"),
        "registry_hash": last_json.get("registry_hash", ""),
        "manifest_hash": last_json.get("manifest_hash", ""),
        "wrapper_version": last_json.get("manifest_hash", "")[:16],
        "generated_at": last_json.get("generated_at", ""),
    }
    meta_out = os.path.join(
        repo, "Plugin", "UnrealBridge", "Content", "Python", "bridge_manifest_meta.json"
    )
    _write_generated(meta_out, json.dumps(runtime_meta, indent=2, ensure_ascii=False, sort_keys=True) + "\n", repo)

    n_libs = len(last_json.get("libraries", {}))
    n_funcs = sum(len(L.get("functions", {})) for L in last_json.get("libraries", {}).values())
    n_enums = len(last_json.get("enums", {}))
    print(f"Wrote {out}")
    print(f"  {n_libs} libraries, {n_funcs} functions, {n_enums} enums")
    print(f"  UE: {last_json.get('ue_version', '?')}, generated: {last_json.get('generated_at', '?')}")
    print(f"  manifest: {last_json.get('manifest_hash', '?')}")

    if not args.no_wrapper:
        wrapper_out = args.wrapper_out or os.path.join(
            repo, "Plugin", "UnrealBridge", "Content", "Python", "unreal_bridge.py"
        )
        wrapper_src, stats = _generate_wrapper(last_json)
        _write_generated(wrapper_out, wrapper_src, repo)
        print(f"Wrote {wrapper_out}")
        print(f"  {stats['classes']} classes, {stats['methods']} methods, "
              f"{stats['skipped']} skipped (Python keyword in param name)")

        if args.deploy:
            from deploy_scoped import deploy
            if os.path.abspath(wrapper_out) != os.path.join(repo, 'Plugin', 'UnrealBridge', 'Content', 'Python', 'unreal_bridge.py'):
                raise ValueError('--deploy requires the canonical wrapper output')
            print(json.dumps(deploy(repo, args.project,
                ['Content/Python/unreal_bridge.py', 'Content/Python/bridge_manifest_meta.json'], apply=True)))

    return 0


# ── Wrapper module generation (offline; reads manifest, emits Python) ──────

def _generate_wrapper(manifest: dict) -> "tuple[str, dict]":
    """Emit the kwargs-only wrapper module source from the manifest.

    Each unreal.UnrealBridgeXxxLibrary becomes a class `Xxx` whose
    @staticmethods mirror the bridge functions with kwargs-only signatures.
    """
    out = []
    out.append('"""')
    out.append("Auto-generated kwargs-only wrapper for UnrealBridge*Library functions.")
    out.append("")
    out.append("Regenerate after C++ header changes:")
    out.append("    python tools/gen_manifest.py")
    out.append("")
    out.append("Usage from a script sent via the bridge:")
    out.append("    from unreal_bridge import Asset, Level")
    out.append("    paths, _ = Asset.search_assets_in_all_content(query='Hero', max_results=20)")
    out.append("    info = Level.get_actor_info(actor_path='/Persistent/Player')")
    out.append("")
    out.append("Why kwargs-only? Positional-arg-order is the #1 source of model")
    out.append("hallucinations against bridge APIs — kwargs make the contract")
    out.append("structural rather than mnemonic.")
    out.append('"""')
    out.append("")
    out.append("import unreal")
    out.append("")
    out.append(f"_GENERATED_AT = {manifest.get('generated_at', '?')!r}")
    out.append(f"_UE_VERSION = {manifest.get('ue_version', '?')!r}")
    out.append(f"_PROTOCOL_VERSION = {manifest.get('protocol_version', 1)!r}")
    out.append(f"_PLUGIN_VERSION = {manifest.get('plugin_version', 'unknown')!r}")
    out.append(f"_REGISTRY_HASH = {manifest.get('registry_hash', '')!r}")
    out.append(f"_MANIFEST_HASH = {manifest.get('manifest_hash', '')!r}")
    out.append("")
    out.append("def verify_runtime_compatibility():")
    out.append("    \"\"\"Fail fast when the generated wrapper no longer matches the loaded plugin.\"\"\"")
    out.append("    if not _REGISTRY_HASH:")
    out.append("        return True")
    out.append("    registry = getattr(unreal, 'UnrealBridgeRegistryLibrary', None)")
    out.append("    if registry is None:")
    out.append("        raise RuntimeError('UnrealBridge wrapper requires a plugin with Registry support')")
    out.append("    runtime_hash = str(registry.get_tool_registry_hash())")
    out.append("    if runtime_hash != _REGISTRY_HASH:")
    out.append("        raise RuntimeError(")
    out.append("            'UnrealBridge wrapper/plugin mismatch: regenerate with python tools/gen_manifest.py'")
    out.append("        )")
    out.append("    return True")
    out.append("")
    out.append("verify_runtime_compatibility()")
    out.append("")

    n_classes, n_methods, n_skipped = 0, 0, 0

    for lib_name in sorted(manifest.get("libraries", {}).keys()):
        lib = manifest["libraries"][lib_name]
        short = _short_name(lib_name)  # UnrealBridgeAssetLibrary → Asset
        out.append(f"class {short}:")
        out.append(f'    """Wraps unreal.{lib_name} (kwargs-only)."""')
        out.append("")
        n_classes += 1

        funcs = lib.get("functions", {})
        if not funcs:
            out.append("    pass")
            out.append("")
            continue

        for fn_name in sorted(funcs.keys()):
            fn = funcs[fn_name]
            params = fn.get("params", [])

            # Skip if any param name is a Python keyword — wrapping it would
            # produce invalid syntax (def foo(*, class=...) is a SyntaxError).
            bad = [p["name"] for p in params if _keyword.iskeyword(p["name"])]
            if bad:
                out.append(f"    # SKIPPED {fn_name}: param name(s) are Python keywords: {bad}")
                out.append(f"    # Call directly: unreal.{lib_name}.{fn_name}(...)")
                out.append("")
                n_skipped += 1
                continue

            sig_parts, call_parts = [], []
            for p in params:
                pname = p["name"]
                if p.get("has_default") and p.get("default") is not None:
                    sig_parts.append(f"{pname}={p['default']}")
                else:
                    sig_parts.append(pname)
                call_parts.append(pname)

            doc = (fn.get("doc") or "").replace('"""', "'''")
            # Auto-append known traps to wrapper docstrings. References go
            # under-read — these hints are the load-bearing channel: agents
            # see them via inspect.signature, IDE auto-complete, and direct
            # wrapper-module Read. Keep each hint terse (<200 chars) so the
            # signature line stays scannable.
            returns = fn.get("returns") or ""
            extra_notes = []

            # Return-type traps:
            if "SoftObjectPath" in returns:
                extra_notes.append(
                    "Note: SoftObjectPath does NOT stringify usefully — call "
                    ".export_text() for the '/Game/Foo.Foo' path (or .to_tuple()[0]). "
                    "See bridge-asset-api.md.")

            # Function-name traps (matched by lib + name; a single bridge-X
            # function shouldn't have more than one trap so this stays linear):
            qualname = f"{lib_name}.{fn_name}"
            if qualname == "UnrealBridgeChooserLibrary.set_chooser_cell_raw":
                extra_notes.append(
                    "Trap: BoolColumn cells use bare enum text ('MatchTrue'/'MatchFalse'/"
                    "'MatchAny'), NOT a struct like '(Value=True)'. EnumColumn cells need "
                    "explicit '(Comparison=MatchAny)' for wildcards — default '()' compares "
                    "against int 0. See bridge-chooser-api.md cell-format table.")
            elif fn_name.startswith("add_chooser_column"):
                extra_notes.append(
                    "If this is a freshly-created chooser (empty ContextData), call "
                    "set_chooser_context_object_class FIRST — otherwise the editor binding "
                    "widget shows 'NoPropertyBound' on every column. See bridge-chooser-api.md "
                    "step 0.")
            elif qualname == "UnrealBridgeAnimLibrary.get_anim_node_details":
                extra_notes.append(
                    "Index-based addressing is fragile + top-level AnimGraph only. For "
                    "state-machine interiors / transition rules / sub-graphs, use "
                    "get_anim_node_details_by_guid(abp_path, graph_name, node_guid).")

            if extra_notes:
                doc = doc.rstrip() + "  " + " ".join(extra_notes)

            out.append("    @staticmethod")
            if params:
                out.append(f"    def {fn_name}(*, {', '.join(sig_parts)}):")
            else:
                out.append(f"    def {fn_name}():")
            if doc:
                out.append(f'        """{doc}"""')
            out.append(f"        return unreal.{lib_name}.{fn_name}({', '.join(call_parts)})")
            out.append("")
            n_methods += 1

        out.append("")

    return "\n".join(out), {
        "classes": n_classes,
        "methods": n_methods,
        "skipped": n_skipped,
    }


def _short_name(lib_name: str) -> str:
    """UnrealBridgeAssetLibrary → Asset; UnrealBridgeUMGLibrary → UMG."""
    s = lib_name
    if s.startswith("UnrealBridge"):
        s = s[len("UnrealBridge"):]
    if s.endswith("Library"):
        s = s[: -len("Library")]
    return s or lib_name


def _write_generated(path, text, repo):
    from pathlib import Path
    from datetime import datetime, timezone
    import shutil
    target = Path(path)
    data = text.encode('utf-8')
    if target.exists() and target.read_bytes() == data:
        return
    if target.exists():
        backup = Path(repo) / '.tmp/codex-backups' / datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ') / 'manifest'
        backup.mkdir(parents=True)
        shutil.copy2(target, backup / target.name)
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(data)
    if not data or target.read_bytes() != data:
        raise OSError('Generated file readback failed: ' + str(target))


def manifest_matches_project(manifest, expected):
    """Even a manually selected endpoint cannot authorize another project."""
    def canonical(value):
        if not isinstance(value, str) or not os.path.isabs(value) or not value.lower().endswith('.uproject'):
            return None
        return os.path.normcase(os.path.realpath(value)).replace('\\', '/')
    wanted = canonical(expected)
    return wanted is not None and canonical(manifest.get('project_path')) == wanted


# ── Entry point ────────────────────────────────────────────────────────────

if _IN_UE:
    print(json.dumps(_build_manifest_in_ue(), ensure_ascii=False))
elif __name__ == '__main__':
    sys.exit(_cli())
