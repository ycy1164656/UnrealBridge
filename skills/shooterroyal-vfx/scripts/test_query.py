import importlib.util,json,copy,unittest,hashlib,tempfile,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
spec=importlib.util.spec_from_file_location('q',Path(__file__).with_name('query_vfx.py'));q=importlib.util.module_from_spec(spec);spec.loader.exec_module(q)
class QueryBehavior(unittest.TestCase):
    def setUp(self):
        if not q.CATALOG.is_file():
            self.skipTest('Select a ShooterRoyal project with SHOOTERROYAL_PROJECT_ROOT for catalog integration tests')
        self.d=json.loads(q.CATALOG.read_text(encoding='utf8'))
    def test_current_unique_facts(self):
        q.validate(self.d)
    def test_real_r6_historical_review_is_recalled(self):
        result=q.query(self.d,{'id':'VFX-4AED47FB','reviewed':True})
        self.assertEqual(len(result),1)
        self.assertEqual(result[0]['visual_state'],'timestamped_frames_reviewed')
    def test_legacy_ids_when_original_scan_backup_is_available(self):
        baseline=q.ROOT/'.tmp/codex-backups/20261009-vfx-367'/q.CATALOG.name
        if not baseline.is_file():
            self.skipTest('Original 2026-10-09 scan backup is unavailable; historical ID comparison is unverified')
        initial=json.loads(baseline.read_text(encoding='utf8'))
        by={x['path']:x['id'] for x in self.d['particles']}
        self.assertTrue(all(by.get(x['path'])==x['id'] for x in initial['particles']))
    def test_class_scope_and_path_alias(self):
        r=q.query(self.d,{'pack':'ParagonGideon','class_name':'ParticleSystem','limit':8})
        self.assertTrue(r);self.assertTrue(all(x['class']=='ParticleSystem' and x['pack']=='ParagonGideon' for x in r))
        a=self.d['particles'][0]
        self.assertEqual(q.query(self.d,{'id':a['package']})[0]['asset_id'],a['id'])
    def test_template_exclusion_unknown_tags_recall_and_family(self):
        sample=copy.deepcopy(self.d['particles'][0]);sample.update(id='UNKNOWN',asset_id='UNKNOWN',inferred_tags={},family_id='F-UNKNOWN')
        template=copy.deepcopy(sample);template.update(id='TEMPLATE',is_template=True)
        d={'particles':[sample,template]}
        self.assertEqual([x['asset_id'] for x in q.query(d,{'role':'projectile_body'})],['UNKNOWN'])
        sibling=copy.deepcopy(sample);sibling['id']='SIBLING';d['particles'].append(sibling)
        self.assertEqual(len(q.query(d,{'role':'projectile_body'})),1)
        self.assertEqual(len(q.query(d,{'role':'projectile_body','group_by_family':False})),2)
    def test_synonyms_and_reproducibility(self):
        a=q.query(self.d,{'role':'projectile_body','element':'暗','limit':8})
        self.assertEqual(a,q.query(self.d,{'role':'projectile_body','element':'dark','limit':8}))
        self.assertTrue(any('Dark' in x['path'] for x in a))
    def test_observed_full_catalog_accepts_legacy_record_shapes(self):
        results=q.query(self.d,{'observed':True,'limit':10000,'group_by_family':False})
        self.assertTrue(all(r['observation_validity']['disk_state']=='current' for r in results))
    def test_unknown_is_not_failed_and_stale_is_exposed(self):
        a=fixture();a['observation_stale']=True
        r=q.query({'particles':[a]}, {'id':a['id']})[0]
        self.assertTrue(r['observation_stale']);self.assertEqual(r['visual_state'],'reviewed')
        self.assertFalse(q.query({'particles':[a]}, {'observed':True}))
        self.assertEqual(len(q.query({'particles':[a]}, {'reviewed':True})),1)
        a.update(visual_state='unseen',observation_stale=False,revision={})
        r=q.query({'particles':[a]}, {})[0]
        self.assertEqual(r['runtime_state'],'not_exercised')
        self.assertEqual(r['observation_validity']['disk_state'],'unknown')

def fixture():
    return {'id':'FIXTURE','path':'/Fixture/System.System','class':'NiagaraSystem',
            'class_path':'/Script/Niagara.NiagaraSystem','group':'fixture','name':'System',
            'visual_state':'reviewed','file':'system.fixture','revision':{'kind':'sha256','value':'0'*64}}

