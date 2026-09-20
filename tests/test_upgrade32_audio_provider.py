import math
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import wave
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'.claude/skills/unreal-bridge/scripts'))
from unreal_bridge_audio_provider import AudioProviderOrder,validate_pcm_wave


class FakeProvider:
    def __init__(self,lose=False):self.calls=0;self.lose=lose
    def capabilities(self):return {'configured':True,'online':False,'provider':'mock','status':True,'cancel':True}
    def estimate(self,request):return {'known':True,'unit':'credits','upper_bound':0}
    def submit(self,request,**kwargs):
        self.calls+=1
        if self.lose:raise TimeoutError('Response lost after receipt')
        return {'job_id':'mock-job','actual_cost':0}
    def status(self,**kwargs):return {'status':'complete','job_id':'mock-job'}
    def cancel(self,**kwargs):pass


class AudioTests(unittest.TestCase):
    def request(self):return {'schema':'unrealbridge.audio_provider.v1','request_id':'request1','provider':'mock','purpose':'hit',
        'prompt':'short impact','variant_count':2,'seconds':.2,'budget_unit':'credits','budget_limit':0,
        'allow_paid_requests':False,'allow_external_upload':False,'reference_files':[],'rights_reference':'test fixture'}

    def test_lost_response_never_resubmits_or_claims_online(self):
        with tempfile.TemporaryDirectory() as folder:
            provider=FakeProvider(True);order=AudioProviderOrder(folder,'order',provider)
            frozen=order.prepare(self.request());state=order.submit(frozen['payload_hash'])
            self.assertEqual(state['status'],'outcome_unknown')
            order.submit(frozen['payload_hash']);self.assertEqual(provider.calls,1)
            self.assertFalse(order.reconcile()['online_generated'])
            self.assertFalse(order.cancel()['refund_confirmed'])

    def test_unknown_provider_and_upload_refused(self):
        with tempfile.TemporaryDirectory() as folder:
            order=AudioProviderOrder(folder,'order');state=order.prepare(self.request())
            with self.assertRaises(ValueError):order.submit(state['payload_hash'])
            request=self.request();request['reference_files']=['not-authorized.wav']
            with self.assertRaises(ValueError):order.prepare(request)

    def test_real_pcm_decode_scope_provenance_and_truncation(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);path=root/'sample.wav'
            with wave.open(str(path),'wb') as out:
                out.setparams((1,2,48000,0,'NONE','not compressed'))
                out.writeframes(b''.join(struct.pack('<h',round(10000*math.sin(i*.3))) for i in range(9600)))
            metrics=validate_pcm_wave(path);self.assertTrue(metrics['decoded']);self.assertEqual(metrics['duration_seconds'],.2)
            order=AudioProviderOrder(root,'local');state=order.import_local([path],approved_roots=[root],rights_reference='synthetic fixture')
            self.assertFalse(state['online_generated']);self.assertFalse(state['human_accepted'])
            with self.assertRaises(ValueError):order.import_local([path],approved_roots=[],rights_reference='fixture')
            raw=path.read_bytes();(root/'partial.wav').write_bytes(raw[:-9])
            with self.assertRaises(ValueError):validate_pcm_wave(root/'partial.wav')


if __name__=='__main__':unittest.main()
