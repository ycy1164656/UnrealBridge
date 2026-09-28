"""Owned local process sessions. Argument arrays, signed evidence, no shell/force kill.

The editor_game backend is explicitly uncooked UnrealEditor -game/-server; it does
not certify a packaged Server target. The project supplies its runtime observer.
"""
from __future__ import annotations
import ctypes
from ctypes import wintypes
import hashlib
import hmac
import json
import os
from pathlib import Path
import re
import secrets
import socket
import subprocess
import threading
import time
import uuid

SCHEMA = 'unrealbridge.external_session.v1'
OPERATIONS = {'observe', 'checkpoint', 'disconnect', 'network_loss', 'quit'}


def identifier(value, length=None):
    if not isinstance(value,str) or not re.fullmatch(r'[a-z0-9-]{1,64}',value):
        raise ValueError('Lowercase bounded identity required')
    if length and (len(value)!=length or not re.fullmatch(r'[a-f0-9]+',value)):
        raise ValueError('Exact lowercase hexadecimal identity required')
    return value


def safe_path(value, *, exists=True):
    path=Path(value).absolute()
    for part in (path,*path.parents):
        try:
            if part.is_symlink() or getattr(part.stat(),'st_file_attributes',0)&0x400:
                raise ValueError('Reparse points are not allowed in process control paths')
        except FileNotFoundError:
            if exists and part==path: raise
    if exists and not path.exists(): raise ValueError(f'Required path is absent: {path}')
    return path.resolve()


def digest(path):
    with Path(path).open('rb') as stream: return hashlib.file_digest(stream,'sha256').hexdigest()


def normalize(spec):
    allowed={'schema','backend','exe','uproject','map','port','max_clients','lease_seconds','network_profile'}
    if not isinstance(spec,dict) or spec.get('schema')!=SCHEMA or set(spec)-allowed or spec.get('backend')!='editor_game':
        raise ValueError('Explicit editor_game external backend and typed fields required')
    value=dict(spec)
    exe=safe_path(value.get('exe','')); project=safe_path(value.get('uproject',''))
    if exe.name.lower()!='unrealeditor.exe' or project.suffix.lower()!='.uproject' or not exe.is_file() or not project.is_file():
        raise ValueError('Exact UnrealEditor.exe and existing .uproject required')
    if not isinstance(value.get('map'),str) or not re.fullmatch(r'/ShooterRoyal/[A-Za-z0-9_/]+',value['map']):
        raise ValueError('An explicit ShooterRoyal package map required; URL options are forbidden')
    package=value['map'].removeprefix('/ShooterRoyal/')+'.umap'
    safe_path(project.parent/'Plugins/GameFeatures/ShooterRoyal/Content'/package)
    for key,default,minimum,maximum in [('port',17777,1024,65531),('max_clients',4,1,4),('lease_seconds',600,60,1200)]:
        value.setdefault(key,default)
        if type(value[key]) is not int or not minimum<=value[key]<=maximum: raise ValueError(f'Invalid bounded {key}')
    profile=value.setdefault('network_profile',{'out_lag_ms':0,'out_loss_percent':0})
    if not isinstance(profile,dict) or set(profile)!={'out_lag_ms','out_loss_percent'}: raise ValueError('Explicit outgoing profile required')
    for key,maximum in [('out_lag_ms',500),('out_loss_percent',10)]:
        if type(profile[key]) is not int or not 0<=profile[key]<=maximum: raise ValueError('Invalid emulation profile')
    value['exe']=str(exe); value['uproject']=str(project)
    return value


