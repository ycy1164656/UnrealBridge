"""Read-only catalog query. Unknown inferred tags remain exploratory candidates."""
import argparse, json
from pathlib import Path
from observation_validity import revision_matches as check_revisions, observation_validity, historically_reviewed
from project_paths import CATALOG_RELATIVE, project_root
ROOT=project_root()
CATALOG=ROOT/CATALOG_RELATIVE
ALIASES={'暗':'dark','黑暗':'dark','黑潮':'dark','shadow':'dark','箭':'arrow','梭':'spindle','尾迹':'trail','尾流':'trail','弹体':'projectile','飞弹':'projectile','impact':'hit','命中':'hit'}
def canonical(value): return ALIASES.get(value.casefold(),value.casefold())
def rows(catalog):
    return [a for a in catalog['particles'] if not a.get('is_template',False) and a.get('object_path',a.get('path')) and a.get('class_path',a.get('class'))]
def revision_matches(revisions, root=ROOT):
    return check_revisions(revisions, root)
def control_records(identity, field=None, root=ROOT, detail=False):
    result=[]
    for file in sorted((root/'docs/reference/data/vfx/control').glob('*.json')):
        record=json.loads(file.read_text(encoding='utf8'))
        if record.get('schema')!='SR-VFX-CONTROL-1':continue
        identities=[record.get('asset_id'),record.get('record_id'),record.get('object_path'),
                    *record.get('derived_asset_ids',[]),*record.get('derived_object_paths',[])]
        if identity not in identities:continue
        record['revision_check']=revision_matches(record.get('revisions',[]),root)
        record['record_file']=str(file.relative_to(root))
        if field:
            record['controls']=[c for c in record.get('controls',[]) if field.casefold() in json.dumps(c,ensure_ascii=False).casefold()]
        result.append(record if detail else {k:record.get(k) for k in ['record_id','record_revision','asset_id','object_path','derived_asset_ids','derived_object_paths','tool_revision','controls','revision_check','record_file','evidence_root','evidence','human_state','limits']})
    return result
def observation_is_current(asset, root=ROOT):
    if not historically_reviewed(asset):return False
    return observation_validity(asset,control_records(asset['id'],root=root),root)['disk_state']=='current'
def observation_tag_index(root):
    file=root/'docs/reference/data/vfx/observations.json'
    index={}
    if file.is_file():
        for number,record in enumerate(json.loads(file.read_text(encoding='utf8'))):
            if isinstance(record,dict) and isinstance(record.get('observed_tags'),dict):
                index.setdefault(record.get('asset_id'),[]).append({
                    **record['observed_tags'], 'reviewed_frames':record.get('reviewed_frames',[]),
                    'source':f'docs/reference/data/vfx/observations.json#{number}'})
    return index

def observed_tags(asset, index):
    result={key:[] for key in ('roles','forms','motions','elements')};provenance=[]
    if not historically_reviewed(asset):return result,provenance
    direct=asset.get('observed_tags')
    records=index.get(asset['id'],[])+([{**direct,'source':'catalog.observed_tags'}] if isinstance(direct,dict) else [])
    for tags in records:
        frames=tags.get('reviewed_frames',[])
        if not isinstance(frames,list):continue
        evidence=[frame for frame in frames if isinstance(frame,dict)
                  and (frame.get('path') or frame.get('media_id'))
                  and any(isinstance(frame.get(key),(float,int)) and not isinstance(frame.get(key),bool)
                          and frame[key]>=0 for key in ('seconds_after_action','pts_seconds','frame_number'))]
        if not evidence:continue
        accepted={}
        for key in result:
            values=tags.get(key,[])
            if not isinstance(values,list):continue
            accepted[key]=[canonical(value) for value in values if isinstance(value,str) and value]
            result[key]=sorted(set(result[key]+accepted[key]))
        provenance.append({'source':tags['source'],'tags':accepted,'reviewed_frames':evidence,
                           'scope':'historical appearance; current revision checked separately'})
    return result,provenance

