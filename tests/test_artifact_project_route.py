import sys,tempfile,unittest,os
from pathlib import Path
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'skills/unreal-bridge/scripts'))
import unreal_bridge_mcp_server as host

class ArtifactProjectRouteTests(unittest.TestCase):
    def test_explicit_local_project_survives_missing_discovery_path(self):
        with tempfile.TemporaryDirectory() as folder:
            project=Path(folder)/'Fixture.uproject';project.write_text('{}')
            with patch.dict(os.environ,{},clear=True),patch.object(host.bridge_cli,'resolve_target',side_effect=AssertionError('No discovery needed')):
                token=host._CATALOG_CONTEXT.set(None)
                try:
                    store=host._artifact_store_for(endpoint='127.0.0.1:1234',project=str(project))
                    self.assertEqual(store.root,Path(folder)/'Saved/UnrealBridge/Artifacts')
                finally:host._CATALOG_CONTEXT.reset(token)
