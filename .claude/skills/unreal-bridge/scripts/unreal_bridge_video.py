"""Bounded MJPEG/AVI muxing and full container/frame decoding, without downloading executables.

Capture is native, explicitly scoped and opt-in. This host module never records the
desktop/audio or changes Unreal assets. Missing samples remain visible in the manifest.
"""
from __future__ import annotations

import hashlib
import io
import json
import math
from pathlib import Path
import shutil
import struct

from unreal_bridge_production import atomic_json
from unreal_bridge_sessions import safe_path


def _chunk(tag, data):
    return tag+struct.pack('<I',len(data))+data+(b'\0' if len(data)%2 else b'')


def _list(kind, data):
    return _chunk(b'LIST',kind+data)


def _digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def _decode_jpeg(data, width, height):
    from PIL import Image
    with Image.open(io.BytesIO(data)) as picture:
        if picture.format != 'JPEG' or picture.size != (width,height):
            raise ValueError('Frame format or dimensions changed')
        picture.load()


def mux_avi(output, jpeg_paths, *, fps, width, height, max_bytes):
    output=Path(output)
    if output.exists() or not jpeg_paths or len(jpeg_paths)>601 or not 1<=fps<=30 or not 0<width<=1280 or not 0<height<=720:
        raise ValueError('Bounded new AVI output and declared dimensions required')
    if shutil.disk_usage(output.parent).free<max_bytes+16*1024*1024:
        raise ValueError('Insufficient disk budget; preserve existing evidence')
    # AVI 1.0: one MJPEG video stream, no audio, keyframes indexed in movi.
    largest=max(Path(p).stat().st_size for p in set(jpeg_paths))
    avih=struct.pack('<14I',round(1_000_000/fps),largest*fps,0,0x10,len(jpeg_paths),0,1,largest,width,height,0,0,0,0)
    strh=struct.pack('<4s4sIHH8I4h',b'vids',b'MJPG',0,0,0,0,1,fps,0,len(jpeg_paths),largest,0xffffffff,0,0,0,width,height)
    strf=struct.pack('<IiiHH4sIiiII',40,width,height,1,24,b'MJPG',width*height*3,0,0,0,0)
    hdrl=_list(b'hdrl',_chunk(b'avih',avih)+_list(b'strl',_chunk(b'strh',strh)+_chunk(b'strf',strf)))
    indices=[]; verified=set()
    with output.open('xb') as stream:
        stream.write(b'RIFF\0\0\0\0AVI '+hdrl)
        movi_offset=stream.tell();stream.write(b'LIST\0\0\0\0movi')
        for path in jpeg_paths:
            data=Path(path).read_bytes()
            if path not in verified:
                _decode_jpeg(data,width,height);verified.add(path)
            offset=stream.tell()-(movi_offset+8)
            if stream.tell()+len(data)+16*len(jpeg_paths)+32>max_bytes:
                raise ValueError('Video byte budget exceeded; partial file retained')
            stream.write(_chunk(b'00dc',data));indices.append(struct.pack('<4sIII',b'00dc',0x10,offset,len(data)))
        movi_end=stream.tell();stream.write(_chunk(b'idx1',b''.join(indices)))
        end=stream.tell();stream.seek(movi_offset+4);stream.write(struct.pack('<I',movi_end-movi_offset-8))
        stream.seek(4);stream.write(struct.pack('<I',end-8));stream.flush()
    return decode_avi(output)


def decode_avi(path):
    """Parse real RIFF/AVI chunks and decode every JPEG frame; reject partial/truncated AVI."""
    raw=Path(path).read_bytes()
    if len(raw)<12 or len(raw)>256*1024*1024 or raw[:4]!=b'RIFF' or raw[8:12]!=b'AVI ' or struct.unpack_from('<I',raw,4)[0]!=len(raw)-8:
        raise ValueError('Invalid or incomplete AVI container')
    meta={};decoded=0;index_count=0;frame_index=[];declared_index=[];movi_base=None
    def chunks(start,end,parent=b''):
        nonlocal decoded,index_count,movi_base
        at=start
        while at<end:
            if at+8>end: raise ValueError('Truncated chunk header')
            tag,size=struct.unpack_from('<4sI',raw,at);payload=at+8;limit=payload+size
            if limit>end: raise ValueError('Truncated chunk data')
            if tag==b'LIST':
                if size<4:raise ValueError('Invalid LIST')
                if raw[payload:payload+4]==b'movi':movi_base=payload
                chunks(payload+4,limit,raw[payload:payload+4])
            elif tag==b'avih':
                if size!=56:raise ValueError('Invalid AVI header')
                values=struct.unpack_from('<14I',raw,payload)
                meta.update(microseconds_per_frame=values[0],declared_frames=values[4],streams=values[6],width=values[8],height=values[9])
            elif tag==b'strh':
                if raw[payload:payload+8]!=b'vidsMJPG':raise ValueError('Only video MJPEG stream supported')
            elif tag==b'00dc' and parent==b'movi':
                if not meta:raise ValueError('Video header must precede frames')
                _decode_jpeg(raw[payload:limit],meta['width'],meta['height']);decoded+=1
                frame_index.append((b'00dc',0x10,at-movi_base,size))
            elif tag==b'idx1':
                if size%16:raise ValueError('Invalid frame index')
                index_count=size//16
                declared_index.extend(struct.unpack_from('<4sIII',raw,payload+i*16) for i in range(index_count))
            at=limit+(size%2)
        if at!=end:raise ValueError('Invalid chunk alignment')
    chunks(12,len(raw))
    if not decoded or decoded!=meta.get('declared_frames') or decoded!=index_count or declared_index!=frame_index or meta.get('streams')!=1 or not meta.get('microseconds_per_frame'):
        raise ValueError('Frame count, index or timing mismatch')
    return dict(meta,decoded_frames=decoded,duration_seconds=decoded*meta['microseconds_per_frame']/1_000_000,
                container='AVI',codec='MJPEG',audio_streams=0,sha256=hashlib.sha256(raw).hexdigest(),fully_decoded=True)


