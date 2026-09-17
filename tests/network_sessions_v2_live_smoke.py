"""Real exact-owned PIE topology/late-join matrix. No Content/Config writes."""
import argparse
import json
from pathlib import Path
from network_multiclient_v3_live_smoke import _load_server


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--project',required=True); parser.add_argument('--out',required=True)
    parser.add_argument('--case',default='all',choices=['all','listen1','listen3','dedicated1','dedicated2','latejoin'])
    args=parser.parse_args(); server=_load_server()
    path=Path(args.out); path.parent.mkdir(parents=True,exist_ok=True)
    report={'status':'running','cases':[]}
    def persist(): path.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    cases=[('listen1','listen',1,False),('listen3','listen',3,False),('dedicated1','dedicated',1,False),('dedicated2','dedicated',2,False),('latejoin','dedicated',2,True)]
    try:
        for name,topology,count,late in cases:
            if args.case!='all' and args.case!=name: continue
            spec=dict(schema=server.network_sessions.SCHEMA,backend='owned_pie',topology=topology,remote_client_count=count,
                      cleanup_policy='always',join_plan=[{'client_ordinal':2,'after':'ready_conditions'}] if late else [],
                      ready_conditions=[],steps=[])
            run=server.network_sessions.NetworkSessionRun(spec,lambda op,values,owner,cleanup:server._runtime_rpc(op,values,owner,cleanup,project=args.project),path.parent/'reports')
            entry={'name':name,'run_id':run.run_id,'report_path':str(run.path)}; report['cases'].append(entry); persist()
            try:
                run.execute()
                native=run.report['ready']['network_session']
                worlds=native['network_worlds']; expected_mode='ListenServer' if topology=='listen' else 'DedicatedServer'
                assert len(worlds)==count+1,worlds
                authorities=[w for w in worlds if w['net_mode']==expected_mode]
                clients=[w for w in worlds if w['net_mode']=='Client']
                assert len(authorities)==1 and len(clients)==count,worlds
                assert authorities[0]['local_player_count']==(1 if topology=='listen' else 0),worlds
                assert all(w['local_player_count']==1 for w in clients),worlds
                assert all(p['pawn_ready'] for w in worlds for p in w['players']),worlds
                if late: assert len(run.report['joins'])==1 and run.report['joins'][0]['status']=='ready'
                # A foreign nonce cannot terminate this session.
                response=server.bridge_control_network_session('stop',run.run_id,'0'*32,'foreign-stop',project=args.project)
                result=server.bridge_wait_job(response['job_id'],wait_timeout=15,project=args.project)
                invalid=server._last_json_output(result)
                assert invalid and invalid.get('error_code')=='ScopeViolation',result
                current=run.call('v2_state'); assert current['network_session']['topology_ready']
                entry.update(status='passed',network_worlds=worlds)
            finally:
                cleanup=run.cleanup(); entry['cleanup']=cleanup; persist()
                assert cleanup['success'] and cleanup.get('cleanup_state')=='complete',cleanup
                assert not run.report['final']['pie'] and not run.report['final']['dirty_content'] and not run.report['final']['dirty_maps']
        report['status']='passed'
    except Exception as exc:
        report.update(status='failed',error=str(exc)); raise
    finally: persist()
    print(json.dumps({'status':report['status'],'cases':len(report['cases']),'out':str(path)}))


if __name__=='__main__': main()
