from pathlib import Path
import sys
import tempfile
import unittest
from PIL import Image
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'.claude/skills/unreal-bridge/scripts'))
from unreal_bridge_video import mux_avi, decode_avi, finalize_capture
from unreal_bridge_workflows import last_json_object


class VideoTests(unittest.TestCase):
    def test_native_pretty_job_envelope_is_not_a_nested_record(self):
        value={'ok':True,'identity':{'capture_id':'owned'},'frames':[{'index':0}]}
        import json
        self.assertEqual(last_json_object({'output':'job-id','job_result':{'output':json.dumps(value,indent=2)}}),value)

    def test_real_avi_full_decode_and_truncation(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);frames=[]
            for i in range(3):
                path=root/f'{i}.jpg';Image.new('RGB',(64,48),(i*60,30,70)).save(path);frames.append(path)
            result=mux_avi(root/'test.avi',frames,fps=3,width=64,height=48,max_bytes=1024*1024)
            self.assertTrue(result['fully_decoded']);self.assertEqual(result['decoded_frames'],3)
            self.assertAlmostEqual(result['duration_seconds'],1,places=4)
            raw=(root/'test.avi').read_bytes();(root/'partial.avi').write_bytes(raw[:-11])
            with self.assertRaises(ValueError):decode_avi(root/'partial.avi')
            with self.assertRaises(ValueError):mux_avi(root/'test.avi',frames,fps=3,width=64,height=48,max_bytes=1024*1024)

    def test_dropped_frames_remain_partial_and_pts_preserve_elapsed_time(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);capture=root/'captures/example';capture.mkdir(parents=True)
            frames=[]
            for i,t in enumerate((10.,10.1,10.3)):
                path=capture/f'{i}.jpg';Image.new('RGB',(64,48),(i*50,0,0)).save(path)
                frames.append({'index':i,'path':str(path),'platform_seconds':t,'world_seconds':t+1})
            value={'schema':'unrealbridge.capture.v1','ok':True,'status':'frames_complete','root':str(capture),
                'identity':{'capture_id':'example','world_handle':'world1','max_bytes':4*1024*1024},
                'frames':frames,'requested_fps':10,'width':64,'height':48,'partial':True,'dropped_frames':1}
            result=finalize_capture(value,artifact_root=root,events=[{'event_id':'hit','world_handle':'world1','world_seconds':11.11}])
            self.assertTrue(result['partial']);self.assertEqual(result['duplicate_output_frames'],1)
            self.assertEqual(result['keyframes'][0]['frame'],1)
            self.assertEqual(result['decode']['decoded_frames'],4)

    def test_wrong_world_or_clock_rejected_before_encoding(self):
        value={'schema':'unrealbridge.capture.v1','ok':True,'status':'frames_complete',
               'identity':{'world_handle':'owned'}}
        for event in ({'world_handle':'other','world_seconds':1},{'world_handle':'owned','world_seconds':float('nan')}):
            with self.assertRaisesRegex(ValueError,'World/clock'):
                finalize_capture(value,artifact_root='unused',events=[event])

    def test_disk_budget_rejection_preserves_existing_files(self):
        from unittest.mock import patch
        from types import SimpleNamespace
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);frame=root/'frame.jpg';Image.new('RGB',(16,16)).save(frame)
            with patch('unreal_bridge_video.shutil.disk_usage',return_value=SimpleNamespace(free=0)):
                with self.assertRaisesRegex(ValueError,'Insufficient disk'):
                    mux_avi(root/'clip.avi',[frame],fps=1,width=16,height=16,max_bytes=1024)
            self.assertTrue(frame.exists());self.assertFalse((root/'clip.avi').exists())


if __name__=='__main__':unittest.main()