def finalize_capture(capture, *, artifact_root, events=()):
    if not isinstance(capture,dict) or capture.get('schema')!='unrealbridge.capture.v1' or capture.get('status')!='frames_complete' or not capture.get('ok'):
        raise ValueError('Terminal native frame capture required')
    identity=capture['identity']
    if not isinstance(events,(list,tuple)) or len(events)>12:raise ValueError('At most twelve explicit event anchors')
    for event in events:
        if not isinstance(event,dict) or event.get('world_handle')!=identity['world_handle'] or type(event.get('world_seconds')) not in (int,float) or not math.isfinite(event['world_seconds']):
            raise ValueError('Event World/clock mismatch')
    root=safe_path(capture['root']);artifact_root=safe_path(artifact_root)
    if root.parent != artifact_root/'captures' or root.name!=identity['capture_id']:
        raise ValueError('Capture root must match exact artifact identity')
    frames=capture['frames'];fps=capture['requested_fps']
    if not 2<=len(frames)<=600:
        raise ValueError('At least two real frames and at most 600 required')
    previous=None
    for index,frame in enumerate(frames):
        path=safe_path(frame['path'])
        if path.parent!=root or frame['index']!=index or path.suffix.lower()!='.jpg' or (previous is not None and frame['platform_seconds']<=previous):
            raise ValueError('Frame identity/order/clock mismatch')
        frame['sha256']=_digest(path);previous=frame['platform_seconds']
    start,end=frames[0]['platform_seconds'],frames[-1]['platform_seconds']
    if not 0<end-start<=21:raise ValueError('Capture clock outside duration budget')
    mapping=[];selected=[];source=0
    for output_index in range(round((end-start)*fps)+1):
        sample=start+output_index/fps
        while source+1<len(frames) and abs(frames[source+1]['platform_seconds']-sample)<abs(frames[source]['platform_seconds']-sample): source+=1
        selected.append(frames[source]['path'])
        mapping.append({'video_frame':output_index,'pts_seconds':output_index/fps,'source_frame':source,
                        'sampling_offset_seconds':sample-frames[source]['platform_seconds']})
    manifest=dict(capture,container='AVI',codec='MJPEG',video_encoded=False,measured_fps=(len(frames)-1)/(end-start),
                  duplicate_output_frames=len(selected)-len(set(selected)),pts_mapping=mapping,human_accepted=False)
    output=root/'clip.avi'
    try:
        remaining=identity['max_bytes']-sum(Path(f['path']).stat().st_size for f in frames)-1024*1024
        if remaining<=0:raise ValueError('Frame and manifest storage exhausted the combined evidence budget')
        decoded=mux_avi(output,selected,fps=fps,width=capture['width'],height=capture['height'],max_bytes=remaining)
        manifest.update(video_encoded=True,video_path=str(output),decode=decoded)
    except Exception as error:
        manifest.update(partial=True,encoding_error=str(error))
        atomic_json(root/'capture_manifest.json',manifest)
        raise
    # Events are data only. Same World game clock is required; never subtract clocks across processes.
    keyframes=[]
    for event in events:
        nearest=min(frames,key=lambda f:abs(f['world_seconds']-event['world_seconds']))
        keyframes.append({'event_id':str(event.get('event_id',''))[:96],'frame':nearest['index'],'path':nearest['path'],
                          'sha256':nearest['sha256'],'alignment_error_seconds':abs(nearest['world_seconds']-event['world_seconds']),
                          'inside_capture':frames[0]['world_seconds']<=event['world_seconds']<=frames[-1]['world_seconds']})
    manifest['keyframes']=keyframes
    atomic_json(root/'capture_manifest.json',manifest)
    return manifest
