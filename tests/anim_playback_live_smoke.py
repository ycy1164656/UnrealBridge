"""Actual World-ticked Montage/Notify semantics on isolated ShooterRoyal subjects."""
import argparse
import time
from authoring_live_support import AuthoringLive
from scenario_live_support import ScenarioLive


def main():
    parser=argparse.ArgumentParser(); parser.add_argument('--project',required=True); parser.add_argument('--out',required=True)
    args=parser.parse_args(); h=AuthoringLive(args.project,args.out)
    try:
        with ScenarioLive(h,'SRSC-ANIMATION-AUTHORING') as live:
            h.report['playback']=[]
            for action,expected,wait in [('play_once',(1,1,1,1),2),('play_interrupt',(1,1,1,0),.8),('play_jump',(1,1,1,1),1.7),('play_loop_cancel',(3,3,3,0),2.5)]:
                before=live.refresh()['evidence']; state=live.action(action)
                h.check(action+' accepted on real AnimInstance',float(state['evidence']['play_return_seconds'])>0,state)
                time.sleep(wait); state=live.refresh(); after=state['evidence']
                keys=['anim_Native_Notify','anim_Window_Begin','anim_Window_End','anim_Blueprint_Notify']
                delta=tuple(int(after.get(k,0))-int(before.get(k,0)) for k in keys)
                h.report['playback'].append({'action':action,'expected':expected,'actual':delta,'state':state}); h.persist()
                h.check(action+' native/Blueprint/State callbacks match actual timeline',delta==expected,delta)
                h.check(action+' ends owned montage',after.get('montage_active')=='false',after)
        h.check('Scenario owned actors released',all(x.get('success') and not x.get('owned_actor_paths') for x in h.report['scenario_cleanup']),h.report['scenario_cleanup'])
        h.finish()
    except Exception as exc:
        h.report.update(status='failed',error=str(exc)); h.persist(); raise


if __name__=='__main__': main()
