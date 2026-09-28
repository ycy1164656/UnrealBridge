"""One-time non-deleting migration to the agent-neutral maintained runtime.

Existing text is backed up before exact literal replacements. No asset or
historical report is moved, rewritten or deleted. Claude's old copy is untouched.
"""
from pathlib import Path
from datetime import datetime,timezone
import shutil,json,re

root=Path(__file__).resolve().parents[1]
source=root/'.claude/skills/unreal-bridge'
destination=root/'skills/unreal-bridge'
backup=root/'.tmp/codex-backups'/datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')/'neutral-runtime'
assert destination.resolve().is_relative_to(root.resolve())
copied=[];changed=[]
for src in source.rglob('*'):
    if not src.is_file() or '__pycache__' in src.parts or src.suffix.lower() not in {'.py','.md','.json','.txt','.ps1','.bat'}:continue
    assert not src.is_symlink()
    dst=destination/src.relative_to(source)
    if dst.exists():
        assert dst.read_bytes()==src.read_bytes(),f'Conflicting existing neutral runtime: {dst}'
        continue
    dst.parent.mkdir(parents=True,exist_ok=True)
    shutil.copy2(src,dst);copied.append(str(dst.relative_to(root)))

targets=list((root/'tests').glob('*.py'))+list((root/'tools').glob('*.py'))+[root/'README.md',root/'README.zh-CN.md']
targets += list((root/'.codex').rglob('*.md'))+list(destination.rglob('*.md'))+list(destination.rglob('*.py'))
targets += list(Path('C:/dev/ShooterRoyal_5_8_DirectUpgrade/.tmp').glob('bridge330*.py'))
targets += [Path('C:/Users/ycy/.codex/config.toml')]
for path in targets:
    if path==Path(__file__).resolve():continue
    before=path.read_text(encoding='utf-8')
    after=before.replace('.claude/skills/unreal-bridge','skills/unreal-bridge').replace('.claude\\skills\\unreal-bridge','skills\\unreal-bridge')
    after=re.sub(r'/\s*[\"\']\.claude[\"\']\s*(?=/)', '', after)
    after=after.replace('repo, ".claude", "skills"','repo, "skills"')
    if path.is_relative_to(destination):
        after=after.replace('SCRIPT_DIR.parents[3]','SCRIPT_DIR.parents[2]').replace('Path(SCRIPT_DIR).parents[3]','Path(SCRIPT_DIR).parents[2]')
        after=after.replace('scripts → unreal-bridge → skills → .claude → <repo-root>','scripts → unreal-bridge → skills → <repo-root>').replace('scripts -> unreal-bridge -> skills -> .claude -> <repo-root>','scripts -> unreal-bridge -> skills -> <repo-root>')
    if before==after:continue
    relative=path.relative_to(root) if path.is_relative_to(root) else Path('external')/path.drive.replace(':','')/Path(*path.parts[1:])
    saved=backup/relative;saved.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(path,saved)
    path.write_text(after,encoding='utf-8')
    assert path.stat().st_size and path.read_text(encoding='utf-8')==after
    changed.append(str(path))
print(json.dumps({'copied':len(copied),'changed':len(changed),'backup':str(backup),'legacy_untouched':True}))