def query(catalog, q, root=ROOT):
    results=[];tag_index=observation_tag_index(root)
    for a in rows(catalog):
        path=a.get('object_path',a['path']); blob=(path+' '+a.get('name','')+' '+a.get('group','')).casefold()
        tags=a.get('inferred_tags',{}); role=q.get('role'); reasons=[]
        if q.get('id') and q['id'] not in [a['id'],path,a.get('package')]:continue
        if q.get('pack') and q['pack'].casefold() not in a.get('group','').casefold():continue
        if q.get('class_name') and q['class_name']!=a['class']:continue
        if q.get('source') and q['source']!=a.get('source_id'):continue
        if q.get('family') and q['family']!=a.get('family_id'):continue
        if q.get('reviewed') and not historically_reviewed(a):continue
        if q.get('observed') and not observation_is_current(a,root):continue
        if q.get('sr_state') and a.get('sr_state','not_exercised')!=q['sr_state']:continue
        if q.get('text') and not all(canonical(t) in blob for t in q['text'].split()):continue
        observed,provenance=observed_tags(a,tag_index)
        effective={key:set(tags.get(key,[]))|set(observed[key]) for key in observed}
        if role:
            roles=effective['roles']
            if roles and role not in roles:continue
            reasons.append('observed role match' if role in observed['roles'] else 'inferred role match' if role in roles else 'unknown role: exploration')
        form=any(canonical(t) in effective['forms'] for t in q.get('form',[]))
        motion=any(canonical(t) in effective['motions'] for t in q.get('motion',[]))
        element=bool(q.get('element') and canonical(q['element']) in effective['elements'])
        observed_match=bool(role and role in observed['roles'])
        for key,terms,matched in [('forms',q.get('form',[]),form),('motions',q.get('motion',[]),motion),('elements',[q['element']] if q.get('element') else [],element)]:
            if matched:
                backed=any(canonical(term) in observed[key] for term in terms)
                observed_match=observed_match or backed
                reasons.append(('observed ' if backed else 'inferred ')+key+' preference')
        # Preferences rank evidence and recall; these are not aesthetic quality scores.
        priority=(observed_match,bool(role and role in effective['roles']),form,motion,element,historically_reviewed(a))
        results.append((priority,a,reasons,observed,provenance))
    results.sort(key=lambda x: (tuple(-int(v) for v in x[0]),x[1]['id']))
    selected=[];seen=set()
    for _,a,reasons,observed,provenance in results:
        family=a.get('family_id',a['id'])
        if q.get('group_by_family',True) and not q.get('id') and not q.get('family') and family in seen:continue
        seen.add(family)
        validity=observation_validity(a,control_records(a['id'],root=root),root)
        selected.append({'asset_id':a['id'],'path':a.get('object_path',a['path']),'class':a['class'],'pack':a['group'],'family_id':family,
                         'reasons':reasons,'inferred_tags':a.get('inferred_tags',{}),'observed_tags':observed,
                         'observed_tag_provenance':provenance,'availability':a.get('availability','unknown'),
                         'runtime_state':a.get('runtime_state','not_exercised'),'visual_state':a.get('visual_state','unseen'),
                         'sr_state':a.get('sr_state','not_exercised'),'human_state':a.get('human_state','pending'),
                         'entry_conditions':a.get('entry_conditions',{'state':'unknown'}),'media_ids':a.get('media_ids',[]),
                         'recipe_ids':a.get('recipe_ids',[]),'source_id':a.get('source_id'),
                         'observation_stale':validity['disk_state']=='stale','observation_validity':validity})
        if len(selected)>=q.get('limit',8):break
    return selected
def validate(d):
    items=d['particles'];ids=[a['id'] for a in items];paths=[a.get('object_path',a['path']) for a in items]
    assert len(ids)==len(set(ids)), 'Duplicate stable ID'
    assert len(paths)==len(set(paths)), 'Duplicate Object Path'
    assert all(a.get('class_path') and a.get('source_id') and not a.get('is_template') for a in items), 'Incomplete fact/template'
    return {'records':len(items),'unique_ids':len(set(ids)),'unique_paths':len(set(paths))}
