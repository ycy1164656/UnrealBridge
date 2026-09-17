import copy
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'.claude/skills/unreal-bridge/scripts'))
import unreal_bridge_sessions as sessions


class ExternalHostTests(unittest.TestCase):
    def test_signed_evidence_rejects_tampering_and_foreign_envelope(self):
        secret='a'*64
        value={'run_id':'b'*32,'participant_id':'server','sequence':1}
        encoded=sessions.envelope(secret,value)
        self.assertEqual(value,sessions.decode(secret,encoded))
        for invalid in [dict(encoded,payload=encoded['payload']+' '),dict(encoded,signature='0'*40),dict(encoded,extra=1)]:
            with self.assertRaises(ValueError): sessions.decode(secret,invalid)

    def test_pid_reuse_parent_and_executable_are_not_ownership(self):
        expected=dict(pid=10,creation_time=20,parent_pid=30,exe='C:/Editor.exe')
        self.assertTrue(sessions.same_process(expected,copy.deepcopy(expected)))
        for change in ({'creation_time':21},{'parent_pid':31},{'exe':'C:/Other.exe'},{'pid':11}):
            self.assertFalse(sessions.same_process(expected,dict(expected,**change)))
        self.assertFalse(sessions.same_process(expected,None))

    @unittest.skipUnless(os.name=='nt','Windows process identity backend')
    def test_real_current_process_identity(self):
        identity=sessions.process_identity(os.getpid())
        self.assertEqual(os.getpid(),identity['pid']); self.assertEqual(os.getppid(),identity['parent_pid'])
        self.assertGreater(identity['creation_time'],0)
        # Windows venv/uv launchers can set sys.executable to the redirector;
        # ownership must use the running image, independently read from HMODULE.
        import ctypes
        from ctypes import wintypes
        kernel=ctypes.WinDLL('kernel32',use_last_error=True)
        kernel.GetModuleFileNameW.argtypes=[wintypes.HMODULE,wintypes.LPWSTR,wintypes.DWORD]
        buffer=ctypes.create_unicode_buffer(32768)
        self.assertGreater(kernel.GetModuleFileNameW(None,buffer,len(buffer)),0)
        self.assertEqual(Path(buffer.value).resolve(),Path(identity['exe']))

    def test_unreadable_parent_is_not_assumed_safe(self):
        with tempfile.TemporaryDirectory() as directory:
            target=Path(directory)/'new-run'
            original=Path.stat
            def denied(path,*args,**kw):
                if path==Path(directory): raise PermissionError('unreadable ancestor')
                return original(path,*args,**kw)
            with patch.object(Path,'stat',denied):
                with self.assertRaises(PermissionError): sessions.safe_path(target,exists=False)

    def test_bounded_identifiers_cannot_escape_paths(self):
        for invalid in ('../run','run/other','run?quit','x'*65,'A'*32,2,True):
            with self.assertRaises(ValueError): sessions.identifier(invalid)
        self.assertEqual('a'*32,sessions.identifier('a'*32,32))
        with self.assertRaises(ValueError): sessions.identifier('z'*32,32)

    def test_foreign_run_pid_and_replayed_sequence_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            session=object.__new__(sessions.ExternalSession); session.root=Path(directory); session.run_id='b'*32; session.secret='a'*64
            session.processes={'server':{'pid':12,'last_sequence':10}}
            (session.root/'server').mkdir()
            valid=dict(schema='shooterroyal.external.state.v1',run_id=session.run_id,participant_id='server',pid=12,sequence=11)
            with patch.object(session,'verify_process',return_value=True):
                for change in ({'run_id':'c'*32},{'participant_id':'client-other'},{'pid':99},{'sequence':9}):
                    sessions.write_json(session.root/'server/state.json',sessions.envelope(session.secret,dict(valid,**change)))
                    with self.assertRaises(ValueError): session.read('server')
                sessions.write_json(session.root/'server/state.json',sessions.envelope(session.secret,valid))
                self.assertTrue(session.read('server')['evidence_fresh'])

    def test_late_join_cannot_use_sleep_or_unobserved_server_event(self):
        import threading
        session=object.__new__(sessions.ExternalSession); session.lock=threading.RLock(); session.processes={}
        session.spec={'max_clients':4}; session.record={'server_checkpoint':17}
        with patch.object(session,'read',return_value={'alive':True,'evidence_fresh':True,'events':[{'sequence':17,'type':'server_checkpoint'}]}),patch.object(session,'verify_server_port'),patch.object(session,'spawn') as spawn:
            for sequence in (True,16,'17'):
                with self.assertRaises(ValueError): session.join_client('player-1',after_sequence=sequence)
            spawn.assert_not_called()

    def test_silent_port_fallback_or_foreign_owner_blocks_join(self):
        session=object.__new__(sessions.ExternalSession); session.spec={'port':28773}; session.processes={'server':{'pid':17}}
        with patch.object(sessions,'udp_owners',return_value={17}):
            session.verify_server_port({'listen_address':'0.0.0.0:28773'})
            with self.assertRaises(ValueError): session.verify_server_port({'listen_address':'0.0.0.0:28774'})
        with patch.object(sessions,'udp_owners',return_value={18}):
            with self.assertRaises(ValueError): session.verify_server_port({'listen_address':'0.0.0.0:28773'})

    def test_unknown_command_never_writes(self):
        session=object.__new__(sessions.ExternalSession)
        with self.assertRaises(ValueError): session.command('server','shell')
        with self.assertRaises(ValueError): session.disconnect_client('client','restart-is-network-loss')


if __name__=='__main__': unittest.main()
