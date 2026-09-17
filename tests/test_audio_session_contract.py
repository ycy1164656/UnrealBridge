import unittest
from test_anim_authoring_contract import request  # scripts path setup
import unreal_bridge_audio_sessions as audio


class AudioSessionContract(unittest.TestCase):
    def example(self):
        return dict(schema=audio.SCHEMA,request_id='owned-audio-1',world_handle='world-session-1',audio_device_id=2,lease_seconds=30,
            sound_mix_path='',control_bus_mix_path='',sources=[dict(sound_path='/ShooterRoyal/Audio/Wave',relative_location_cm=dict(x=100,y=0,z=0),volume_gain=dict(unit='linear',value=.5),pitch_ratio=1,volume_bus_path='')])

    def test_job_pins_world_and_idempotency(self):
        class Server:
            def bridge_submit_job(self,code,**kwargs): return code,kwargs
        code,route=audio.submit(Server(),self.example())
        self.assertIn('begin_audio_mix_session',code); self.assertEqual('world-session-1',route['world_handle']); self.assertIn('owned-audio-1',route['idempotency_key'])

    def test_bounded_lease_device_and_sources(self):
        for field,bad in [('audio_device_id',True),('audio_device_id',-1),('lease_seconds',0),('lease_seconds',301),('sources',[]),('sources',[self.example()['sources'][0]]*4)]:
            value=self.example(); value[field]=bad
            with self.assertRaises(ValueError): audio.normalize(value)

    def test_no_untyped_gain_or_nonfinite_coordinate(self):
        for field,bad in [('volume_gain',.5),('volume_gain',dict(unit='db',value=20)),('relative_location_cm',dict(x=float('nan'),y=0,z=0)),('pitch_ratio',False)]:
            value=self.example(); value['sources'][0][field]=bad
            with self.assertRaises(ValueError): audio.normalize(value)

    def test_global_mix_profile_arguments_rejected(self):
        for field in ('restore_all','clear_all_mixes','profile_path','device_name'):
            value=self.example(); value[field]=True
            with self.assertRaises(ValueError): audio.normalize(value)


if __name__=='__main__': unittest.main()
