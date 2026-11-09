"""Merge a completed read-only registry scan; retain IDs and historical records."""
import argparse, json, hashlib, re, struct, shutil, datetime
from pathlib import Path
from collections import Counter,defaultdict
from project_paths import project_root
ROOT=project_root()
OUT=ROOT/'.tmp/artifacts/vfx-asset-first/Docs-Windows-Static/latest'
CAT=ROOT/'docs/reference/data/308_ShooterRoyal_VFX全库选型目录.json'
DATA=ROOT/'docs/reference/data/vfx'
KNOWN=set()
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def stable(s,prefix='VFX-'):return prefix+hashlib.sha1(s.encode()).hexdigest()[:8].upper()
def fstring(b,p):
    n=struct.unpack_from('<i',b,p)[0];p+=4
    if n==0:return '',p
    if abs(n)>100000:raise ValueError('string size')
    size=n if n>0 else -n*2
    if p+size>len(b):raise ValueError('bounds')
    s=b[p:p+size].decode('utf8' if n>0 else 'utf-16-le')
    if not s.endswith('\0'):raise ValueError('terminator')
    return s[:-1],p+size
def metadata(a,read_all=False):
    p=ROOT/a['file']
    if '__External' in a['file']:return {**a,'state':'external_object_container_excluded','class':None}
    with p.open('rb') as f:b=f.read() if read_all else f.read(4194304)
    raw=p.stem.encode()+b'\0';pattern=struct.pack('<i',len(raw))+raw;pos=0;matches={}
    while True:
        pos=b.find(pattern,pos)
        if pos<0:break
        try:
            name,k=fstring(b,pos);cls,k=fstring(b,k)
            if not re.fullmatch(r'(?:/Script/[A-Za-z0-9_]+\.)?[A-Za-z][A-Za-z0-9_]*',cls):raise ValueError('class')
            if cls.rsplit('.',1)[-1] not in KNOWN and not cls.startswith('/Script/'):raise ValueError('unknown class')
            count=struct.unpack_from('<i',b,k)[0];k+=4
            if not 0<=count<=10000:raise ValueError('tags')
            for _ in range(count):key,k=fstring(b,k);value,k=fstring(b,k)
            matches[cls]={'name':name,'class':cls.rsplit('.',1)[-1],'class_path':cls,'path':a['package']+'.'+name}
        except (ValueError,UnicodeError,struct.error):pass
        pos+=4
    if len(matches)==1:return {**a,**next(iter(matches.values())),'state':'disk_only_metadata'}
    if not matches and not read_all and p.stat().st_size>4194304:return metadata(a,True)
    return {**a,'state':'metadata_unknown','class':None,'candidates':list(matches)}
def group(p):
    if p.startswith('/ShooterRoyal/VFX/AAAProjectilesVol2/'):return 'AAAProjectilesVol2'
    return p.split('/')[2] if p.startswith('/Game/') else p.split('/')[1]
def infer(path,cls):
    s=path.casefold();name=path.rsplit('.',1)[-1];tokens=set(re.findall(r'[a-z]+',re.sub(r'([a-z])([A-Z])',r'\1_\2',name).casefold()));roles=[];forms=[];motions=[];elements=[]
    if tokens.intersection(['projectile','flight','missile','bolt','arrow']) and not tokens.intersection(['impact','hit','explosion']):roles.append('projectile_body');motions+=['forward','directional']
    if tokens.intersection(['trail','ribbon']):roles.append('trail')
    if tokens.intersection(['impact','hit','explosion']):roles.append('impact')
    if tokens.intersection(['cast','charge','spawn','summon']):roles.append('formation')
    if tokens.intersection(['aura','loop','sustain','source']):roles.append('sustain')
    if tokens.intersection(['end','dissolve','death']):roles.append('exit')
    for x in ['arrow','spindle','spear','orb','beam','wave','wall','shield','smoke']:
        if x in tokens:forms.append(x)
    for x in ['dark','shadow','fire','ice','water','earth','lightning','poison','light','air']:
        if x in s:elements.append('dark' if x=='shadow' else x)
    return {'roles':sorted(set(roles)),'forms':sorted(set(forms)),'motions':sorted(set(motions)),'elements':sorted(set(elements)),
            'basis':'path/name heuristic only; absence means unknown; not observed appearance'}
