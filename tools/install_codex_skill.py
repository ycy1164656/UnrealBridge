"""Preview/install a router to this checkout, never a second runtime snapshot."""
from __future__ import annotations

import argparse
import hashlib
import json
import shutil
from datetime import datetime, timezone
from pathlib import Path


def digest(data):
    return hashlib.sha256(data).hexdigest()


def install(checkout, destination, *, apply=False, replace_customized=False):
    checkout = Path(checkout).resolve(strict=True)
    destination = Path(destination).absolute()
    # Reject junctions at every existing ancestor before writing user files.
    for ancestor in (destination, *destination.parents):
        if ancestor.is_symlink() or getattr(ancestor, 'is_junction', lambda: False)():
            raise ValueError('Skill destination cannot traverse a link or junction')
    canonical = checkout / '.claude/skills/unreal-bridge'
    for source in ('SKILL.md', 'scripts/bridge.py', 'scripts/bridge_manifest.json'):
        if not (canonical / source).is_file():
            raise ValueError(f'Missing canonical source: {source}')
    router = (checkout / '.codex/skills/unreal-bridge/SKILL.md').read_text(encoding='utf-8')
    data = router.replace('{{CHECKOUT}}', checkout.as_posix()).replace('{{SKILL}}', canonical.as_posix()).encode('utf-8')
    target = destination / 'SKILL.md'
    owner = destination / 'unrealbridge-router.json'
    old = target.read_bytes() if target.exists() else None
    previous = json.loads(owner.read_text(encoding='utf-8')) if owner.exists() else {}
    changed = old != data
    customized = old is not None and digest(old) != previous.get('router_sha256') and changed
    result = {'source': str(canonical), 'target': str(target), 'changed': changed,
              'customized': customized, 'legacy_runtime_present': (destination / 'scripts').exists(),
              'runtime_copied': False, 'applied': False}
    if not apply:
        return result
    if customized and not replace_customized:
        raise ValueError('Customized/unmanaged router: review preview; use --replace-customized with task authorization')
    destination.mkdir(parents=True, exist_ok=True)
    backup = checkout / '.tmp/codex-backups' / datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ') / 'skill-install'
    if changed and old is not None:
        backup.mkdir(parents=True)
        shutil.copy2(target, backup / 'SKILL.md')
        if owner.exists():
            shutil.copy2(owner, backup / owner.name)
        result['backup'] = str(backup)
    if changed:
        target.write_bytes(data)
    ownership = {'schema': 1, 'checkout': str(checkout), 'router_sha256': digest(data), 'runtime': str(canonical)}
    # Only rewrite ownership when content changes, preserving re-run idempotency.
    if previous != ownership:
        if owner.exists() and not backup.exists():
            backup.mkdir(parents=True)
            shutil.copy2(owner, backup / owner.name)
        owner.write_text(json.dumps(ownership, indent=2) + '\n', encoding='utf-8')
    if target.read_bytes() != data:
        raise OSError('Router readback failed')
    result['applied'] = True
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--destination', type=Path, default=Path.home() / '.codex/skills/unreal-bridge')
    parser.add_argument('--apply', action='store_true', help='Default is preview only')
    parser.add_argument('--replace-customized', action='store_true', help='Task-authorized replacement, with backup')
    args = parser.parse_args()
    print(json.dumps(install(Path(__file__).resolve().parents[1], args.destination,
                             apply=args.apply, replace_customized=args.replace_customized), indent=2))
