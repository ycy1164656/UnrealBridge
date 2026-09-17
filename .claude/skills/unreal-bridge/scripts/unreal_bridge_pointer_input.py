"""Typed coordinates for native per-tick Slate routing, without OS pointer input."""
import copy
import json
import math
import re

SCHEMA='unrealbridge.pointer.v1'


def normalize(request):
    fields={'schema','request_id','world_handle','widget_handle','generation','window_id','geometry_revision','local_player_index','coordinate_space','button','modifiers','events'}
    if not isinstance(request,dict) or set(request)!=fields or request.get('schema')!=SCHEMA: raise ValueError('Exact pointer v1 fields required')
    value=copy.deepcopy(request)
    for field,prefix in [('world_handle','ubr:world:'),('widget_handle','ubr:widget-input:'),('window_id','window:')]:
        if not isinstance(value[field],str) or not value[field].startswith(prefix) or len(value[field])>256: raise ValueError('Native geometry handles required')
    for field,count in [('generation',32),('geometry_revision',40)]:
        if not isinstance(value[field],str) or not re.fullmatch('[a-fA-F0-9]{'+str(count)+'}',value[field]): raise ValueError('Fresh generation/revision required')
    if not isinstance(value['request_id'],str) or not re.fullmatch(r'[A-Za-z0-9_.-]{1,128}',value['request_id']): raise ValueError('Explicit request_id required')
    if type(value['local_player_index']) is not int or not 0<=value['local_player_index']<=3: raise ValueError('Local player index must be 0..3')
    if value['coordinate_space'] not in ('widget_normalized','widget_local') or value['button'] not in ('left','right','middle'): raise ValueError('Invalid coordinate space/button')
    modifiers=value['modifiers']
    if not isinstance(modifiers,list) or len(modifiers)>4 or any(type(v) is not str or v not in ('shift','ctrl','alt','cmd') for v in modifiers) or len(set(modifiers))!=len(modifiers): raise ValueError('Typed unique modifiers required')
    events=value['events']
    if not isinstance(events,list) or not 1<=len(events)<=120: raise ValueError('Pointer budget is 1..120 events')
    pressed=False; previous=-1
    for index,event in enumerate(events):
        if not isinstance(event,dict) or set(event)!={'type','x','y','at_seconds'}: raise ValueError('Exact event fields required')
        for key in ('x','y','at_seconds'):
            if type(event[key]) not in (int,float) or not math.isfinite(event[key]) or event[key]<0: raise ValueError('Finite nonnegative coordinates/time required')
        if not previous<=event['at_seconds']<=10: raise ValueError('Monotonic bounded event time required')
        previous=event['at_seconds']
        if value['coordinate_space']=='widget_normalized' and (event['x']>1 or event['y']>1): raise ValueError('Normalized coordinates must stay in [0,1]')
        if index==0 and event['type']!='move': raise ValueError('Pointer sequence begins with move')
        if event['type']=='down':
            if pressed: raise ValueError('Repeated down')
            pressed=True
        elif event['type']=='up':
            if not pressed: raise ValueError('Up without down')
            pressed=False
        elif event['type']!='move': raise ValueError('Unknown pointer event')
    if pressed: raise ValueError('Sequence must release its button')
    if len(json.dumps(value,allow_nan=False).encode())>32768: raise ValueError('Pointer request exceeds 32 KiB')
    return value


def submit(server,request,**route):
    value=normalize(request); payload=json.dumps(value,sort_keys=True,separators=(',',':'))
    return server.bridge_submit_job('import unreal\nprint(unreal.UnrealBridgeSlateInputLibrary.submit_pointer_sequence('+repr(payload)+'))',
        world_handle=value['world_handle'],idempotency_key='pointer:'+value['request_id'],run_timeout=15,**route)