class ValidityBehavior(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name);self.a=fixture()
        def revision(name):
            path=self.root/name;path.write_text(name)
            return {'file':name,'sha256':hashlib.sha256(path.read_bytes()).hexdigest()}
        source=revision('system.fixture');self.a['revision']['value']=source['sha256']
        self.a['observation_evidence']={'dependency_revisions':[revision(n) for n in ['material.fixture','texture.fixture','driver.fixture']],
            'preview_revisions':[revision('preview.json')],
            'dependency_coverage':{'state':'complete','scope':'isolated fixture preview'}}
    def result(self):
        return q.query({'particles':[self.a]}, {}, self.root)[0]
    def test_recorded_unseen_stale_and_current_filters(self):
        for state in ['recorded','unseen','not_individually_viewed','historical']:
            self.a['visual_state']=state
            self.assertFalse(q.query({'particles':[self.a]},{'reviewed':True},self.root))
            self.assertFalse(q.observation_is_current(self.a,self.root))
        self.a['visual_state']='timestamped_frames_reviewed'
        self.assertTrue(q.query({'particles':[self.a]},{'reviewed':True},self.root))
        self.assertTrue(q.observation_is_current(self.a,self.root))
        (self.root/'material.fixture').write_bytes(b'changed')
        self.assertTrue(q.query({'particles':[self.a]},{'reviewed':True},self.root))
        self.assertFalse(q.observation_is_current(self.a,self.root))
    def test_tags_require_viewing_and_time_addressed_provenance(self):
        self.a['observed_tags']={'roles':['projectile_body'],'forms':['spindle'],'motions':['forward']}
        self.assertFalse(self.result()['observed_tags']['forms'])
        self.a['observed_tags']['reviewed_frames']=[{'media_id':'sample','pts_seconds':2.5}]
        result=q.query({'particles':[self.a]},{'role':'projectile_body','form':['spindle']},self.root)[0]
        self.assertEqual(result['observed_tags']['forms'],['spindle'])
        self.assertTrue(result['observed_tag_provenance'])
        self.assertIn('observed forms preference',result['reasons'])
        self.a['visual_state']='not_individually_viewed'
        self.assertFalse(self.result()['observed_tags']['forms'])
    def test_matching_disk_does_not_certify_editor_or_human(self):
        v=self.result()['observation_validity']
        self.assertEqual(v['disk_state'],'current')
        self.assertEqual(v['runtime_state'],'unknown')
        self.assertFalse(v['current_acceptance_eligible'])
        self.assertTrue(q.observation_is_current(self.a,self.root))
        self.assertEqual(self.result()['human_state'],'pending')
    def test_source_material_texture_driver_and_preview_changes_expire(self):
        for name in ['system.fixture','material.fixture','texture.fixture','driver.fixture','preview.json']:
            with self.subTest(name=name):
                path=self.root/name;original=path.read_bytes();path.write_bytes(b'changed')
                self.assertTrue(self.result()['observation_stale'])
                self.assertFalse(q.observation_is_current(self.a,self.root))
                self.assertEqual(self.result()['visual_state'],'reviewed')
                path.write_bytes(original)
    def test_missing_dependency_and_incomplete_coverage(self):
        evidence=self.a['observation_evidence'];evidence['dependency_coverage']['state']='partial'
        self.assertEqual(self.result()['observation_validity']['disk_state'],'unknown')
        self.assertFalse(q.observation_is_current(self.a,self.root))
        (self.root/'texture.fixture').unlink()
        self.assertEqual(self.result()['observation_validity']['disk_state'],'stale')
    def test_legacy_evidence_is_unknown_not_silently_complete(self):
        self.a.pop('observation_evidence')
        v=self.result()['observation_validity']
        self.assertEqual(v['source_revision']['state'],'current')
        self.assertEqual(v['disk_state'],'unknown')
        self.assertFalse(q.observation_is_current(self.a,self.root))
    def test_legacy_text_evidence_remains_unknown(self):
        self.a['observation_evidence']='historical-recipe.json'
        self.assertEqual(self.result()['observation_validity']['disk_state'],'unknown')
        self.assertEqual(self.a['observation_evidence'],'historical-recipe.json')
    def test_malformed_revision_containers_are_unknown(self):
        for value in [None,'legacy',{},['bad-record']]:
            with self.subTest(value=value):
                self.assertEqual(q.revision_matches(value,self.root)['state'],'unknown')
    def test_malformed_revision_is_unknown_without_crash(self):
        self.a['revision']={}
        self.assertEqual(self.result()['observation_validity']['disk_state'],'unknown')
        self.assertEqual(q.revision_matches([{}],self.root)['state'],'unknown')
    def test_control_dependencies_also_expire_preview(self):
        folder=self.root/'docs/reference/data/vfx/control';folder.mkdir(parents=True)
        record={'schema':'SR-VFX-CONTROL-1','asset_id':'FIXTURE','record_id':'CONTROL',
                'revisions':[{'file':'driver.fixture','sha256':'f'*64}]}
        (folder/'control.json').write_text(json.dumps(record))
        self.assertTrue(self.result()['observation_stale'])
        self.assertFalse(q.observation_is_current(self.a,self.root))
    def test_declared_missing_control_record_is_not_current(self):
        self.a['control_record_ids']=['MISSING']
        self.assertEqual(self.result()['observation_validity']['disk_state'],'unknown')
        self.assertFalse(q.observation_is_current(self.a,self.root))
if __name__=='__main__':unittest.main(verbosity=1)