def export_html(d,target):
    data=[{'id':a['id'],'name':a['name'],'pack':a['group'],'class':a['class'],'path':a.get('object_path',a['path']),
           'family':a.get('family_id',''),'state':a.get('availability','unknown')+' / historical '+a.get('visual_state','unseen')+' / current runtime unknown',
           'tags':json.dumps(a.get('inferred_tags',{}),ensure_ascii=False),'media':','.join(a.get('media_ids',[]))} for a in rows(d)]
    payload=json.dumps(data,ensure_ascii=False).replace('</','<\\/')
    content='''<!doctype html><meta charset="utf-8"><title>ShooterRoyal VFX全库筛选</title><style>body{font:14px system-ui;margin:24px;background:#10151d;color:#e4e8f0}input,select{padding:8px;margin:6px}table{border-collapse:collapse;width:100%}td,th{padding:8px;border-bottom:1px solid #344052;text-align:left;overflow-wrap:anywhere}th{position:sticky;top:0;background:#192231}small{color:#aab8ca}</style><h1>ShooterRoyal VFX全库目录</h1><p>事实、推测、运行、观察、接入、人工认可分别记录。此表由308唯一事实目录生成。</p><input id="q" placeholder="名称、路径、资源包、推测标签"><select id="c"><option value="">全部类型</option><option>NiagaraSystem</option><option>ParticleSystem</option><option>NiagaraEmitter</option></select><small id="n"></small><p><a href="media-index.json">实际媒体索引</a></p><table><thead><tr><th>ID/名称</th><th>来源/类型</th><th>状态/家族</th><th>实际路径</th></tr></thead><tbody id="b"></tbody></table><script>const data=PAYLOAD;const esc=s=>String(s).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));function draw(){const t=q.value.toLowerCase(),items=data.filter(x=>(!c.value||x.class===c.value)&&JSON.stringify(x).toLowerCase().includes(t));n.textContent=items.length+' / '+data.length;b.innerHTML=items.map(x=>'<tr><td>'+esc(x.id)+'<br>'+esc(x.name)+'</td><td>'+esc(x.pack)+'<br>'+esc(x.class)+'</td><td>'+esc(x.state)+'<br>'+esc(x.family)+'</td><td>'+esc(x.path)+'</td></tr>').join('')}q.oninput=c.onchange=draw;draw()</script>'''.replace('PAYLOAD',payload)
    Path(target).write_text(content,encoding='utf8')
def main():
    p=argparse.ArgumentParser();p.add_argument('--project-root',type=Path,help='ShooterRoyal project data root; defaults to SHOOTERROYAL_PROJECT_ROOT or the current project');p.add_argument('--catalog');p.add_argument('--id');p.add_argument('--pack');p.add_argument('--class',dest='class_name');p.add_argument('--source');p.add_argument('--family');p.add_argument('--detail',action='store_true');p.add_argument('--recipes-for');p.add_argument('--controls-for');p.add_argument('--control-field');p.add_argument('--role');p.add_argument('--form',nargs='*',default=[]);p.add_argument('--motion',nargs='*',default=[]);p.add_argument('--element');p.add_argument('--text');p.add_argument('--observed',action='store_true',help='Reviewed with complete matching disk evidence; does not certify current Editor memory');p.add_argument('--reviewed',action='store_true',help='Historical review, including stale or unknown evidence');p.add_argument('--sr-state');p.add_argument('--limit',type=int,default=8);p.add_argument('--all-family-members',action='store_true');p.add_argument('--export-html');p.add_argument('--validate',action='store_true');args=p.parse_args();root=project_root(args.project_root);catalog=Path(args.catalog) if args.catalog else root/CATALOG_RELATIVE;d=json.loads(catalog.read_text(encoding='utf8'));q=vars(args);q['group_by_family']=not args.all_family_members
    if args.export_html:export_html(d,args.export_html)
    result=validate(d) if args.validate else query(d,q,root)
    if args.detail:
        matched={x['asset_id']:x for x in result};result=[{**x,'observation_validity':matched[x['id']]['observation_validity'],'observation_stale':matched[x['id']]['observation_stale']} for x in rows(d) if x['id'] in matched]
    if args.recipes_for:
        recipes=[]
        for file in (root/'docs/reference/data/vfx').glob('recipe-*.json'):
            record=json.loads(file.read_text(encoding='utf8'))
            if args.recipes_for in [record.get('skill_id'),record.get('recipe_id'),*record.get('asset_ids',[])]:recipes.append(record)
        result=recipes
    if args.controls_for:result=control_records(args.controls_for,args.control_field,root=root,detail=args.detail)
    print(json.dumps(result,ensure_ascii=False,indent=2))
if __name__=='__main__':main()