def process_identity(pid):
    """Query current Windows creation time, executable and parent; never trust PID alone."""
    if os.name!='nt': raise OSError('The first external-process backend supports Windows only')
    kernel=ctypes.WinDLL('kernel32',use_last_error=True)
    kernel.OpenProcess.argtypes=[wintypes.DWORD,wintypes.BOOL,wintypes.DWORD]; kernel.OpenProcess.restype=wintypes.HANDLE
    kernel.CloseHandle.argtypes=[wintypes.HANDLE]
    kernel.GetProcessTimes.argtypes=[wintypes.HANDLE,*([ctypes.POINTER(wintypes.FILETIME)]*4)]
    kernel.QueryFullProcessImageNameW.argtypes=[wintypes.HANDLE,wintypes.DWORD,wintypes.LPWSTR,ctypes.POINTER(wintypes.DWORD)]
    handle=kernel.OpenProcess(0x1000,False,int(pid))
    if not handle:
        if ctypes.get_last_error()==87: return None
        raise OSError(ctypes.get_last_error(),'Cannot inspect process identity')
    try:
        times=[wintypes.FILETIME() for _ in range(4)]
        if not kernel.GetProcessTimes(handle,*[ctypes.byref(v) for v in times]): raise ctypes.WinError(ctypes.get_last_error())
        # Exit time is nonzero while an exited process still has an open handle.
        if times[1].dwHighDateTime or times[1].dwLowDateTime: return None
        size=wintypes.DWORD(32768); buffer=ctypes.create_unicode_buffer(size.value)
        if not kernel.QueryFullProcessImageNameW(handle,0,buffer,ctypes.byref(size)): raise ctypes.WinError(ctypes.get_last_error())
        created=(times[0].dwHighDateTime<<32)|times[0].dwLowDateTime
    finally: kernel.CloseHandle(handle)
    class Entry(ctypes.Structure):
        _fields_=[('size',wintypes.DWORD),('usage',wintypes.DWORD),('pid',wintypes.DWORD),('heap',ctypes.c_size_t),('module',wintypes.DWORD),('threads',wintypes.DWORD),('parent',wintypes.DWORD),('priority',wintypes.LONG),('flags',wintypes.DWORD),('exe',wintypes.WCHAR*260)]
    kernel.CreateToolhelp32Snapshot.argtypes=[wintypes.DWORD,wintypes.DWORD]; kernel.CreateToolhelp32Snapshot.restype=wintypes.HANDLE
    kernel.Process32FirstW.argtypes=[wintypes.HANDLE,ctypes.POINTER(Entry)]
    kernel.Process32NextW.argtypes=[wintypes.HANDLE,ctypes.POINTER(Entry)]
    snapshot=kernel.CreateToolhelp32Snapshot(2,0); parent=None
    if snapshot==ctypes.c_void_p(-1).value: raise ctypes.WinError(ctypes.get_last_error())
    try:
        entry=Entry(); entry.size=ctypes.sizeof(entry); success=kernel.Process32FirstW(snapshot,ctypes.byref(entry))
        while success:
            if entry.pid==pid: parent=entry.parent; break
            success=kernel.Process32NextW(snapshot,ctypes.byref(entry))
    finally: kernel.CloseHandle(snapshot)
    if parent is None: return None
    return {'pid':int(pid),'creation_time':created,'exe':str(Path(buffer.value).resolve()),'parent_pid':int(parent)}


def same_process(expected, current):
    return bool(current and all(expected.get(k)==current.get(k) for k in ('pid','creation_time','parent_pid'))
                and os.path.normcase(expected['exe'])==os.path.normcase(current['exe']))


def udp_owners(port):
    """Actual IPv4 UDP owners, including sockets UE silently moved to another port."""
    library=ctypes.WinDLL('iphlpapi',use_last_error=True)
    query=library.GetExtendedUdpTable; query.argtypes=[ctypes.c_void_p,ctypes.POINTER(wintypes.ULONG),wintypes.BOOL,wintypes.ULONG,ctypes.c_int,wintypes.ULONG]
    size=wintypes.ULONG(); result=query(None,ctypes.byref(size),False,2,1,0)
    if result not in (0,122): raise OSError(result,'UDP ownership query failed')
    buffer=ctypes.create_string_buffer(size.value); result=query(buffer,ctypes.byref(size),False,2,1,0)
    if result: raise OSError(result,'UDP ownership query failed')
    count=wintypes.DWORD.from_buffer(buffer).value; owners=set()
    class Row(ctypes.Structure): _fields_=[('address',wintypes.DWORD),('port',wintypes.DWORD),('pid',wintypes.DWORD)]
    for index in range(count):
        row=Row.from_buffer(buffer,4+index*ctypes.sizeof(Row))
        if socket.ntohs(row.port&0xffff)==port: owners.add(row.pid)
    return owners


