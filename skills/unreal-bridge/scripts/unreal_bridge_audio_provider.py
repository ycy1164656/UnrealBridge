"""Small opt-in SFX provider contract and lossless local PCM validation.

No remote provider is preselected and no credentials, uploads or network calls are
implicit. Local files and mock results never count as online generation.
"""
from __future__ import annotations
import hashlib
import json
import math
from pathlib import Path
import shutil
import struct
import wave

from unreal_bridge_production import atomic_json, canonical_bytes, identifier
from unreal_bridge_sessions import safe_path


def validate_pcm_wave(path):
    path=safe_path(path)
    if path.stat().st_size>16*1024*1024:raise ValueError('SFX file exceeds 16 MiB')
    raw=path.read_bytes()
    if raw[:4]!=b'RIFF' or raw[8:12]!=b'WAVE':raise ValueError('Actual RIFF/WAVE required')
    with wave.open(str(path),'rb') as stream:
        channels,width,rate,frames=stream.getnchannels(),stream.getsampwidth(),stream.getframerate(),stream.getnframes()
        if stream.getcomptype()!='NONE' or channels not in (1,2) or width!=2 or not 8000<=rate<=96000 or not 0<frames/rate<=10:
            raise ValueError('Supported short SFX: PCM16, 1/2 channels, 8..96kHz, at most 10 seconds')
        pcm=stream.readframes(frames)
    if len(pcm)!=frames*channels*width:raise ValueError('Truncated PCM payload')
    samples=[x[0]/32768 for x in struct.iter_unpack('<h',pcm)]
    peak=max(abs(x) for x in samples);rms=math.sqrt(sum(x*x for x in samples)/len(samples))
    active=[i//channels for i,x in enumerate(samples) if abs(x)>0.001]
    return {'format':'PCM16_WAVE','channels':channels,'sample_rate':rate,'duration_seconds':frames/rate,
            'frames':frames,'decoded':True,'peak_dbfs':20*math.log10(max(peak,1e-12)),
            'rms_dbfs':20*math.log10(max(rms,1e-12)),'clipped_samples':sum(abs(x)>=32767/32768 for x in samples),
            'leading_silence_seconds':active[0]/rate if active else frames/rate,
            'trailing_silence_seconds':(frames-1-active[-1])/rate if active else frames/rate,
            'loop_edge_delta':abs(samples[-channels]-samples[0]),'sha256':hashlib.sha256(raw).hexdigest(),
            'processing':[],'source_preserved':True,'human_accepted':False}


class AudioProviderOrder:
    """Provider adapters implement capabilities/estimate/submit/status/retrieve/cancel.

    A adapter is explicitly injected by integration code. Uncertain submission is
    never retried by this state machine, even for idempotent backends.
    """
    def __init__(self,artifact_root,work_order_id,adapter=None):
        identifier(work_order_id)
        self.root=Path(artifact_root)/'audio'/work_order_id
        self.path=self.root/'provider.json';self.adapter=adapter

    def capabilities(self):
        return self.adapter.capabilities() if self.adapter else {'configured':False,'online':False,'provider':'unconfigured',
            'import_local':True,'accepted_format':'PCM16_WAVE','max_variants':2,'max_seconds':10,'submit':False}

    def prepare(self,request):
        fields={'schema','request_id','provider','purpose','prompt','variant_count','seconds','budget_unit','budget_limit',
                'allow_paid_requests','allow_external_upload','reference_files','rights_reference'}
        if not isinstance(request,dict) or set(request)!=fields or request['schema']!='unrealbridge.audio_provider.v1':
            raise ValueError('Exact Audio Provider request schema required')
        identifier(request['request_id'])
        if type(request['variant_count']) is not int or not 1<=request['variant_count']<=2 or type(request['seconds']) not in (int,float) or not math.isfinite(request['seconds']) or not 0<request['seconds']<=10:
            raise ValueError('At most two short SFX variants')
        if any(type(request[k]) is not bool for k in ('allow_paid_requests','allow_external_upload')):
            raise ValueError('Explicit permission booleans required')
        if type(request['budget_limit']) not in (int,float) or not math.isfinite(request['budget_limit']) or request['budget_limit']<0:
            raise ValueError('Finite nonnegative cost ceiling required')
        if not isinstance(request['reference_files'],list) or len(request['reference_files'])>2:
            raise ValueError('Bounded explicit reference list required')
        if request['reference_files'] and not request['allow_external_upload']:
            raise ValueError('Reference uploads are not authorized')
        for field in ('provider','purpose','prompt','budget_unit','rights_reference'):
            if not isinstance(request[field],str) or not request[field] or len(request[field])>(4096 if field=='prompt' else 256):
                raise ValueError('Bounded purpose/prompt/provider/rights reference required')
        payload_hash=hashlib.sha256(canonical_bytes(request)).hexdigest()
        if self.path.exists():
            old=json.loads(self.path.read_text())
            if old['payload_hash']!=payload_hash:raise ValueError('Audio request changed; old operation must be reconciled')
            return old
        caps=self.capabilities()
        estimate=self.adapter.estimate(request) if self.adapter and caps.get('provider')==request['provider'] else {'known':False}
        state={'schema':'unrealbridge.audio_provider_record.v1','request':request,'payload_hash':payload_hash,
               'prompt_sha256':hashlib.sha256(request['prompt'].encode()).hexdigest(),'capabilities':caps,
               'estimate':estimate,'status':'prepared' if caps.get('configured') else 'blocked_provider_unconfigured',
               'online_generated':False,'provider_job_id':None,'outputs':[],'actual_cost':None,'human_accepted':False}
        atomic_json(self.path,state);return state

    def submit(self,approved_payload_hash):
        state=json.loads(self.path.read_text());request=state['request'];caps=state['capabilities']
        if state['payload_hash']!=approved_payload_hash:raise ValueError('Exact approved Audio payload hash required')
        if state['status'] in {'submitted','complete','outcome_unknown','submit_intent','cancel_requested'}:
            return state
        if not self.adapter or not caps.get('configured') or caps.get('provider')!=request['provider']:
            raise ValueError('Provider not configured; no online request made')
        estimate=state['estimate']
        if caps.get('online') and (not estimate.get('known') or estimate.get('unit')!=request['budget_unit'] or estimate.get('upper_bound',math.inf)>request['budget_limit'] or (estimate.get('upper_bound',0)>0 and not request['allow_paid_requests'])):
            raise ValueError('Authorized provider, known bounded cost and paid permission required')
        state['status']='submit_intent';atomic_json(self.path,state)
        try:
            result=self.adapter.submit(request,client_request_id=request['request_id'],payload_hash=state['payload_hash'])
            # Sync adapters may return files without inventing a remote job ID.
            state['provider_job_id']=result.get('job_id');state['status']='submitted'
            state['actual_cost']=result.get('actual_cost')
        except Exception:
            state['status']='outcome_unknown'
        atomic_json(self.path,state);return state

    def reconcile(self):
        state=json.loads(self.path.read_text());caps=state['capabilities']
        if not self.adapter or not caps.get('status'):
            return state
        result=self.adapter.status(job_id=state.get('provider_job_id'),client_request_id=state['request']['request_id'])
        state['provider_status']=str(result.get('status','unknown'))[:64]
        if result.get('status')=='failed':state['status']='failed'
        if result.get('status')=='complete':state['status']='provider_complete_requires_retrieval'
        if result.get('job_id'):state['provider_job_id']=str(result['job_id'])[:128]
        # Provider metadata is not an executable or arbitrary URL fetch instruction.
        atomic_json(self.path,state);return state

    def retrieve(self,*,approved_roots):
        state=json.loads(self.path.read_text())
        if not self.adapter or state['status']!='provider_complete_requires_retrieval':
            raise ValueError('A reconciled completed Provider result is required')
        result=self.adapter.retrieve(job_id=state.get('provider_job_id'),client_request_id=state['request']['request_id'])
        # Backend owns its documented download/redirect/host limits; this generic
        # contract accepts only local staged results and never fetches metadata URLs.
        outputs=self.import_local(result['paths'],approved_roots=approved_roots,
            source_type='mock' if not state['capabilities'].get('online') else 'local_file',
            rights_reference=state['request']['rights_reference'])
        if state['capabilities'].get('online'):
            outputs['online_generated']=True
            for item in outputs['outputs']:item['source_type']='online_provider'
        outputs['status']='retrieved_validated';atomic_json(self.path,outputs);return outputs

    def cancel(self):
        state=json.loads(self.path.read_text())
        if not self.adapter or not state['capabilities'].get('cancel'):raise ValueError('Provider does not support cancellation')
        self.adapter.cancel(job_id=state.get('provider_job_id'),client_request_id=state['request']['request_id'])
        state.update(status='cancel_requested',refund_confirmed=False);atomic_json(self.path,state);return state

    def import_local(self,paths,*,approved_roots,source_type='local_file',rights_reference):
        if source_type not in {'local_file','mock'} or not 1<=len(paths)<=2 or not rights_reference:
            raise ValueError('Explicit bounded source and rights reference required')
        roots=[safe_path(p) for p in approved_roots]
        if not roots:raise ValueError('Explicit local staging roots required')
        state=json.loads(self.path.read_text()) if self.path.exists() else {'status':'local_validation','outputs':[]}
        outputs=[]
        for value in paths:
            path=safe_path(value)
            if not any(path.is_relative_to(root) for root in roots):raise ValueError('Audio path outside approved staging roots')
            metrics=validate_pcm_wave(path)
            outputs.append(dict(metrics,path=str(path),source_type=source_type,rights_reference=rights_reference))
        if state.get('outputs') and state['outputs']!=outputs:raise ValueError('Existing Audio outputs differ; no silent replacement')
        state.update(status='validated_local_files',outputs=outputs,online_generated=source_type=='online_provider',human_accepted=False)
        self.root.mkdir(parents=True,exist_ok=True);atomic_json(self.path,state);return state
