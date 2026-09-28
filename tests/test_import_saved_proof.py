import hashlib,json,tempfile,unittest,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'skills/unreal-bridge/scripts'))
from unreal_bridge_imports import ImportOrder

class ImportSavedProofTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        root=Path(self.temp.name).resolve();self.project=root/'Fixture.uproject';self.project.write_text('{}')
        self.package=root/'Content/Test.uasset';self.package.parent.mkdir();self.package.write_bytes(b'unit fixture; not an Unreal asset')
        self.view='old|'+str(root/'intermediate/sandboxes/UB_fixture')+'|lease'
        self.order=ImportOrder(root/'artifacts','fixture')
        self.order.freeze({'schema':'unrealbridge.external_import.v1','request_id':'fixture'})
        self.order.update(contract={'view':self.view},phase='imported_not_saved')
        self.actual={'ok':True,'dirty':False,'saved_in_current_view':True,'sandbox_active':False,
          'project_identity':str(self.project),'package_filename':str(self.package),
          'package_sha256':hashlib.sha256(self.package.read_bytes()).hexdigest(),'editor_session_id':'new'}
        key=str(self.package).replace('\\','/').lower();digest=hashlib.sha1(self.package.read_bytes()).hexdigest()
        self.lease={'lease_id':'lease','project':str(self.project).replace('\\','/'),'sealed':{key:digest},
          'persist_results':[{'path':key,'confirmed':True,'main_sha1':digest}]}
        self.receipt=root/'Saved/UnrealBridge/SandboxLeases/UB_fixture.json';self.receipt.parent.mkdir(parents=True)
        self.receipt.write_text(json.dumps(self.lease))

    def test_real_digest_receipt_and_new_session_required(self):
        self.assertEqual(self.order.observe_saved(self.actual)['phase'],'cold_loaded')
        self.assertEqual(self.order.observe_saved({**self.actual,'editor_session_id':'old'})['phase'],'persisted')

    def test_modified_main_file_cannot_claim_persisted(self):
        self.package.write_bytes(b'changed')
        with self.assertRaises(ValueError):self.order.observe_saved(self.actual)

    def test_dirty_and_blocked_never_count_as_saved(self):
        for field in ('dirty','save_blocked'):
            result=self.order.observe_saved({**self.actual,field:True})
            self.assertEqual(result['saved_observation']['status'],'imported_not_saved')

    def test_other_sandbox_cannot_claim_saved(self):
        with self.assertRaises(ValueError):self.order.observe_saved({**self.actual,'sandbox_active':True,'current_view':'other'})

    def test_unconfirmed_persist_cannot_claim_cold(self):
        self.lease['persist_results'][0]['confirmed']=False;self.receipt.write_text(json.dumps(self.lease))
        with self.assertRaises(ValueError):self.order.observe_saved(self.actual)

if __name__=='__main__':unittest.main()
