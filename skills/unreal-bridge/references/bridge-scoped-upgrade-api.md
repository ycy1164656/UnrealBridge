# Scoped authoring, pointer input, audio and network sessions

Use this reference for the SRUB upgrades to UnrealBridge 3.1. Read the live
manifest and tool schema before calling: source availability alone does not
prove the running DLL or MCP adapter contains a feature. The reference describes
bounded interfaces, not blanket certification of every asset class or engine.

## Entry points and ownership

| Task | MCP/native entry |
|---|---|
| Revision/scope preflight | `Upgrade.GetAuthoringSnapshot`, `bridge_submit_upgrade_validation` |
| Incremental assets | `bridge_submit_authoring_request` |
| Widget coordinates | `SlateInput.GetWidgetInputGeometry`, `bridge_submit_pointer_action` |
| Explicit PIE topology | `bridge_runtime_run` with recipe v2, `bridge_control_network_session` |
| Independent local game processes | `bridge_external_session` |
| Transient audio | `Audio.GetAudioMixContext`, `bridge_submit_audio_mix_session` |
| ShooterRoyal fixed conditions | `bridge_submit_sr_scenario` (project adapter, not generic game rules) |

Use a stable request ID for an identical retry; changed payloads require a new
ID. A submitted Job is not the native operation's terminal result. Read the Job
output and then the returned operation/session state. Cancellation needs
confirmed terminal cleanup. After host or Editor replacement, reconcile the
recorded IDs and actual effects; do not replay a persisted running command.

## Incremental asset authoring

The envelope is `unrealbridge.authoring.v1` with exactly
`schema, operation_id, context, operations, save_policy`.
Build `context` from a fresh `Upgrade.GetAuthoringSnapshot`:
preserve schema/project_identity/editor_session_id/engine_version, declare
target_packages and expected_revisions, and set a new request_id,
operation_id=`upgrade.validate`, dry_run and save_policy=`never`.
The outer save policy is `never` or `declared_targets`; previews never save.
Requests are limited to 64 KiB and 64 operations. Other than audio.routing,
one request targets one asset; audio.routing allows up to 16 named packages.

| operation_id | Model and implemented edits |
|---|---|
| anim.montage_segments | `Anim.GetMontageEditModel`; slots, source ranges, rate, loops, move/update by segment fingerprint |
| anim.notify_add / anim.notify_update | Concrete native/Blueprint Notify or NotifyState, stable GUID, track/time/duration, whitelisted typed properties |
| ai.blackboard_keys | Add bool/int/float/vector/object keys |
| ai.behavior_tree | `AI.GetBehaviorTreeEditModel`; GUID nodes, connect/reparent/order, typed properties and attached decorators/services |
| audio.sound_cue | `Audio.GetSoundCueEditModel`; Wave/Mixer/Random/Modulator/Attenuation, GUID connections, output and properties |
| audio.routing | `Audio.GetAudioRoutingModel`; class/submix parents, cue routes/sends, SoundMix overrides/timing, optional Volume Control Bus/Mix |

Use Montage/Notify model validation, `ValidateBehaviorTreeAsset` or
`ValidateSoundCueAsset` and save/reopen readback. Preview uses transient
copies; it is not authorization to undo unrelated edits or rebuild/delete an
existing graph. Preserve unrelated Dirty packages. Refuse a target already
dirty or whose revision changed rather than saving someone else's work.

Animation requires the actual same Skeleton; reverse playback and parent/child
Montage adaptation are not supported. BT composites currently support Selector
and Sequence; SimpleParallel and arbitrary node semantics require another
adapter. Sound Cue support is the explicit five-kind set, not every SoundNode.
Audio routing requires all changed old/new parent packages in the target set.
Do not enable missing AudioModulation or alter Config to make a request pass.

Examples with actual schemas and typed properties live in the checkout's
`tests/anim_authoring_live_smoke.py`, `bt_authoring_live_smoke.py`,
`audio_authoring_live_smoke.py`, and `audio_routing_live_smoke.py`.
These scripts name ShooterRoyal development assets; do not run them in another
project or substitute production assets without checking the task scope.

## Coordinates, capture and screenshots

Get the exact runtime object path through
`UMG.GetRuntimeWidgetTree("", exact_user_widget_instance_path)`; Blueprint
class filtering matches the actual class name/path, not all native ancestors.
Use `UMG.GetRuntimeWidgetState` for visibility and native geometry. Do not
assume UUserWidget.WidgetTree or GetWidgetFromName is available in UE Python.

`GetWidgetInputGeometry(world_handle, widget_path, local_player_index)`
returns widget_handle, generation, window_id and geometry_revision. The pointer
v1 request contains those fields plus world_handle/local_player_index,
request_id, coordinate_space (`widget_local` or `widget_normalized`),
button, modifiers and events. Each event has type, x, y and at_seconds;
start with move and balance down/up. Limit: 120 events, 10 seconds, 32 KiB.
Normalized coordinates must stay in [0,1]. Read
`GetPointerSequenceState`; use `CancelPointerSequence(id, world)` to stop.

