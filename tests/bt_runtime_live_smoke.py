"""Runs the authored tree on an isolated real AI Controller and movable SR character."""
import argparse
import time
from authoring_live_support import AuthoringLive
from scenario_live_support import ScenarioLive


def main():
    parser=argparse.ArgumentParser(); parser.add_argument('--project',required=True); parser.add_argument('--out',required=True)
    args=parser.parse_args(); h=AuthoringLive(args.project,args.out)
    try:
        with ScenarioLive(h,'SRSC-BT-AUTHORING') as live:
            h.report['states']=[]
            live.action('start_ai'); time.sleep(.5); state=live.refresh(); h.report['states'].append(state)
            e=state['evidence']; h.check('False Blackboard selects Idle task',int(e.get('ai_Idle_Execute',0))==1 and int(e.get('ai_Pursue_Execute',0))==0,e)
            h.check('Root service actually updates Blackboard',int(e.get('service_ticks',0))>0,e)
            live.action('target_on'); time.sleep(1); state=live.refresh(); h.report['states'].append(state)
            e=state['evidence']; h.check('Target appearance aborts lower priority Idle',int(e.get('ai_Idle_Abort',0))==1,e)
            h.check('Blueprint pursuit task executes on real tree',int(e.get('ai_Pursue_Execute',0))==1,e)
            h.check('Navigation accepts pursuit request',int(e.get('ai_Pursue_MoveAccepted',0))==1 and int(e.get('ai_Pursue_MoveRejected',0))==0,e)
            h.check('Owned character actually moves',float(e.get('distance_moved_cm',0))>10,e)
            live.action('target_off'); time.sleep(.4); state=live.refresh(); h.report['states'].append(state)
            e=state['evidence']; h.check('Target loss aborts pursuit and returns Idle',int(e.get('ai_Pursue_Abort',0))==1 and int(e.get('ai_Idle_Execute',0))==2,e)
            live.action('stop_ai'); state=live.refresh(); h.report['states'].append(state)
            h.check('Explicit cancellation stops tree',state['evidence']['tree_running']=='false',state)
        h.check('Owned AI and fixture release',all(x.get('success') and not x.get('owned_actor_paths') for x in h.report['scenario_cleanup']),h.report['scenario_cleanup'])
        h.finish()
    except Exception as exc:
        h.report.update(status='failed',error=str(exc)); h.persist(); raise


if __name__=='__main__': main()
