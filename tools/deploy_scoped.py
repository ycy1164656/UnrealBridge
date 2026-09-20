"""Conflict-checked, non-deleting deployment of explicit plugin source/text files."""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

ALLOWED = {'.h', '.cpp', '.cs', '.uplugin', '.py', '.json', '.usf', '.ush'}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def safe_path(root, relative):
    root = Path(root)
    for ancestor in [root, *root.parents]:
        if ancestor.is_symlink() or getattr(ancestor, 'is_junction', lambda: False)():
            raise ValueError('Deployment root traverses a link/junction')
    rel = Path(relative)
    if rel.is_absolute() or '..' in rel.parts or rel.suffix.lower() not in ALLOWED:
        raise ValueError('Only exact plugin source/text paths are deployable; assets are forbidden')
    current = root
    for part in rel.parts:
        current /= part
        if current.is_symlink() or getattr(current, 'is_junction', lambda: False)():
            raise ValueError('Deployment path traverses a link/junction')
    if not current.resolve().is_relative_to(root.resolve()):
        raise ValueError('Deployment escaped root')
    return current


def deploy(repo, project, files, *, apply=False):
    repo, project = Path(repo).resolve(strict=True), Path(project).resolve(strict=True)
    if project.suffix.lower() != '.uproject':
        raise ValueError('Exact project file required')
    source_root = repo / 'Plugin/UnrealBridge'
    target_root = project.parent / 'Plugins/UnrealBridge'
    evidence = project.parent / '.tmp/artifacts/unrealbridge32/LyraEditor-Win64-Development/latest'
    state_path = evidence / 'deploy-state.json'
    state = json.loads(state_path.read_text(encoding='utf-8')) if state_path.exists() else {}
    planned = []
    for relative in sorted(set(files)):
        source, target = safe_path(source_root, relative), safe_path(target_root, relative)
        new = source.read_bytes()
        old = target.read_bytes() if target.exists() else None
        if old == new:
            continue
        baseline = subprocess.run(['git', 'show', 'HEAD:Plugin/UnrealBridge/' + Path(relative).as_posix()],
                                  cwd=repo, capture_output=True)
        known = old is None or sha(old) == state.get(relative) or (
            baseline.returncode == 0 and old.replace(b'\r\n', b'\n') == baseline.stdout.replace(b'\r\n', b'\n'))
        if not known:
            raise ValueError('Destination differs from repository baseline and prior task deployment: ' + str(target))
        if not new:
            raise ValueError('Empty source refused: ' + str(source))
        planned.append((relative, source, target, old, sha(new)))
    result = {'project': str(project), 'apply': apply, 'files': [x[0] for x in planned], 'count': len(planned)}
    if not apply or not planned:
        return result
    backup = project.parent / '.tmp/codex-backups' / datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ') / 'unrealbridge32'
    for relative, source, target, old, expected in planned:
        # Recheck the destination immediately before each copy; no overwrite after preview drift.
        if (target.read_bytes() if target.exists() else None) != old:
            raise ValueError('Destination changed during deployment: ' + str(target))
        if old is not None:
            saved = backup / relative
            saved.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(target, saved)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
        if sha(target.read_bytes()) != expected:
            raise OSError('Deployment readback failed: ' + str(target))
        state[relative] = expected
    evidence.mkdir(parents=True, exist_ok=True)
    if state_path.exists():
        backup.mkdir(parents=True, exist_ok=True)
        shutil.copy2(state_path, backup / 'deploy-state.json')
    state_path.write_text(json.dumps(state, indent=2) + '\n', encoding='utf-8')
    result['backup'] = str(backup)
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--project', required=True)
    parser.add_argument('--apply', action='store_true')
    parser.add_argument('files', nargs='+', help='Exact paths relative to Plugin/UnrealBridge')
    args = parser.parse_args()
    print(json.dumps(deploy(Path(__file__).resolve().parents[1], args.project, args.files, apply=args.apply), indent=2))
