"""Verify compiled x64 COFF guard bodies from a real Shipping target build.

This proves build-time entry gating, not a cooked game's runtime acceptance.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import struct


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def functions(path):
    data=path.read_bytes()
    assert data[:4]==b'\x00\x00\xff\xff' and struct.unpack_from('<H',data,6)[0]==0x8664,'Expected x64 bigobj COFF'
    sections,symbols,count=struct.unpack_from('<III',data,44)
    assert 56+40*sections<=len(data) and symbols+20*count+4<=len(data)
    strings=symbols+20*count; index=0; result={}
    while index<count:
        offset=symbols+index*20; raw=data[offset:offset+8]
        if raw[:4]==b'\x00'*4:
            start=strings+struct.unpack_from('<I',raw,4)[0]
            name=data[start:data.index(0,start)].decode('ascii','strict')
        else: name=raw.rstrip(b'\x00').decode('ascii','strict')
        value,section,type_,storage,aux=struct.unpack_from('<IiHBB',data,offset+8)
        if 0<section<=sections and storage==2 and type_&0x20:
            size,pointer=struct.unpack_from('<II',data,56+(section-1)*40+16)
            assert pointer+size<=len(data)
            result[name]=data[pointer+value:pointer+min(size,value+16)].hex()
        index+=1+aux
    return result


def main():
    p=argparse.ArgumentParser(); p.add_argument('--project',required=True); p.add_argument('--out',required=True); a=p.parse_args()
    root=Path(a.project).resolve().parent
    base=root/'Plugins/GameFeatures/ShooterRoyal/Intermediate/Build/Win64/x64/UnrealGame/Shipping/ShooterRoyalRuntime'
    requests={
        'SRAutomationScenarioSubsystem.cpp.obj':{
            '?ShouldCreateSubsystem@USRAutomationScenarioSubsystem@@':'32c0c3',
            '?IsScenarioCodeAvailable@USRAutomationScenarioSubsystem@@':'32c0c3',
            '?IsScenarioAccessEnabled@USRAutomationScenarioSubsystem@@':'32c0c3',
            '?GetForPlayer@USRAutomationScenarioSubsystem@@':'33c0c3'},
        'SRAutomationExternalSessionSubsystem.cpp.obj':{'?ShouldCreateSubsystem@USRAutomationExternalSessionSubsystem@@':'32c0c3'},
        'SRInputAutomationProbeWidget.cpp.obj':{'?CreateInputProbe@USRInputAutomationProbeWidget@@':'33c0c3'}}
    guards=[]; objects=[]
    for filename,expected in requests.items():
        path=base/filename; symbols=functions(path); objects.append(dict(path=str(path),sha256=sha(path)))
        for prefix,code in expected.items():
            found=[(name,body) for name,body in symbols.items() if name.startswith(prefix)]
            assert len(found)==1 and found[0][1]==code,(prefix,found)
            guards.append(dict(symbol=found[0][0],machine_code_hex=code,result='constant_false' if code.startswith('32') else 'constant_null'))
    binary=root/'Binaries/Win64/LyraGame-Win64-Shipping.exe'
    receipt=root/'Binaries/Win64/LyraGame-Win64-Shipping.target'
    target=json.loads(receipt.read_text(encoding='utf-8-sig'))
    assert target['Configuration']=='Shipping' and target['TargetName']=='LyraGame',target
    report=dict(status='passed',schema='shooterroyal.shipping_exclusion.v1',generated_utc=datetime.now(timezone.utc).isoformat(),
        target='LyraGame Win64 Shipping',binary=dict(path=str(binary),sha256=sha(binary)),receipt=dict(path=str(receipt),sha256=sha(receipt)),
        objects=objects,guards=guards,assertions=['Actual Shipping target linked']+[x['symbol'] for x in guards],
        interpretation='x64 xor al/al or eax/eax then ret: development entry gates are compiled false/null; reflected type metadata may remain',
        not_verified=['Cooked Shipping game launch and gameplay runtime acceptance'])
    out=Path(a.out); out.parent.mkdir(parents=True,exist_ok=True); out.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps(dict(status=report['status'],guards=len(guards),out=str(out))))


if __name__=='__main__': main()