def normal_close_windows(expected):
    """Fallback for an owned process whose native observer failed to initialize."""
    if not same_process(expected,process_identity(expected['pid'])): raise ValueError('Process changed before normal window-close request')
    user=ctypes.WinDLL('user32',use_last_error=True)
    callback_type=ctypes.WINFUNCTYPE(wintypes.BOOL,wintypes.HWND,wintypes.LPARAM)
    user.GetWindowThreadProcessId.argtypes=[wintypes.HWND,ctypes.POINTER(wintypes.DWORD)]
    user.PostMessageW.argtypes=[wintypes.HWND,wintypes.UINT,wintypes.WPARAM,wintypes.LPARAM]
    requested=[]
    @callback_type
    def callback(window,parameter):
        pid=wintypes.DWORD(); user.GetWindowThreadProcessId(window,ctypes.byref(pid))
        if pid.value==expected['pid'] and user.IsWindowVisible(window):
            requested.append(bool(user.PostMessageW(window,0x0010,0,0)))
        return True
    user.EnumWindows.argtypes=[callback_type,wintypes.LPARAM]; user.EnumWindows(callback,0)
    return any(requested)


def sign(secret, payload): return hmac.new(secret.encode('ascii'),payload.encode('utf-8'),hashlib.sha1).hexdigest()


def envelope(secret, value):
    payload=json.dumps(value,ensure_ascii=True,sort_keys=True,separators=(',',':'),allow_nan=False)
    return {'payload':payload,'signature':sign(secret,payload)}


def decode(secret, value):
    if not isinstance(value,dict) or set(value)!={'payload','signature'} or not isinstance(value['payload'],str) or not isinstance(value['signature'],str):
        raise ValueError('Malformed signed evidence')
    if not hmac.compare_digest(value['signature'],sign(secret,value['payload'])): raise ValueError('Evidence authentication failed')
    return json.loads(value['payload'])


def write_json(path, value):
    safe_path(path,exists=False)
    temporary=path.with_suffix(path.suffix+'.'+uuid.uuid4().hex+'.pending')
    safe_path(temporary,exists=False)
    temporary.write_text(json.dumps(value,ensure_ascii=True,indent=2,allow_nan=False),encoding='utf-8')
    for attempt in range(10):
        try: os.replace(temporary,path); break
        except PermissionError:
            if attempt==9: raise
            time.sleep(.02*(attempt+1))


