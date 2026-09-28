import sys
from pathlib import Path
import tempfile
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'skills/unreal-bridge/scripts'))
from unreal_bridge_workflows import ArtifactStore

class Utf8ArtifactTests(unittest.TestCase):
    def test_every_small_page_reconstructs_chinese_and_emoji(self):
        with tempfile.TemporaryDirectory() as root:
            store=ArtifactStore(root)
            record=store.put({'text':'中🙂文'*19},kind='test')
            expected=store._safe_path(record.artifact_id).read_bytes()
            for budget in range(1,9):
                offset=0;chunks=[]
                while True:
                    page=store.read(record.artifact_id,offset=offset,max_bytes=budget)
                    chunks.append(page['content'].encode('utf-8'))
                    if page['next_offset'] is None:break
                    self.assertGreater(page['next_offset'],offset)
                    offset=page['next_offset']
                self.assertEqual(b''.join(chunks),expected)

if __name__=='__main__':unittest.main()
