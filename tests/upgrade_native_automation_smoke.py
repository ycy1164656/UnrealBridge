"""Run the four native upgrade tests and inspect actual UE results."""
import argparse
import json
import time
from authoring_live_support import AuthoringLive

NAMES=['UnrealBridge.Upgrade.NativeValidation','UnrealBridge.Upgrade.NetworkSessionContract',
       'ShooterRoyal.BridgeUpgrade.Scenario.Lifecycle','ShooterRoyal.BridgeUpgrade.Scenario.FailureAndExpiry']


def main():
    p=argparse.ArgumentParser(); p.add_argument('--project',required=True); p.add_argument('--out',required=True)
    a=p.parse_args(); h=AuthoringLive(a.project,a.out); s=h.server
    try:
        h.check('Clean idle Editor before native automation',h.editor()=={'pie':False,'dirty':[]})
        submitted=s.bridge_submit_automation_run(test_names=NAMES,project=h.project)
        h.report['submission']=submitted; h.persist()
        h.check('Native automation scenario submitted',submitted.get('success') and submitted.get('scenario_id'),submitted)
        deadline=time.monotonic()+240
        while True:
            response=s.bridge_get_scenario(submitted['scenario_id'],project=h.project)
            state=response.get('result',response)
            h.report['scenario']=state; h.persist()
            if state.get('status') not in ('running','queued'): break
            if time.monotonic()>deadline: raise TimeoutError('Native automation did not complete')
            time.sleep(.5)
        h.check('Automation provider scenario reaches terminal success',state.get('status')=='succeeded',state)
        output=next(step for step in state['steps'] if step['id']=='results')['output']
        value=output['official_result']['result']['returnValue']
        actual=json.loads(value) if isinstance(value,str) else value
        h.report['native_results']=actual; h.persist()
        h.check('All four actual C++ tests ran and passed',actual.get('total')==len(NAMES) and actual.get('passed')==len(NAMES) and actual.get('failed')==0 and actual.get('skipped')==0,actual)
        h.finish()
    except Exception as exc:
        h.report.update(status='failed',error=str(exc)); h.persist(); raise


if __name__=='__main__': main()
