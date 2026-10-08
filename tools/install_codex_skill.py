"""Compatibility entry for the shared AI agent router installer."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from install_agent_skill import install, main  # noqa: E402,F401


if __name__ == '__main__':
    main(Path.home() / '.codex/skills/unreal-bridge')
