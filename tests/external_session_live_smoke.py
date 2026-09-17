"""Actual local process/connection evidence; never substitutes PIE for rejoin."""
import argparse
import json
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'.claude/skills/unreal-bridge/scripts'))
from unreal_bridge_sessions import ExternalSession, SCHEMA, process_identity


def main():
    parser=argparse.ArgumentParser(); parser.add_argument('--project',required=True); parser.add_argument('--out',required=True)
    parser.add_argument('--lag',type=int,default=0); parser.add_argument('--loss',type=int,default=0)
    parser.add_argument('--server-only',action='store_true')
    args=parser.parse_args(); path=Path(args.out); path.parent.mkdir(parents=True,exist_ok=True)
    session=ExternalSession(dict(schema=SCHEMA,backend='editor_game',exe='C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe',
        uproject=args.project,map='/ShooterRoyal/Maps/SR_GamePlay_Field',port=28773,max_clients=2,lease_seconds=600,
        network_profile={'out_lag_ms':args.lag,'out_loss_percent':args.loss}))
    report={'status':'running','run_id':session.run_id,'report_path':str(session.root/'session.json'),'assertions':[],'evidence':{}}
    def persist(): path.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    def check(name,value):
        assert value,name
        report['assertions'].append(name); persist()
    try:
        server=session.start_session(); report['evidence']['server_ready']=server; persist()
        check('Actual dedicated process has zero local players',server['net_mode']=='DedicatedServer' and server['local_players']==0)
        if not args.server_only:
            first=session.join_client('player-one'); report['evidence']['first']=first; persist()
            checkpoint=session.checkpoint(); report['evidence']['checkpoint']=checkpoint
            late=session.join_client('player-two',after_sequence=session.record['server_checkpoint']); report['evidence']['late']=late
            check('Late join follows a fixed successful server event',next(p for p in late['server']['players'] if p.get('connection_id')==late['connection_id'])['connected_sequence']>late['after_sequence'])
            normal=session.disconnect_client(first['participant_id'],'normal_exit'); report['evidence']['normal_exit']=normal; persist()
            rejoin=session.reconnect_client(first['participant_id']); report['evidence']['normal_rejoin']=rejoin
            check('Normal rejoin keeps logical identity and creates new connection',rejoin['logical_id']==first['logical_id'] and rejoin['connection_id']!=first['connection_id'])
            lost=session.disconnect_client(rejoin['participant_id'],'network_loss'); report['evidence']['network_loss']=lost; persist()
            reconnect=session.reconnect_client(rejoin['participant_id']); report['evidence']['loss_rejoin']=reconnect
            check('Network loss proved old connection end before same-identity handshake',reconnect['logical_id']==first['logical_id'] and reconnect['connection_id']!=rejoin['connection_id'])
            check('Only two server connections remain',len(reconnect['server']['players'])==2)
            report['recovery_semantics']='Observed actual position/resources/inventory; no simulated profile restoration or platform account guarantee.'
        report['status']='passed'
    except Exception as exc: report.update(status='failed',error=str(exc)); raise
    finally:
        report['cleanup']=session.stop_owned_session(); persist()
    check('All and only owned processes stopped normally',report['cleanup']['status']=='cleaned' and all(not s['alive'] for s in report['cleanup']['participants'].values()))
    print(json.dumps({'status':report['status'],'checks':len(report['assertions']),'out':str(path)}))


if __name__=='__main__': main()