class ExternalSession:
    def __init__(self,spec,*,run_id=None):
        self.spec=normalize(spec); self.run_id=identifier(run_id,32) if run_id else uuid.uuid4().hex
        self.root=safe_path(Path(self.spec['uproject']).parent/'Saved/UnrealBridge/ExternalSessions'/self.run_id,exists=False)
        if self.root.exists(): raise ValueError('Run path already exists; use explicit read-only reconciliation')
        self.root.mkdir(parents=True); self.secret=secrets.token_hex(32)
        (self.root/'host.secret').write_text(self.secret,encoding='ascii')
        self.processes={}; self.handles={}; self.lock=threading.RLock(); self.cancel_event=threading.Event(); self.cleaning=False
        self.record={'schema':SCHEMA,'run_id':self.run_id,'spec':self.spec,'status':'starting','host_pid':os.getpid(),
                     'created_unix':time.time(),'processes':self.processes,'server_checkpoint':None,'operations':[]}
        self.exe_hash=digest(self.spec['exe']); self.record['exe_sha256']=self.exe_hash; self.persist()

    @classmethod
    def reconcile(cls,uproject,run_id):
        root=safe_path(Path(uproject).parent/'Saved/UnrealBridge/ExternalSessions'/identifier(run_id,32))
        record=json.loads(safe_path(root/'session.json').read_text(encoding='utf-8'))
        if record.get('run_id')!=run_id or Path(record['spec']['uproject']).resolve()!=Path(uproject).resolve(): raise ValueError('Reconciliation project/run mismatch')
        self=object.__new__(cls); self.spec=normalize(record['spec']); self.root=root; self.run_id=run_id
        self.secret=safe_path(root/'host.secret').read_text(encoding='ascii'); identifier(self.secret,64)
        self.record=record; self.processes=record['processes']; self.handles={}; self.lock=threading.RLock(); self.cancel_event=threading.Event(); self.cleaning=False; self.exe_hash=record['exe_sha256']
        self.record['executor_resumed']=False
        return self

    def persist(self):
        with self.lock: write_json(self.root/'session.json',self.record)

    def request_cancel(self):
        # Deliberately outside the process-operation lock: an in-flight wait
        # must be able to observe cancellation and release that lock.
        self.cancel_event.set()

    def verify_process(self,participant):
        identifier(participant); entry=self.processes[participant]
        current=process_identity(entry['pid'])
        if current is None: return False
        if not same_process(entry,current) or digest(self.spec['exe'])!=entry['exe_sha256']:
            raise ValueError('Process identity changed; refusing control or ownership adoption')
        return True

    def read(self,participant):
        entry=self.processes[identifier(participant)]
        live=self.verify_process(participant)
        path=safe_path(self.root/participant/'state.json',exists=False)
        try:
            if not 1<=path.stat().st_size<=65536: raise ValueError('Evidence exceeds 64 KiB')
            encoded=path.read_text(encoding='utf-8-sig')
        except (FileNotFoundError,PermissionError):
            # The native file manager's replace may expose a short rename gap.
            # Keep waiting on fresh signed state; never infer ready/disconnected.
            return {'status':'starting' if live else 'exited','participant_id':participant,'alive':live,'evidence_fresh':False}
        value=decode(self.secret,json.loads(encoded))
        if value.get('schema')!='shooterroyal.external.state.v1' or value.get('run_id')!=self.run_id or value.get('participant_id')!=participant or value.get('pid')!=entry['pid']:
            raise ValueError('Foreign run/participant/process evidence')
        seq=value.get('sequence')
        if type(seq) is not int or seq<entry.get('last_sequence',0): raise ValueError('Stale or regressed evidence sequence')
        if seq>entry.get('last_sequence',0): entry.update(last_sequence=seq,last_progress_unix=time.time())
        value['alive']=live; value['evidence_fresh']=time.time()-entry.get('last_progress_unix',0)<10
        if not live: value['status']='exited'
        return value

    def wait(self,participant,predicate,timeout=120):
        if not 0<timeout<=180: raise ValueError('Bounded wait required')
        end=time.monotonic()+timeout
        while True:
            event=getattr(self,'cancel_event',None)
            if event and event.is_set() and not getattr(self,'cleaning',False):
                raise RuntimeError('Owned operation cancelled; explicit stop owns cleanup')
            state=self.read(participant)
            if predicate(state): self.persist(); return state
            if not state.get('alive'): raise RuntimeError(f'Owned process exited before condition: {participant}')
            if time.monotonic()>=end: self.persist(); raise TimeoutError(f'Condition timed out for {participant}; last evidence sequence {state.get("sequence")}')
            time.sleep(.2)

    def spawn(self,participant,logical=None,connection=None):
        if self.cancel_event.is_set(): raise RuntimeError('Cancelled session cannot spawn new participants')
        identifier(participant)
        if participant in self.processes: raise ValueError('Participant cannot be reused')
        directory=safe_path(self.root/participant,exists=False); directory.mkdir()
        spec=self.spec; url=spec['map']
        if logical:
            identifier(logical); identifier(connection,32)
            ticket=sign(self.secret,self.run_id+'\n'+logical+'\n'+connection)
            url=f'127.0.0.1:{spec["port"]}?SRUBRun={self.run_id}?SRUBLogical={logical}?SRUBConnection={connection}?SRUBTicket={ticket}'
        args=[spec['exe'],spec['uproject'],url,'-game','-nosplash','-unattended','-nowrite','-NoSound','-NoEOS','-NoSteam',
              '-SRUBExternal',f'-SRUBRun={self.run_id}',f'-SRUBParticipant={participant}',
              f'-SRUBLag={spec["network_profile"]["out_lag_ms"]}',f'-SRUBLoss={spec["network_profile"]["out_loss_percent"]}',
              f'-seconds={spec["lease_seconds"]}',f'-abslog={directory / "game.log"}', '-logcmds=LogTemp Warning']
        if participant=='server': args+=['-server','-nullrhi',f'-port={spec["port"]}']
        else: args+=['-windowed','-ResX=1280','-ResY=720']
        # Persist intent first. A crash between spawn and identity capture is never silently adopted.
        self.record['operations'].append({'operation':'spawn','participant_id':participant,'dispatch':'starting','time':time.time()}); self.persist()
        process=subprocess.Popen(args,cwd=Path(spec['uproject']).parent,stdin=subprocess.DEVNULL,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,
                                 creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
        identity=process_identity(process.pid)
        if not identity or identity['parent_pid']!=os.getpid() or os.path.normcase(identity['exe'])!=os.path.normcase(spec['exe']):
            self.record['status']='needs_reconciliation'; self.persist(); raise RuntimeError('Spawn identity could not be established; no force termination')
        identity.update(participant_id=participant,run_id=self.run_id,exe_sha256=self.exe_hash,logical_id=logical,connection_id=connection,command_sequence=0)
        self.processes[participant]=identity; self.handles[participant]=process
        self.record['operations'][-1].update(dispatch='owned',pid=process.pid); self.persist()
        return identity

    def start_session(self):
        with self.lock:
            if self.processes: raise ValueError('Session start is not replayable')
            if udp_owners(self.spec['port']): raise ValueError('Requested server port already has a process owner')
            # Reserve-check every UE port used by the declared server before launch.
            sockets=[]
            try:
                for port in (self.spec['port'],self.spec['port']+1):
                    sock=socket.socket(socket.AF_INET,socket.SOCK_DGRAM); sockets.append(sock)
                    if hasattr(socket,'SO_EXCLUSIVEADDRUSE'): sock.setsockopt(socket.SOL_SOCKET,socket.SO_EXCLUSIVEADDRUSE,1)
                    sock.bind(('127.0.0.1',port))
            finally:
                for sock in sockets: sock.close()
            self.spawn('server')
            state=self.wait('server',lambda s:s.get('evidence_fresh') and s.get('net_mode')=='DedicatedServer' and s.get('begun_play') and s.get('local_players')==0,180)
            self.verify_server_port(state)
            self.record['status']='ready'; self.persist(); return state

    def verify_server_port(self,state):
        if not state.get('listen_address','').endswith(':'+str(self.spec['port'])) or udp_owners(self.spec['port'])!={self.processes['server']['pid']}:
            raise ValueError('Native listening address and OS UDP owner must match this exact server and declared port')

    def command(self,participant,operation,*,request_id=None,timeout=20):
        if operation not in OPERATIONS: raise ValueError('Unknown typed command')
        identifier(participant); request_id=identifier(request_id) if request_id else uuid.uuid4().hex
        with self.lock:
            if not self.verify_process(participant):
                if operation=='quit': return {'status':'exited','already_exited':True}
                raise ValueError('Process already exited')
            entry=self.processes[participant]; entry['command_sequence']+=1
            value=dict(schema='shooterroyal.external.command.v1',run_id=self.run_id,participant_id=participant,
                       sequence=entry['command_sequence'],request_id=request_id,operation=operation)
            self.record['operations'].append(dict(value,dispatch='intent')); self.persist()
            write_json(self.root/participant/'command.json',envelope(self.secret,value))
            result=self.wait(participant,lambda s:s.get('request_id')==request_id and s.get('command_sequence')==value['sequence'],timeout)
            self.record['operations'][-1]['dispatch']='acknowledged'; self.persist()
            if result.get('error'): raise RuntimeError(result['error'])
            return result

    def checkpoint(self):
        state=self.command('server','checkpoint')
        event=next(e for e in reversed(state['events']) if e['type']=='server_checkpoint')
        self.record['server_checkpoint']=event['sequence']; self.persist(); return state

    def join_client(self,logical_id,*,after_sequence=None):
        with self.lock:
            identifier(logical_id)
            active=[p for p,e in self.processes.items() if p!='server' and self.verify_process(p)]
            if len(active)>=self.spec['max_clients']: raise ValueError('Declared live-client capacity reached')
            if len(self.processes)>=25: raise ValueError('Bounded connection history reached')
            if any(self.processes[p]['logical_id']==logical_id for p in active): raise ValueError('Old client must exit before same-identity rejoin')
            server=self.read('server')
            if not server.get('evidence_fresh') or not server.get('alive'): raise ValueError('Fresh owned server evidence required')
            self.verify_server_port(server)
            if after_sequence is not None:
                if type(after_sequence) is not int or after_sequence!=self.record['server_checkpoint'] or not any(e['sequence']==after_sequence and e['type']=='server_checkpoint' for e in server.get('events',[])):
                    raise ValueError('Late join requires a verified fixed server event sequence')
            connection=uuid.uuid4().hex; participant=f'client-{connection}'
            self.spawn(participant,logical_id,connection)
            client=self.wait(participant,lambda s:s.get('evidence_fresh') and s.get('client_connected') and any(p.get('pawn') for p in s.get('players',[])),180)
            server=self.wait('server',lambda s:any(p.get('logical_id')==logical_id and p.get('connection_id')==connection and p.get('pawn') for p in s.get('players',[])),30)
            self.record['status']='running'; self.persist()
            return {'participant_id':participant,'logical_id':logical_id,'connection_id':connection,'client':client,'server':server,'after_sequence':after_sequence}

    def disconnect_client(self,participant,mode):
        if mode not in ('normal_exit','network_loss'): raise ValueError('Separate normal_exit/network_loss mechanism required')
        with self.lock:
            entry=self.processes[identifier(participant)]
            if participant=='server': raise ValueError('Client participant required')
            operation='quit' if mode=='normal_exit' else 'network_loss'
            before=self.read('server'); ack=self.command(participant,operation)
            connection=entry['connection_id']
            server=self.wait('server',lambda s:any(e['type']=='disconnected' and e['connection_id']==connection and e['sequence']>before['sequence'] for e in s.get('events',[]))
                             and not any(p.get('connection_id')==connection for p in s.get('players',[])),90)
            if mode=='network_loss':
                client=self.wait(participant,lambda s:not s.get('client_connected') and s.get('evidence_fresh'),90)
                # Observe actual packet settings before disposal; this is not a quit simulation.
                self.command(participant,'quit')
            else: client=ack
            self.wait(participant,lambda s:not s.get('alive'),60)
            self.persist(); return {'mode':mode,'before_server':before,'server':server,'client':client,'ack':ack}

    def reconnect_client(self,previous_participant):
        entry=self.processes[identifier(previous_participant)]
        if previous_participant=='server' or self.verify_process(previous_participant): raise ValueError('Exact previous client must have exited')
        server=self.read('server')
        if not any(e['type']=='disconnected' and e['connection_id']==entry['connection_id'] for e in server.get('events',[])):
            raise ValueError('Old connection termination evidence required')
        result=self.join_client(entry['logical_id']); result['previous_connection_id']=entry['connection_id']; return result

    def get_session_state(self):
        with self.lock:
            states={participant:self.read(participant) for participant in self.processes}
            self.persist(); return {'run_id':self.run_id,'status':self.record['status'],'executor_resumed':self.record.get('executor_resumed',True),'participants':states,'report_path':str(self.root/'session.json')}

    def stop_owned_session(self):
        with self.lock:
            self.cleaning=True  # cleanup waits must run even after cooperative cancellation
            self.record['status']='stopping'; self.persist(); errors={}
            for participant in sorted(self.processes,key=lambda p:p=='server'):
                try:
                    if self.verify_process(participant):
                        try: self.command(participant,'quit')
                        except (RuntimeError,TimeoutError):
                            if self.verify_process(participant) and normal_close_windows(self.processes[participant]):
                                self.record['operations'].append({'operation':'normal_window_close','participant_id':participant})
                            else: raise
                        self.wait(participant,lambda s:not s.get('alive'),60)
                except (OSError,ValueError,RuntimeError,TimeoutError) as exc: errors[participant]=str(exc)
            self.record['status']='needs_reconciliation' if errors else 'cleaned'; self.record['cleanup_errors']=errors; self.persist()
            return self.get_session_state()