Geometry is valid for at most 300 seconds; stale geometry is refused on
submission. During a sequence the same live Widget may reproject a changed
transform at unchanged local size, reporting geometry_reprojected per event.
Size, Widget, World or window replacement still ends the sequence. A partly executed
click is not safe to blindly retry. Actual hit testing rejects clipped/covered
widgets; foreign pointer capture is refused. Native events run on Slate ticks
outside the Python Editor script guard. Explicit moves must not be marked
as synthetic hover refreshes, which skip a captured widget's OnMouseMove.
The bridge never raises or activates the window. Submission requires the exact
owned PIE window and application to already be active; otherwise it returns
WindowNotActive without dispatching. Losing either during a sequence ends it
and releases only owned capture. Defer foreground input/audio acceptance while
the user is using the computer. Read-only work can use a separately coordinated
RenderOffScreen Editor session, but that is not desktop-input acceptance.

A visible decorative widget may be SelfHitTestInvisible and therefore absent
from the hit path. Query the real input receiver, and convert the visual anchor
with both live geometries (`unreal.SlateLibrary` in UE Python). Do not force
visibility or bypass NativeOnMouseButtonDown to make an input test pass.

`GetOwnedPIEWindowGeometry`, `SetOwnedPIEWindowGeometry` and
`CaptureOwnedPIEWindow` require a ready native network lease and its exact
World/window. Embedded Editor windows are refused. Window request fields:
schema=`unrealbridge.pie_window.v1`, request_id, run_id, world_handle,
window_id, width_px, height_px and dpi_scale. Client size bounds are
640..2560 by 480..1440, DPI 1..2. Resizing does not activate the window.
Minimized or maximized windows return WindowLayoutUnavailable; the bridge
never restores them, since the Windows restoration path can activate a window.
Wait for actual layout readback.
Windows/custom borders can make client and viewport sizes differ; to request
an exact rendered size, adjust the previous requested client size by
(desired viewport - measured viewport), with a bounded settlement loop.
Never label an approximate viewport as an exact screenshot resolution.
Capture includes Slate/UMG and writes the one owned diagnostic PNG under
Saved/UnrealBridge/Captures/PointerWindow/latest.png.

Official Slate Click still uses ref/button/doubleClick/modifiers. The new
coordinate interface does not add imaginary fields to Epic's Click schema.

## Network and fixed-condition experience

Recipe v2 declares backend=`owned_pie`, topology=`listen` or `dedicated`,
remote_client_count=1..4, cleanup_policy=`always` or
`retain_for_feedback`, and optional join_plan, ready_conditions,
network_profile, timeouts and typed steps. A listen server adds a local host;
a dedicated topology does not. join_plan uses consecutive client_ordinal
entries after `ready_conditions`. Packet emulation specifies outgoing lag/loss,
not observed RTT. The existing v1 client_count=3 still means listen plus two
clients; its meaning is unchanged.

Independent processes use the explicit local editor_game backend and signed
ownership records. Stop/rejoin addresses the exact owned PID, creation time,
executable and logical participant. It is not platform account authentication,
remote deployment or packaged Dedicated Server certification. Cancellation
does not authorize force-killing an Editor or an unrelated process.

For ShooterRoyal, discover the project scenario context with an exact World
and player handle. Prepare a registered scenario, observe its native evidence,
then explicitly enter the experience and retain the owned session for feedback.
Release/stop after the user is done. Framework and individual gameplay samples
remain separate; never clear a real inventory or alter global rules to
manufacture a sample. Run RPC/GAS prediction and authoritative input on native
World/Slate ticks: direct Python actor calls may force RPC callspace to Local.

## Audio audition

First query the exact World's audio_device_id with `GetAudioMixContext`.
The audio_session.v1 request has schema, request_id, world_handle,
audio_device_id, lease_seconds (5..300), sound_mix_path,
control_bus_mix_path and 1..3 sources. Each source has sound_path,
relative_location_cm (x/y/z within ±5000), volume_gain (unit linear or db,
value), pitch_ratio (0.125..8), and volume_bus_path. Linear gain is 0..4;
dB is -96..12.0411998. Empty optional mix/bus paths mean none.

Read `GetAudioMixSession(id, world)` for actual envelope/playback samples,
observed waves, AudioThread sound/Mix state, bus value, virtualization,
application/device volume and cleanup. A playing component alone is not
audible proof. If background playback is muted, keep listening acceptance
pending while the user is using the computer. Focus an owned preview only
when the user explicitly requests foreground interaction; do not overwrite
global user volume or mixer settings. Music plus effects does not substitute for a
missing dialogue sample. Human listening remains separate acceptance.

`EndAudioMixSession`, lease expiry and World teardown release owned
components and unique transient Mix/BusMix clones. Wait for ended and absent
owned effects. A concurrent asset edit is reported, and ending a session does
not restore an obsolete source snapshot.

## Integration evidence

After Header/Build changes, perform an authorized normal Editor build and
reload, then regenerate manifest/wrappers with `tools/gen_manifest.py`.
Back up existing text generation targets and sync exact source/text files,
not an entire plugin directory that may contain assets. Reload the MCP adapter
when tool discovery requires it. Source mirror hashes do not prove DLL loading.

Use the project's current execution report and capability matrix for actual
passed/unsupported/unverified coverage. For ShooterRoyal, the authority is
docs/reference/278_ShooterRoyal_UnrealBridge升级执行记录.md. ARMOURY remains
deferred; do not treat this reference or mechanical tests as user acceptance.