def family(path):
    p=path.split('.')[0];parent,name=p.rsplit('/',1)
    stage=re.sub(r'(?i)(projectile|flight|trail|impact|hit|cast|charge|muzzle|loop|end)','stage',name)
    stage=re.sub(r'(?i)(_base|_lod\d|_v\d+|_\d+)$','',stage)
    return stable(parent+'/'+stage,'FAM-')
def main():
    global ROOT, OUT, CAT, DATA, KNOWN
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--project-root',type=Path,help='ShooterRoyal project data root')
    args=parser.parse_args()
    ROOT=project_root(args.project_root)
    OUT=ROOT/'.tmp/artifacts/vfx-asset-first/Docs-Windows-Static/latest'
    CAT=ROOT/'docs/reference/data/308_ShooterRoyal_VFX全库选型目录.json'
    DATA=ROOT/'docs/reference/data/vfx'
    OUT.mkdir(parents=True,exist_ok=True);DATA.mkdir(parents=True,exist_ok=True)
    old=json.loads(CAT.read_text(encoding='utf8'));disk=json.loads((OUT/'disk-inventory.json').read_text(encoding='utf8'));reg=json.loads((OUT/'registry-inventory.json').read_text(encoding='utf8'))
    assert not reg['registry_loading']
    KNOWN={k.rsplit('.',1)[-1] for k in reg['classes']}|{'LyraLobbyBackground'}
    backup=ROOT/'.tmp/codex-backups/20261009-vfx-367';backup.mkdir(parents=True,exist_ok=True)
    if not (backup/CAT.name).exists():shutil.copy2(CAT,backup/CAT.name)
    meta=[metadata(a) for a in reg['unregistered_disk']]
    (OUT/'unmounted-metadata.json').write_text(json.dumps(meta,ensure_ascii=False),encoding='utf8')
    items=list(reg['assets'])+[a for a in meta if a['class'] in ['NiagaraSystem','NiagaraEmitter','ParticleSystem']]
    initial=json.loads((backup/CAT.name).read_text(encoding='utf8'))
    initialpaths={a['path'] for a in initial['particles']}
    oldby={a['path']:a for a in old['particles']};dmap={a['package']:a for a in disk['asset_files']}
    source={g['group']:g['source_id'] for g in old['groups']};source['ParagonMuriel']='SRAS-032'
    existing=DATA/'sources.json'
    if existing.exists():source.update(json.loads(existing.read_text(encoding='utf8')))
    nextid=max(36,max(int(v.split('-')[-1]) for v in source.values()))+1
    for g in sorted({group(a['package']) for a in items}-source.keys()):source[g]=f'SRAS-{nextid:03d}';nextid+=1
    existing.write_text(json.dumps(source,ensure_ascii=False,indent=2),encoding='utf8')
    current=[];bygroup=defaultdict(Counter);fams=defaultdict(list)
    for a in sorted(items,key=lambda x:x['path']):
        olda=oldby.get(a['path'],{});r=dict(olda);g=group(a['package']);file=dmap.get(a['package']);rev={'kind':'sha256','value':sha(ROOT/file['file'])} if file else {'kind':'unknown','value':'unknown'}
        r.update(id=olda.get('id',stable(a['package'])),asset_id=olda.get('id',stable(a['package'])),path=a['path'],object_path=a['path'],package=a['package'],package_path=a['package'],
                 name=a['name'],group=g,pack=g,source_id=source[g],**{'class':a['class']},class_path=a['class_path'],
                 availability='disk_only' if a.get('state')=='disk_only_metadata' else 'registered',revision=rev,
                 provenance={'scan_id':reg['scan_id'],'method':'serialized Package AssetRegistry tags' if a.get('state') else 'live completed AssetRegistry','file':file['file'] if file else None},
                 inferred_tags=infer(a['path'],a['class']),family_id=family(a['path']),runtime_state=olda.get('runtime_state','not_exercised'),
                 visual_state=olda.get('visual_state','unseen'),sr_state=olda.get('sr_state','not_exercised'),human_state=olda.get('human_state','pending'),
                 entry_conditions=olda.get('entry_conditions',{'state':'unknown','candidate_requirements':'actor/socket/target/beam inputs need inspection' if g.startswith('Paragon') else 'inspect system parameters and local/world space'}),media_ids=olda.get('media_ids',[]))
        if olda and olda.get('revision')!=rev and r['visual_state']!='unseen':r['observation_stale']=True
        if olda and not r.get('historical_suggestions'):r['historical_suggestions']={k:olda[k] for k in ['role','use','effect','combination','adaptation','validation','integration'] if k in olda}
        current.append(r);bygroup[g][a['class']]+=1;fams[r['family_id']].append(r['id'])
    currentpaths={a['path'] for a in current};missing=[]
    for path,a in oldby.items():
        if path not in currentpaths:
            r=dict(a);r.update(asset_id=a['id'],object_path=path,class_path={'NiagaraSystem':'/Script/Niagara.NiagaraSystem','ParticleSystem':'/Script/Engine.ParticleSystem','NiagaraEmitter':'/Script/Niagara.NiagaraEmitter'}[a['class']],availability='not_visible_this_scan');current.append(r);missing.append(a['id'])
    oldgroups={g['group']:g for g in initial['groups']};oldgroups.update({g['group']:g for g in old['groups']});groups=[]
    for g,c in sorted(bygroup.items()):
        x=dict(oldgroups.get(g,{}));x.update(group=g,source_id=source[g],name=x.get('name',g),niagara_count=c['NiagaraSystem'],cascade_count=c['ParticleSystem'],emitter_count=c['NiagaraEmitter'],fact_state='enumerated; unseen except separate observations');groups.append(x)
    for g,x in oldgroups.items():
        if g not in bygroup:
            y=dict(x);y.update(niagara_count=0,cascade_count=0,emitter_count=0,fact_state='No particle objects this scan; historical tool/source metadata retained');groups.append(y)
    counts=Counter(a['class'] for a in items)
    history=old.get('historical_scans',[{'date':old['date'],'counts':old['counts'],'method':old['method']}])
    old.update(schema_version='SR-VFX-CATALOG-2',date='2026-10-09',method='Full project Content/plugin discovery + completed live AssetRegistry; unmounted normal packages parsed read-only; no full-library load/compile; naming and historical recommendations are inferred.',
               counts={**old['counts'],'asset_files':len(disk['asset_files']),'registry_records':sum(reg['mount_counts'].values()),'registry_packages':reg['registry_packages'],'unmounted_files':len(meta),'niagara':counts['NiagaraSystem'],'cascade':counts['ParticleSystem'],'emitters':counts['NiagaraEmitter'],'current_particles':len(items),'retained_not_visible':len(missing),'loaded_systems':1177},
               counts_basis={'current':['asset_files','registry_records','registry_packages','unmounted_files','niagara','cascade','emitters','current_particles','retained_not_visible'],'historical_20260913':['loaded_systems','not_ready_at_first_read','phase_dash_business','tools','support','missing_paths','fluids_unmounted_paths','combinations']},
               particles=current,groups=groups,historical_scans=history,
               scan={'scan_id':reg['scan_id'],'time_unix':reg['time_unix'],'engine':reg['engine'],'mounts':disk['mounts'],'mount_counts':reg['mount_counts'],'registry_loading':False,'metadata_unknown':[a['file'] for a in meta if a['state']=='metadata_unknown'],'external_containers_excluded':sum(a['state']=='external_object_container_excluded' for a in meta)},
               delta={'added':[a['id'] for a in current if a['path'] not in initialpaths],'not_visible':missing},
               unmounted_metadata_unknown=[a['file'] for a in meta if a['state']=='metadata_unknown'])
    CAT.write_text(json.dumps(old,ensure_ascii=False,indent=2),encoding='utf8');assert CAT.stat().st_size>0
    families=[{'family_id':k,'asset_ids':v,'relation_type':'stage_or_variant_candidate','basis':'path/name normalization','confirmation':'inferred'} for k,v in fams.items()]
    (DATA/'families.json').write_text(json.dumps(families,ensure_ascii=False,indent=2),encoding='utf8')
    initialgroups={g['group'] for g in initial['groups']}
    summary={'counts':old['counts'],'groups':len(groups),'new_sources':[{k:g[k] for k in ['group','source_id','niagara_count','cascade_count','emitter_count']} for g in groups if g['group'] not in initialgroups],
             'metadata_unknown':old['scan']['metadata_unknown'],'missing':missing,'added':len(old['delta']['added']),'families':len(families),'catalog_sha256':sha(CAT)}
    (OUT/'scan-summary.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2),encoding='utf8')
    print(json.dumps({**{k:v for k,v in summary.items() if k not in ['new_sources','metadata_unknown']},'metadata_unknown_count':len(summary['metadata_unknown'])},ensure_ascii=False))
if __name__=='__main__':main()
