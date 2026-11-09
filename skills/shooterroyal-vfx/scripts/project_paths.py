"""Resolve project data independently of the version-controlled skill checkout."""
import os
from pathlib import Path

CATALOG_RELATIVE = Path('docs/reference/data/308_ShooterRoyal_VFX全库选型目录.json')


def project_root(explicit=None):
    selected = explicit if explicit is not None else os.environ.get('SHOOTERROYAL_PROJECT_ROOT')
    if selected:
        return Path(selected).expanduser().resolve()
    current = Path.cwd().resolve()
    for candidate in (current, *current.parents):
        if (candidate / CATALOG_RELATIVE).is_file():
            return candidate
    return current
