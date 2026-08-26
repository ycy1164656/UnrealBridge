# UE 5.8.1 ToolsetRegistry 能力审计

> 生成时间：`2026-08-26T10:17:23+00:00`
> 引擎：`5.8.1-0+UE5`
> Provider：`EpicToolsetRegistry`
> Toolset：`53`；Tool：`832`

通用执行策略：只有官方 schema 明确标记为只读的 `Reuse` 工具可通过通用适配器执行；`Wrap`、`Extend` 和 `Reject` 均保持阻止，直至存在经过验证的 typed wrapper。

## 汇总

| Reuse | Wrap | Extend | Reject |
|---:|---:|---:|---:|
| 2 | 0 | 776 | 54 |

## Toolset 明细

| Toolset | Module | Version | Tools | Reuse | Wrap | Extend | Reject |
|---|---|---|---:|---:|---:|---:|---:|
| `aimodule_toolset.toolsets.behavior_tree.BehaviorTreeTools` | `AIModuleToolset` | `1.0` | 7 | 0 | 0 | 7 | 0 |
| `animation_toolset.toolsets.conditions.SequencerConditionTools` | `AnimationAssistantToolset` | `1.0` | 9 | 0 | 0 | 9 | 0 |
| `animation_toolset.toolsets.controlrig.ControlRigTools` | `AnimationAssistantToolset` | `1.0` | 44 | 0 | 0 | 42 | 2 |
| `animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools` | `AnimationAssistantToolset` | `1.0` | 72 | 0 | 0 | 70 | 2 |
| `animation_toolset.toolsets.custom_bindings.SequencerCustomBindingTools` | `AnimationAssistantToolset` | `1.0` | 8 | 0 | 0 | 8 | 0 |
| `animation_toolset.toolsets.import_export.SequencerImportExportTools` | `AnimationAssistantToolset` | `1.0` | 6 | 0 | 0 | 6 | 0 |
| `animation_toolset.toolsets.keyframing.SequencerKeyframingTools` | `AnimationAssistantToolset` | `1.0` | 22 | 0 | 0 | 21 | 1 |
| `animation_toolset.toolsets.outliner.SequencerOutlinerTools` | `AnimationAssistantToolset` | `1.0` | 18 | 0 | 0 | 18 | 0 |
| `animation_toolset.toolsets.sequencer.SequencerTools` | `AnimationAssistantToolset` | `1.0` | 140 | 0 | 0 | 129 | 11 |
| `AutomationTestToolset.AutomationTestToolset` | `AutomationTestToolset` | `1.0` | 7 | 0 | 0 | 7 | 0 |
| `ConfigSettingsToolset.ConfigSettingsToolset` | `ConfigSettingsToolset` | `1.0` | 8 | 0 | 0 | 8 | 0 |
| `conversation_toolset.toolsets.conversation.ConversationTools` | `ConversationToolset` | `1.0` | 7 | 0 | 0 | 7 | 0 |
| `DataflowAgent.DataflowAgentToolset` | `DataflowAgent` | `1.0` | 22 | 0 | 0 | 19 | 3 |
| `DataRegistryToolset.DataRegistryTools` | `DataRegistryToolset` | `1.0` | 7 | 0 | 0 | 7 | 0 |
| `editor_toolset.toolsets.actor.ActorTools` | `EditorToolset` | `1.0` | 17 | 0 | 0 | 15 | 2 |
| `editor_toolset.toolsets.asset.AssetTools` | `EditorToolset` | `1.0` | 21 | 0 | 0 | 20 | 1 |
| `editor_toolset.toolsets.blueprint.BlueprintTools` | `EditorToolset` | `1.0` | 53 | 0 | 0 | 48 | 5 |
| `editor_toolset.toolsets.curve_table.CurveTableTools` | `EditorToolset` | `1.0` | 9 | 0 | 0 | 8 | 1 |
| `editor_toolset.toolsets.data_asset.DataAssetTools` | `EditorToolset` | `1.0` | 1 | 0 | 0 | 1 | 0 |
| `editor_toolset.toolsets.data_table.DataTableTools` | `EditorToolset` | `1.0` | 10 | 0 | 0 | 9 | 1 |
| `editor_toolset.toolsets.material.MaterialTools` | `EditorToolset` | `1.0` | 22 | 0 | 0 | 19 | 3 |
| `editor_toolset.toolsets.material_instance.MaterialInstanceTools` | `EditorToolset` | `1.0` | 13 | 0 | 0 | 13 | 0 |
| `editor_toolset.toolsets.object.ObjectTools` | `EditorToolset` | `1.0` | 6 | 0 | 0 | 6 | 0 |
| `editor_toolset.toolsets.primitive.PrimitiveTools` | `EditorToolset` | `1.0` | 4 | 0 | 0 | 4 | 0 |
| `editor_toolset.toolsets.programmatic.ProgrammaticToolset` | `EditorToolset` | `1.0` | 2 | 0 | 0 | 2 | 0 |
| `editor_toolset.toolsets.scene.SceneTools` | `EditorToolset` | `1.0` | 20 | 0 | 0 | 18 | 2 |
| `editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools` | `EditorToolset` | `1.0` | 22 | 0 | 0 | 21 | 1 |
| `editor_toolset.toolsets.static_mesh.StaticMeshTools` | `EditorToolset` | `1.0` | 16 | 0 | 0 | 14 | 2 |
| `editor_toolset.toolsets.string_table.StringTableTools` | `EditorToolset` | `1.0` | 8 | 0 | 0 | 7 | 1 |
| `editor_toolset.toolsets.texture.TextureTools` | `EditorToolset` | `1.0` | 2 | 0 | 0 | 2 | 0 |
| `EditorToolset.EditorAppToolset` | `EditorToolset` | `1.0` | 21 | 0 | 0 | 21 | 0 |
| `EditorToolset.LogsToolset` | `EditorToolset` | `1.0` | 4 | 0 | 0 | 4 | 0 |
| `GameFeaturesToolset.GameFeaturesToolset` | `GameFeaturesToolset` | `1.0` | 7 | 0 | 0 | 7 | 0 |
| `GameplayTagsToolset.GameplayTagsToolset` | `GameplayTagsToolset` | `1.0` | 6 | 0 | 0 | 5 | 1 |
| `GASToolsets.AbilitySystemInspectorToolset` | `GASToolsets` | `1.0` | 4 | 0 | 0 | 4 | 0 |
| `GASToolsets.AttributeSetToolset` | `GASToolsets` | `1.0` | 2 | 0 | 0 | 2 | 0 |
| `GASToolsets.GameplayCueToolset` | `GASToolsets` | `1.0` | 8 | 0 | 0 | 7 | 1 |
| `NiagaraToolsets.NiagaraToolset_Assets` | `NiagaraToolsets` | `1.0` | 3 | 0 | 0 | 3 | 0 |
| `NiagaraToolsets.NiagaraToolset_Blueprint` | `NiagaraToolsets` | `1.0` | 2 | 0 | 0 | 2 | 0 |
| `NiagaraToolsets.NiagaraToolset_Component` | `NiagaraToolsets` | `1.0` | 4 | 0 | 0 | 4 | 0 |
| `NiagaraToolsets.NiagaraToolset_Info` | `NiagaraToolsets` | `1.0` | 1 | 0 | 0 | 1 | 0 |
| `NiagaraToolsets.NiagaraToolset_System` | `NiagaraToolsets` | `1.0` | 46 | 0 | 0 | 41 | 5 |
| `PCGToolset.PCGSpatialToolset` | `PCGToolset` | `1.0` | 1 | 0 | 0 | 1 | 0 |
| `PCGToolset.PCGToolset` | `PCGToolset` | `1.0` | 30 | 0 | 0 | 27 | 3 |
| `PhysicsToolsets.PhysicsAssetToolset` | `PhysicsToolsets` | `1.0` | 17 | 0 | 0 | 14 | 3 |
| `PluginToolset.PluginToolset` | `PluginToolset` | `1.0` | 17 | 0 | 0 | 16 | 1 |
| `SemanticSearchToolset.SemanticSearchToolset` | `SemanticSearchToolset` | `1.0` | 2 | 0 | 0 | 2 | 0 |
| `SlateInspectorToolset.SlateInspectorToolset` | `SlateInspectorToolset` | `1.0` | 14 | 0 | 0 | 14 | 0 |
| `state_tree_toolset.toolsets.state_tree.StateTreeTools` | `StateTreeToolset` | `1.0` | 9 | 0 | 0 | 9 | 0 |
| `ToolsetRegistry.AgentSkillToolset` | `ToolsetRegistry` | `1.0` | 4 | 0 | 0 | 4 | 0 |
| `UMGToolSet.UMGToolSet` | `UMGToolSet` | `1.0` | 23 | 0 | 0 | 21 | 2 |
| `UnrealBridge` | `UnrealBridge` | `3.0.0` | 2 | 2 | 0 | 0 | 0 |
| `WorldConditionsToolset.WorldConditionTools` | `WorldConditionsToolset` | `1.0` | 2 | 0 | 0 | 2 | 0 |

## 工具级决策

### aimodule_toolset.toolsets.behavior_tree.BehaviorTreeTools

| Tool | 分类 | 副作用推断 | 风险 | Bridge | 保存 | 依据 |
|---|---|---|---|---|---|---|
| `get_blackboard` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_children` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_node_depth` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_node_depths` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_root_decorators` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_subtree` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `list_nodes` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'list', but the official schema has no read-only annotation; manual audit is still required. |

### animation_toolset.toolsets.conditions.SequencerConditionTools

| Tool | 分类 | 副作用推断 | 风险 | Bridge | 保存 | 依据 |
|---|---|---|---|---|---|---|
| `clear_section_condition` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `clear_track_condition` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `clear_track_row_condition` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `get_section_condition` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_track_condition` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_track_row_condition` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `set_section_condition` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `set_track_condition` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `set_track_row_condition` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |

### animation_toolset.toolsets.controlrig.ControlRigTools

| Tool | 分类 | 副作用推断 | 风险 | Bridge | 保存 | 依据 |
|---|---|---|---|---|---|---|
| `add_backward_solve_graph` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `add_bone` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `add_control` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `add_element` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `add_event_graph` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `add_event_node` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `add_graph` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `add_interaction_graph` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `add_null` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `add_variable` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `add_variable_node` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `change_variable_type` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `connect_pins` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `create` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `create_node` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `delete_node` | Reject | Destructive | Destructive | Rejected | ProviderDefined | Destructive annotation or destructive operation name. |
| `disconnect_pins` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `get_all_bones` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_all_controls` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_all_nulls` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_backward_solve_graph` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_children` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_connected_pins` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_elements` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_event_graph` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_forward_solve_graph` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_global_transform` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_graph` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_interaction_graph` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_local_transform` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_node_position` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_parent` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_pin_value` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_variable` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `import_bones_from_asset` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `list_graphs` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'list', but the official schema has no read-only annotation; manual audit is still required. |
| `list_nodes` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'list', but the official schema has no read-only annotation; manual audit is still required. |
| `list_pins` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'list', but the official schema has no read-only annotation; manual audit is still required. |
| `list_variables` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'list', but the official schema has no read-only annotation; manual audit is still required. |
| `remove_variable` | Reject | Destructive | Destructive | Rejected | ProviderDefined | Destructive annotation or destructive operation name. |
| `set_global_transform` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `set_local_transform` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `set_node_position` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `set_pin_value` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |

### animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools

| Tool | 分类 | 副作用推断 | 风险 | Bridge | 保存 | 依据 |
|---|---|---|---|---|---|---|
| `add_layer_from_selection` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `bake_space` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `bake_to_control_rig` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `blend_values_on_selected` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `clear_selection` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `collapse_anim_layers` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `delete_anim_layer` | Reject | Destructive | Destructive | Rejected | ProviderDefined | Destructive annotation or destructive operation name. |
| `delete_space` | Reject | Destructive | Destructive | Rejected | ProviderDefined | Destructive annotation or destructive operation name. |
| `duplicate_anim_layer` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `export_fbx_from_rig` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `find_or_create_track` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'find', but the official schema has no read-only annotation; manual audit is still required. |
| `frame_selection` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `get_actor_transform_at_frame` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_anim_layers` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_anim_mode_gizmo_scale` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_anim_mode_hide_manips` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_anim_mode_hierarchy` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_anim_mode_local_spaces` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_anim_mode_nulls` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_anim_mode_only_rig_sel` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_bool` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_control_rigs` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_controls_info` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_controls_mask` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_euler_transform` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_float` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_int` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_position` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_priority_order` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_rotator` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_scale` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_selected_controls` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_transform` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_vector2d` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_world_transform` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `hide_all_controls` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `import_fbx_to_rig` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `is_fk_control_rig` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'is', but the official schema has no read-only annotation; manual audit is still required. |
| `is_layered_control_rig` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'is', but the official schema has no read-only annotation; manual audit is still required. |
| `key_controls` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `key_controls_at_frames` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `load_anim_into_rig` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `merge_anim_layers` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `mirror_selected_controls` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `move_space` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `reorder_anim_layers` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `select_control` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `select_mirrored_controls` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `set_anim_mode_gizmo_scale` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `set_anim_mode_hide_manips` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `set_anim_mode_hierarchy` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `set_anim_mode_local_spaces` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `set_anim_mode_nulls` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `set_anim_mode_only_rig_sel` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `set_bool` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `set_controls_mask` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `set_euler_transform` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `set_float` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `set_int` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `set_layered_mode` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `set_position` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `set_priority_order` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `set_rotator` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `set_scale` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `set_space` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `set_transform` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `set_vector2d` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `set_world_transform` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `show_all_controls` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `snap_control_rig` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `tween_control_rig` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `zero_transforms` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |

### animation_toolset.toolsets.custom_bindings.SequencerCustomBindingTools

| Tool | 分类 | 副作用推断 | 风险 | Bridge | 保存 | 依据 |
|---|---|---|---|---|---|---|
| `change_actor_template_class` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `convert_to_custom_binding` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `convert_to_possessable` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `convert_to_spawnable` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `get_custom_binding_objects` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_custom_binding_type` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_custom_bindings_of_type` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `save_default_spawnable_state` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |

### animation_toolset.toolsets.import_export.SequencerImportExportTools

| Tool | 分类 | 副作用推断 | 风险 | Bridge | 保存 | 依据 |
|---|---|---|---|---|---|---|
| `export_anim_sequence` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `export_fbx` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `get_linked_anim_sequences` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_linked_level_sequence` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `import_fbx` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `link_anim_sequence` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |

### animation_toolset.toolsets.keyframing.SequencerKeyframingTools

| Tool | 分类 | 副作用推断 | 风险 | Bridge | 保存 | 依据 |
|---|---|---|---|---|---|---|
| `add_key_bool` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `add_key_float` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `add_key_integer` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `add_key_string` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `bake_channel_keys` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `close_curve_editor` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `curve_editor_empty_selection` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `curve_editor_select_keys` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `get_channel_names` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_curve_editor_selected_keys` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_default_value` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_keys` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_keys_by_index` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_selected_channels` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_selected_key_channels` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `is_curve_editor_open` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'is', but the official schema has no read-only annotation; manual audit is still required. |
| `is_curve_shown` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'is', but the official schema has no read-only annotation; manual audit is still required. |
| `open_curve_editor` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `remove_key_at_frame` | Reject | Destructive | Destructive | Rejected | ProviderDefined | Destructive annotation or destructive operation name. |
| `select_channels` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `set_default_value` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `show_curve` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |

### animation_toolset.toolsets.outliner.SequencerOutlinerTools

| Tool | 分类 | 副作用推断 | 风险 | Bridge | 保存 | 依据 |
|---|---|---|---|---|---|---|
| `get_deactivated_nodes` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_locked_nodes` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_muted_nodes` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_node_label` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_outliner_children` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_outliner_selection` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_outliner_tree` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_pinned_nodes` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_sections_for_nodes` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_soloed_nodes` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `is_node_expanded` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'is', but the official schema has no read-only annotation; manual audit is still required. |
| `set_node_deactivated` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `set_node_expanded` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `set_node_locked` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `set_node_muted` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `set_node_pinned` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `set_node_solo` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `set_outliner_selection` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |

### animation_toolset.toolsets.sequencer.SequencerTools

| Tool | 分类 | 副作用推断 | 风险 | Bridge | 保存 | 依据 |
|---|---|---|---|---|---|---|
| `add_actors` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `add_actors_by_name` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `add_actors_to_binding` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `add_binding_to_folder` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `add_event_repeater_section` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `add_event_trigger_section` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `add_marked_frame` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `add_root_folder` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `add_section` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `add_spawnable_from_class` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `add_spawnable_from_instance` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `add_track_to_binding` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `add_track_to_folder` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `add_track_to_sequence` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `bake_transform` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `close_sequence` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `copy_bindings` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `copy_folders` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `copy_sections` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `copy_tracks` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `create_camera` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `create_level_sequence` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `delete_all_marked_frames` | Reject | Destructive | Destructive | Rejected | ProviderDefined | Destructive annotation or destructive operation name. |
| `delete_marked_frame` | Reject | Destructive | Destructive | Rejected | ProviderDefined | Destructive annotation or destructive operation name. |
| `empty_selection` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `find_binding_by_name` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'find', but the official schema has no read-only annotation; manual audit is still required. |
| `find_binding_by_tag` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'find', but the official schema has no read-only annotation; manual audit is still required. |
| `find_bindings_by_tag` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'find', but the official schema has no read-only annotation; manual audit is still required. |
| `find_marked_frame_by_label` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'find', but the official schema has no read-only annotation; manual audit is still required. |
| `find_tracks_by_type` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'find', but the official schema has no read-only annotation; manual audit is still required. |
| `fix_actor_references` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `focus_parent_sequence` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `focus_sub_sequence` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `force_evaluate` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `get_all_binding_tags` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_binding_id` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_binding_name` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_binding_tags` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_bindings` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_bound_objects` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_child_possessables` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_clock_source` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_current_sequence` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_display_rate` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_evaluation_type` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_focused_sequence` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_folder_contents` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_loop_mode` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_marked_frames` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_playback_range` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_playback_speed` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_playhead_frame` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_root_folders` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_section_blend_type` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_section_completion_mode` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_section_ease_in` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_section_ease_out` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_section_post_roll_frames` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_section_pre_roll_frames` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_section_properties` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_section_range` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_section_to_key` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_sections` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_selected_bindings` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_selected_folders` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_selected_sections` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_selected_tracks` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_selection_range` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_sequence_lock_state` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_sub_sequence_hierarchy` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_tick_resolution` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_track_display_name` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_track_filter_names` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_tracks_on_binding` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_tracks_on_sequence` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_view_range` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_work_range` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `has_section_end_frame` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'has', but the official schema has no read-only annotation; manual audit is still required. |
| `has_section_start_frame` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'has', but the official schema has no read-only annotation; manual audit is still required. |
| `is_camera_cut_locked` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'is', but the official schema has no read-only annotation; manual audit is still required. |
| `is_playback_range_locked` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'is', but the official schema has no read-only annotation; manual audit is still required. |
| `is_playing` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'is', but the official schema has no read-only annotation; manual audit is still required. |
| `is_sequence_locked` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'is', but the official schema has no read-only annotation; manual audit is still required. |
| `is_track_filter_active` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'is', but the official schema has no read-only annotation; manual audit is still required. |
| `open_sequence` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `paste_bindings` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `paste_folders` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `paste_sections` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `paste_tracks` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `pause` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `play` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `play_to` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `rebind_component` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `refresh_sequence` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `remove_actors_from_binding` | Reject | Destructive | Destructive | Rejected | ProviderDefined | Destructive annotation or destructive operation name. |
| `remove_all_bindings` | Reject | Destructive | Destructive | Rejected | ProviderDefined | Destructive annotation or destructive operation name. |
| `remove_binding` | Reject | Destructive | Destructive | Rejected | ProviderDefined | Destructive annotation or destructive operation name. |
| `remove_binding_tag` | Reject | Destructive | Destructive | Rejected | ProviderDefined | Destructive annotation or destructive operation name. |
| `remove_invalid_bindings` | Reject | Destructive | Destructive | Rejected | ProviderDefined | Destructive annotation or destructive operation name. |
| `remove_root_folder` | Reject | Destructive | Destructive | Rejected | ProviderDefined | Destructive annotation or destructive operation name. |
| `remove_section` | Reject | Destructive | Destructive | Rejected | ProviderDefined | Destructive annotation or destructive operation name. |
| `remove_track` | Reject | Destructive | Destructive | Rejected | ProviderDefined | Destructive annotation or destructive operation name. |
| `remove_track_from_sequence` | Reject | Destructive | Destructive | Rejected | ProviderDefined | Destructive annotation or destructive operation name. |
| `replace_binding_with_actors` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `select_bindings` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `select_folders` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `select_sections` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `select_tracks` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `set_binding_name` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `set_byte_track_enum` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `set_camera_cut_binding` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `set_camera_lock` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `set_clock_source` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `set_display_rate` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `set_evaluation_type` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `set_loop_mode` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `set_playback_range` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `set_playback_range_locked` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `set_playback_speed` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `set_playhead_frame` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `set_property_name_and_path` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `set_section_animation` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `set_section_blend_type` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `set_section_completion_mode` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `set_section_ease_in` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `set_section_ease_out` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `set_section_end_bounded` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `set_section_post_roll_frames` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `set_section_pre_roll_frames` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `set_section_range` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `set_section_start_bounded` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `set_selection_range` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `set_sequence_locked` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `set_tick_resolution` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `set_track_display_name` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `set_track_filter_active` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `set_view_range` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `set_work_range` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `tag_binding` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `untag_binding` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |

### AutomationTestToolset.AutomationTestToolset

| Tool | 分类 | 副作用推断 | 风险 | Bridge | 保存 | 依据 |
|---|---|---|---|---|---|---|
| `DiscoverTests` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `GetTestResults` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `GetTestStatus` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `ListTests` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'list', but the official schema has no read-only annotation; manual audit is still required. |
| `RunTests` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `RunTestsByFilter` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `StopTests` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |

### ConfigSettingsToolset.ConfigSettingsToolset

| Tool | 分类 | 副作用推断 | 风险 | Bridge | 保存 | 依据 |
|---|---|---|---|---|---|---|
| `GetSectionPropertyValues` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `GetSectionSchema` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `ListCategories` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'list', but the official schema has no read-only annotation; manual audit is still required. |
| `ListContainers` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'list', but the official schema has no read-only annotation; manual audit is still required. |
| `ListSections` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'list', but the official schema has no read-only annotation; manual audit is still required. |
| `ResetSectionToDefaults` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `SaveSection` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `SetSectionProperties` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |

### conversation_toolset.toolsets.conversation.ConversationTools

| Tool | 分类 | 副作用推断 | 风险 | Bridge | 保存 | 依据 |
|---|---|---|---|---|---|---|
| `get_all_nodes` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_node_by_guid` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_node_connections` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_node_guids` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_sub_nodes` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `list_entry_points` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'list', but the official schema has no read-only annotation; manual audit is still required. |
| `list_speakers` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'list', but the official schema has no read-only annotation; manual audit is still required. |

### DataflowAgent.DataflowAgentToolset

| Tool | 分类 | 副作用推断 | 风险 | Bridge | 保存 | 依据 |
|---|---|---|---|---|---|---|
| `AddCommentBox` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `AddNode` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `AddVariable` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `AssignDataflowTemplate` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `ConnectNodePins` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `CreateDataflowCompatibleAsset` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `CreateDataflowCompatibleAssetFromTemplate` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `CreateGraph` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `DisconnectNodePins` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `GetGraphStructure` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `GetNodeInfo` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `GetNodeTypeSchema` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `ListDataflowCompatibleAssetTypes` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'list', but the official schema has no read-only annotation; manual audit is still required. |
| `ListDataflowTemplatesForAssetClass` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'list', but the official schema has no read-only annotation; manual audit is still required. |
| `ListNodeTypes` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'list', but the official schema has no read-only annotation; manual audit is still required. |
| `ListVariables` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'list', but the official schema has no read-only annotation; manual audit is still required. |
| `RemoveCommentBox` | Reject | Destructive | Destructive | Rejected | ProviderDefined | Destructive annotation or destructive operation name. |
| `RemoveNode` | Reject | Destructive | Destructive | Rejected | ProviderDefined | Destructive annotation or destructive operation name. |
| `RemoveVariable` | Reject | Destructive | Destructive | Rejected | ProviderDefined | Destructive annotation or destructive operation name. |
| `RepositionNode` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `SetVariable` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `UpdateNode` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |

### DataRegistryToolset.DataRegistryTools

| Tool | 分类 | 副作用推断 | 风险 | Bridge | 保存 | 依据 |
|---|---|---|---|---|---|---|
| `GetItems` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `GetRegistryInfo` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `GetSchema` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `ListDataSources` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'list', but the official schema has no read-only annotation; manual audit is still required. |
| `ListItems` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'list', but the official schema has no read-only annotation; manual audit is still required. |
| `ListRegistries` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'list', but the official schema has no read-only annotation; manual audit is still required. |
| `ListRuntimeSources` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'list', but the official schema has no read-only annotation; manual audit is still required. |

### editor_toolset.toolsets.actor.ActorTools

| Tool | 分类 | 副作用推断 | 风险 | Bridge | 保存 | 依据 |
|---|---|---|---|---|---|---|
| `add_component` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `add_tag` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `get_actor_bounds` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_actor_transform` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_component_actor` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_components` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_label` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_parent_component` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_root_component` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_tags` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `has_tag` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'has', but the official schema has no read-only annotation; manual audit is still required. |
| `look_at` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `remove_component` | Reject | Destructive | Destructive | Rejected | ProviderDefined | Destructive annotation or destructive operation name. |
| `remove_tag` | Reject | Destructive | Destructive | Rejected | ProviderDefined | Destructive annotation or destructive operation name. |
| `set_actor_transform` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `set_label` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `set_parent_component` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |

### editor_toolset.toolsets.asset.AssetTools

| Tool | 分类 | 副作用推断 | 风险 | Bridge | 保存 | 依据 |
|---|---|---|---|---|---|---|
| `can_edit_asset` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'can', but the official schema has no read-only annotation; manual audit is still required. |
| `create_folder` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `delete` | Reject | Destructive | Destructive | Rejected | ProviderDefined | Destructive annotation or destructive operation name. |
| `duplicate` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `exists` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `find_assets` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'find', but the official schema has no read-only annotation; manual audit is still required. |
| `get_asset_class` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_asset_tags` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_dependencies` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_metadata_tags` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_plugin_content_paths` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_referencers` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `is_checked_out` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'is', but the official schema has no read-only annotation; manual audit is still required. |
| `is_dirty` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'is', but the official schema has no read-only annotation; manual audit is still required. |
| `list_folders` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'list', but the official schema has no read-only annotation; manual audit is still required. |
| `load_asset` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `move` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `read_file` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'read', but the official schema has no read-only annotation; manual audit is still required. |
| `save_assets` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `update_metadata_tags` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `write_file` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |

### editor_toolset.toolsets.blueprint.BlueprintTools

| Tool | 分类 | 副作用推断 | 风险 | Bridge | 保存 | 依据 |
|---|---|---|---|---|---|---|
| `add_component_bound_event` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `add_event` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `add_event_dispatcher` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `add_function_graph` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `add_function_param` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `add_node_pin` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `add_object_function_param` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `add_object_variable` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `add_struct_function_param` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `add_struct_variable` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `add_variable` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `arrange_nodes` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `break_pins` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `compile_blueprint` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `connect_pins` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `create` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `create_node` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `delete_node` | Reject | Destructive | Destructive | Rejected | ProviderDefined | Destructive annotation or destructive operation name. |
| `find_node_categories` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'find', but the official schema has no read-only annotation; manual audit is still required. |
| `find_node_types` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'find', but the official schema has no read-only annotation; manual audit is still required. |
| `find_nodes` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'find', but the official schema has no read-only annotation; manual audit is still required. |
| `get_connected_subgraph` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_create_event_function` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_default_object` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_graph` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_graph_dsl_docs` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_node_infos` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_node_type_pins` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_parent` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_pin_value` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_variable_category` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_variable_replication` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `list_compatible_event_functions` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'list', but the official schema has no read-only annotation; manual audit is still required. |
| `list_component_events` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'list', but the official schema has no read-only annotation; manual audit is still required. |
| `list_event_dispatchers` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'list', but the official schema has no read-only annotation; manual audit is still required. |
| `list_events` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'list', but the official schema has no read-only annotation; manual audit is still required. |
| `list_functions` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'list', but the official schema has no read-only annotation; manual audit is still required. |
| `list_graphs` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'list', but the official schema has no read-only annotation; manual audit is still required. |
| `list_variables` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'list', but the official schema has no read-only annotation; manual audit is still required. |
| `read_graph_dsl` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'read', but the official schema has no read-only annotation; manual audit is still required. |
| `remove_function_graph` | Reject | Destructive | Destructive | Rejected | ProviderDefined | Destructive annotation or destructive operation name. |
| `remove_function_param` | Reject | Destructive | Destructive | Rejected | ProviderDefined | Destructive annotation or destructive operation name. |
| `remove_node_pin` | Reject | Destructive | Destructive | Rejected | ProviderDefined | Destructive annotation or destructive operation name. |
| `remove_variable` | Reject | Destructive | Destructive | Rejected | ProviderDefined | Destructive annotation or destructive operation name. |
| `retarget_node_class` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `set_create_event_function` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `set_node_position` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `set_parent` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `set_pin_value` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `set_variable_category` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `set_variable_instance_editable` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `set_variable_replication` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `write_graph_dsl` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |

### editor_toolset.toolsets.curve_table.CurveTableTools

| Tool | 分类 | 副作用推断 | 风险 | Bridge | 保存 | 依据 |
|---|---|---|---|---|---|---|
| `add_key` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `add_row` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `create` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `get_keys` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `import_file` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `list_rows` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'list', but the official schema has no read-only annotation; manual audit is still required. |
| `remove_row` | Reject | Destructive | Destructive | Rejected | ProviderDefined | Destructive annotation or destructive operation name. |
| `rename_row` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `set_keys` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |

### editor_toolset.toolsets.data_asset.DataAssetTools

| Tool | 分类 | 副作用推断 | 风险 | Bridge | 保存 | 依据 |
|---|---|---|---|---|---|---|
| `create` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |

### editor_toolset.toolsets.data_table.DataTableTools

| Tool | 分类 | 副作用推断 | 风险 | Bridge | 保存 | 依据 |
|---|---|---|---|---|---|---|
| `add_rows` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `create` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `get_rows` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_schema` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `import_file` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `list_rows` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'list', but the official schema has no read-only annotation; manual audit is still required. |
| `remove_rows` | Reject | Destructive | Destructive | Rejected | ProviderDefined | Destructive annotation or destructive operation name. |
| `rename_rows` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `search_row_structs` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'search', but the official schema has no read-only annotation; manual audit is still required. |
| `set_rows` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |

### editor_toolset.toolsets.material.MaterialTools

| Tool | 分类 | 副作用推断 | 风险 | Bridge | 保存 | 依据 |
|---|---|---|---|---|---|---|
| `add_expression` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `connect_expressions` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `connect_to_output` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `create_function` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `create_material` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `create_parameter_collection` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `delete_expression` | Reject | Destructive | Destructive | Rejected | ProviderDefined | Destructive annotation or destructive operation name. |
| `delete_parameter_group` | Reject | Destructive | Destructive | Rejected | ProviderDefined | Destructive annotation or destructive operation name. |
| `delete_unused_expressions` | Reject | Destructive | Destructive | Rejected | ProviderDefined | Destructive annotation or destructive operation name. |
| `disconnect_expressions` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `disconnect_from_output` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `get_expression_input_names` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_expression_inputs` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_expression_output_names` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_expressions` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_property_input` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_referencing_materials` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `layout_expressions` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `list_expression_classes` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'list', but the official schema has no read-only annotation; manual audit is still required. |
| `list_parameter_groups` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'list', but the official schema has no read-only annotation; manual audit is still required. |
| `recompile` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `rename_parameter_group` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |

### editor_toolset.toolsets.material_instance.MaterialInstanceTools

| Tool | 分类 | 副作用推断 | 风险 | Bridge | 保存 | 依据 |
|---|---|---|---|---|---|---|
| `clear_parameters` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `create` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `get_scalar_parameter` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_static_switch_parameter` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_texture_parameter` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_vector_parameter` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `list_parameters` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'list', but the official schema has no read-only annotation; manual audit is still required. |
| `set_parameter_override` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `set_parent` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `set_scalar_parameter` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `set_static_switch_parameter` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `set_texture_parameter` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `set_vector_parameter` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |

### editor_toolset.toolsets.object.ObjectTools

| Tool | 分类 | 副作用推断 | 风险 | Bridge | 保存 | 依据 |
|---|---|---|---|---|---|---|
| `get_class` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_properties` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `list_properties` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'list', but the official schema has no read-only annotation; manual audit is still required. |
| `reset_properties` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `search_subclasses` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'search', but the official schema has no read-only annotation; manual audit is still required. |
| `set_properties` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |

### editor_toolset.toolsets.primitive.PrimitiveTools

| Tool | 分类 | 副作用推断 | 风险 | Bridge | 保存 | 依据 |
|---|---|---|---|---|---|---|
| `add_cone` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `add_cube` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `add_cylinder` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `add_sphere` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |

### editor_toolset.toolsets.programmatic.ProgrammaticToolset

| Tool | 分类 | 副作用推断 | 风险 | Bridge | 保存 | 依据 |
|---|---|---|---|---|---|---|
| `execute_tool_script` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `get_execution_environment` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |

### editor_toolset.toolsets.scene.SceneTools

| Tool | 分类 | 副作用推断 | 风险 | Bridge | 保存 | 依据 |
|---|---|---|---|---|---|---|
| `add_to_scene_from_asset` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `add_to_scene_from_class` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `can_edit` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'can', but the official schema has no read-only annotation; manual audit is still required. |
| `commit_level_instance` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `create_level_instance` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `delete_folder` | Reject | Destructive | Destructive | Rejected | ProviderDefined | Destructive annotation or destructive operation name. |
| `edit_level_instance` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `find_actors` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'find', but the official schema has no read-only annotation; manual audit is still required. |
| `get_actors_in_folder` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_collision_channels` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_current_level` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_folders` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `is_checked_out` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'is', but the official schema has no read-only annotation; manual audit is still required. |
| `load_level` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `merge_actors` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `remove_from_scene` | Reject | Destructive | Destructive | Rejected | ProviderDefined | Destructive annotation or destructive operation name. |
| `rename_folder` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `save_actor` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `set_actor_folder` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `trace_world` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |

### editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools

| Tool | 分类 | 副作用推断 | 风险 | Bridge | 保存 | 依据 |
|---|---|---|---|---|---|---|
| `add_socket` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `assign_physics_asset` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `get_bone_children` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_bone_names` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_bone_parent` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_bounds` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_lod_count` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_material` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_material_slots` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_morph_target_names` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_physics_asset` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_section_count` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_skeleton` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_socket_bone` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_socket_names` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_socket_transform` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_vertex_count` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `import_file` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `remove_socket` | Reject | Destructive | Destructive | Rejected | ProviderDefined | Destructive annotation or destructive operation name. |
| `rename_socket` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `set_material` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `set_socket_transform` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |

### editor_toolset.toolsets.static_mesh.StaticMeshTools

| Tool | 分类 | 副作用推断 | 风险 | Bridge | 保存 | 依据 |
|---|---|---|---|---|---|---|
| `generate_convex_collisions` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `generate_lods` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `get_bounds` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_lod_count` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_lod_thresholds` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_material` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_material_slots` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_triangle_count` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_vertex_count` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `import_file` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `is_nanite_enabled` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'is', but the official schema has no read-only annotation; manual audit is still required. |
| `remove_collisions` | Reject | Destructive | Destructive | Rejected | ProviderDefined | Destructive annotation or destructive operation name. |
| `remove_lods` | Reject | Destructive | Destructive | Rejected | ProviderDefined | Destructive annotation or destructive operation name. |
| `set_lod_thresholds` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `set_material` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `set_nanite_enabled` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |

### editor_toolset.toolsets.string_table.StringTableTools

| Tool | 分类 | 副作用推断 | 风险 | Bridge | 保存 | 依据 |
|---|---|---|---|---|---|---|
| `create` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `get_entry` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_namespace` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_table_id` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `import_file` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `list_keys` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'list', but the official schema has no read-only annotation; manual audit is still required. |
| `remove_entry` | Reject | Destructive | Destructive | Rejected | ProviderDefined | Destructive annotation or destructive operation name. |
| `set_entry` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |

### editor_toolset.toolsets.texture.TextureTools

| Tool | 分类 | 副作用推断 | 风险 | Bridge | 保存 | 依据 |
|---|---|---|---|---|---|---|
| `get_size` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `import_file` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |

### EditorToolset.EditorAppToolset

| Tool | 分类 | 副作用推断 | 风险 | Bridge | 保存 | 依据 |
|---|---|---|---|---|---|---|
| `CaptureAssetImage` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `CaptureEditorImage` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `CaptureViewport` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `FocusOnActors` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `GetCameraTransform` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `GetContentBrowserPath` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `GetOpenAssets` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `GetSelectedActors` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `GetSelectedAssets` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `GetVisibleActors` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `IsPIERunning` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'is', but the official schema has no read-only annotation; manual audit is still required. |
| `OpenEditorForAsset` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `ScreenCoordsToWorld` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `SearchCVars` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'search', but the official schema has no read-only annotation; manual audit is still required. |
| `SelectActors` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `SelectAssets` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `SetCameraTransform` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `SetContentBrowserPath` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `StartPIE` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `StopPIE` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `WorldPosToScreenCoords` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |

### EditorToolset.LogsToolset

| Tool | 分类 | 副作用推断 | 风险 | Bridge | 保存 | 依据 |
|---|---|---|---|---|---|---|
| `GetLogCategories` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `GetLogEntries` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `GetVerbosity` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `SetVerbosity` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |

### GameFeaturesToolset.GameFeaturesToolset

| Tool | 分类 | 副作用推断 | 风险 | Bridge | 保存 | 依据 |
|---|---|---|---|---|---|---|
| `GetGameFeatureState` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `IsGameFeatureActive` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'is', but the official schema has no read-only annotation; manual audit is still required. |
| `IsGameFeaturePlugin` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'is', but the official schema has no read-only annotation; manual audit is still required. |
| `ListDiscoveredGameFeaturePlugins` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'list', but the official schema has no read-only annotation; manual audit is still required. |
| `ListEnabledGameFeaturePlugins` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'list', but the official schema has no read-only annotation; manual audit is still required. |
| `RequestActivateGameFeature` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `RequestDeactivateGameFeature` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |

### GameplayTagsToolset.GameplayTagsToolset

| Tool | 分类 | 副作用推断 | 风险 | Bridge | 保存 | 依据 |
|---|---|---|---|---|---|---|
| `AddTag` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `FindReferencersByTag` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'find', but the official schema has no read-only annotation; manual audit is still required. |
| `GetTagInfo` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `ListTags` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'list', but the official schema has no read-only annotation; manual audit is still required. |
| `RemoveTag` | Reject | Destructive | Destructive | Rejected | ProviderDefined | Destructive annotation or destructive operation name. |
| `RenameTag` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |

### GASToolsets.AbilitySystemInspectorToolset

| Tool | 分类 | 副作用推断 | 风险 | Bridge | 保存 | 依据 |
|---|---|---|---|---|---|---|
| `GetActiveEffects` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `GetActiveTags` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `GetAttributeValues` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `GetGrantedAbilities` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |

### GASToolsets.AttributeSetToolset

| Tool | 分类 | 副作用推断 | 风险 | Bridge | 保存 | 依据 |
|---|---|---|---|---|---|---|
| `FindAttributeSetClasses` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'find', but the official schema has no read-only annotation; manual audit is still required. |
| `ListAttributes` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'list', but the official schema has no read-only annotation; manual audit is still required. |

### GASToolsets.GameplayCueToolset

| Tool | 分类 | 副作用推断 | 风险 | Bridge | 保存 | 依据 |
|---|---|---|---|---|---|---|
| `AddCueTag` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `CreateCueNotifyAsset` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `ExecuteCueOnSelectedActor` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `FindCueNotifyAssets` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'find', but the official schema has no read-only annotation; manual audit is still required. |
| `FindCueTagsWithoutNotifies` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'find', but the official schema has no read-only annotation; manual audit is still required. |
| `GetCueInfo` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `ListCues` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'list', but the official schema has no read-only annotation; manual audit is still required. |
| `RemoveCueTag` | Reject | Destructive | Destructive | Rejected | ProviderDefined | Destructive annotation or destructive operation name. |

### NiagaraToolsets.NiagaraToolset_Assets

| Tool | 分类 | 副作用推断 | 风险 | Bridge | 保存 | 依据 |
|---|---|---|---|---|---|---|
| `FindNiagaraScripts` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'find', but the official schema has no read-only annotation; manual audit is still required. |
| `GetAssetDiscoveryInfo` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `GetNiagaraScriptDigest` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |

### NiagaraToolsets.NiagaraToolset_Blueprint

| Tool | 分类 | 副作用推断 | 风险 | Bridge | 保存 | 依据 |
|---|---|---|---|---|---|---|
| `ConstructNiagaraBPWrapperFromComponent` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `ConstructNiagaraBPWrapperFromSystem` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |

### NiagaraToolsets.NiagaraToolset_Component

| Tool | 分类 | 副作用推断 | 风险 | Bridge | 保存 | 依据 |
|---|---|---|---|---|---|---|
| `GetUserVariables` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `GetVariable` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `SetSystem` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `SetVariable` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |

### NiagaraToolsets.NiagaraToolset_Info

| Tool | 分类 | 副作用推断 | 风险 | Bridge | 保存 | 依据 |
|---|---|---|---|---|---|---|
| `UEnum_Info` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |

### NiagaraToolsets.NiagaraToolset_System

| Tool | 分类 | 副作用推断 | 风险 | Bridge | 保存 | 依据 |
|---|---|---|---|---|---|---|
| `AddEmitter` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `AddModule` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `AddRenderer` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `AddSetParameterEntry` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `AddSetParametersModule` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `AddUserVariables` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `ApplyStackIssueFix` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `CreateNiagaraSystem` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `GetAvailableDynamicInputs` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `GetDataInterfaceSchema` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `GetDynamicInputChain` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `GetDynamicInputSchema` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `GetDynamicInputSchemaFromAsset` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `GetEmitterData` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `GetEmitterInputValues` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `GetEmitterSchema` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `GetEmitterSummary` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `GetEmitterTopology` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `GetModuleInputValues` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `GetModuleSchema` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `GetModuleSchemaFromAsset` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `GetModuleTopology` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `GetRendererData` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `GetRendererSchema` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `GetScriptStackInputValues` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `GetScriptStackTopology` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `GetStackInputData` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `GetStackInputSchema` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `GetStackInputTopology` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `GetStackIssues` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `GetSystemCompileState` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `GetSystemData` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `GetSystemDependencies` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `GetSystemSchema` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `GetSystemSummary` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `GetUserVariables` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `RemoveEmitter` | Reject | Destructive | Destructive | Rejected | ProviderDefined | Destructive annotation or destructive operation name. |
| `RemoveModule` | Reject | Destructive | Destructive | Rejected | ProviderDefined | Destructive annotation or destructive operation name. |
| `RemoveRenderer` | Reject | Destructive | Destructive | Rejected | ProviderDefined | Destructive annotation or destructive operation name. |
| `RemoveSetParameterEntry` | Reject | Destructive | Destructive | Rejected | ProviderDefined | Destructive annotation or destructive operation name. |
| `RemoveUserVariables` | Reject | Destructive | Destructive | Rejected | ProviderDefined | Destructive annotation or destructive operation name. |
| `SetEmitterData` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `SetModuleEnabled` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `SetRendererData` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `SetStackInputData` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `SetSystemData` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |

### PCGToolset.PCGSpatialToolset

| Tool | 分类 | 副作用推断 | 风险 | Bridge | 保存 | 依据 |
|---|---|---|---|---|---|---|
| `RunPCGInstantGraph` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |

### PCGToolset.PCGToolset

| Tool | 分类 | 副作用推断 | 风险 | Bridge | 保存 | 依据 |
|---|---|---|---|---|---|---|
| `AddCommentBox` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `AddNode` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `AddSubgraphNode` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `ConnectNodePins` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `CreateGraph` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `DisconnectNodePins` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `DrawSpline` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `ExecuteGraphInstance` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `GetGraphDescription` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `GetGraphInstanceParams` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `GetGraphSchema` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `GetGraphStructure` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `GetNativeNodeSchema` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `GetNodeDataView` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `GetNodeInfo` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `ListAvailableSubgraphs` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'list', but the official schema has no read-only annotation; manual audit is still required. |
| `ListGraphInstances` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'list', but the official schema has no read-only annotation; manual audit is still required. |
| `ListNativeNodes` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'list', but the official schema has no read-only annotation; manual audit is still required. |
| `RemoveCommentBox` | Reject | Destructive | Destructive | Rejected | ProviderDefined | Destructive annotation or destructive operation name. |
| `RemoveGraphParams` | Reject | Destructive | Destructive | Rejected | ProviderDefined | Destructive annotation or destructive operation name. |
| `RemoveNode` | Reject | Destructive | Destructive | Rejected | ProviderDefined | Destructive annotation or destructive operation name. |
| `RepositionNode` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `ResetGraphInstanceParams` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `SetGraphDescription` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `SetGraphInstanceParams` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `SetGraphParams` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `SetNodeComment` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `SpawnGraphInstance` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `UpdateCommentBox` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `UpdateNode` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |

### PhysicsToolsets.PhysicsAssetToolset

| Tool | 分类 | 副作用推断 | 风险 | Bridge | 保存 | 依据 |
|---|---|---|---|---|---|---|
| `AddBody` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `AddConstraint` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `CreateFromMesh` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `GetBodyMassScale` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `GetBodyNames` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `GetBodyPhysicsMode` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `GetBodyShapes` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `GetConstraints` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `RemoveBody` | Reject | Destructive | Destructive | Rejected | ProviderDefined | Destructive annotation or destructive operation name. |
| `RemoveConstraint` | Reject | Destructive | Destructive | Rejected | ProviderDefined | Destructive annotation or destructive operation name. |
| `RemoveShape` | Reject | Destructive | Destructive | Rejected | ProviderDefined | Destructive annotation or destructive operation name. |
| `SetBodyMassScale` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `SetBodyPhysicsMode` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `SetBox` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `SetCapsule` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `SetConstraintLimits` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `SetSphere` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |

### PluginToolset.PluginToolset

| Tool | 分类 | 副作用推断 | 风险 | Bridge | 保存 | 依据 |
|---|---|---|---|---|---|---|
| `AddPluginDependency` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `CreatePlugin` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `GetPluginDependencies` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `GetPluginDependents` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `GetPluginDescriptor` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `GetPluginForAsset` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `GetPluginInfo` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `GetPluginTemplateDescriptions` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `IsEnabled` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'is', but the official schema has no read-only annotation; manual audit is still required. |
| `IsPluginCreationAllowed` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'is', but the official schema has no read-only annotation; manual audit is still required. |
| `IsPluginModificationAllowed` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'is', but the official schema has no read-only annotation; manual audit is still required. |
| `ListDiscoveredPlugins` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'list', but the official schema has no read-only annotation; manual audit is still required. |
| `ListEnabledPlugins` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'list', but the official schema has no read-only annotation; manual audit is still required. |
| `RemovePluginDependency` | Reject | Destructive | Destructive | Rejected | ProviderDefined | Destructive annotation or destructive operation name. |
| `SetPluginEnabled` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `UpdatePluginDescriptor` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `ValidateNewPluginNameAndLocation` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'validate', but the official schema has no read-only annotation; manual audit is still required. |

### SemanticSearchToolset.SemanticSearchToolset

| Tool | 分类 | 副作用推断 | 风险 | Bridge | 保存 | 依据 |
|---|---|---|---|---|---|---|
| `FindSimilar` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'find', but the official schema has no read-only annotation; manual audit is still required. |
| `Search` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'search', but the official schema has no read-only annotation; manual audit is still required. |

### SlateInspectorToolset.SlateInspectorToolset

| Tool | 分类 | 副作用推断 | 风险 | Bridge | 保存 | 依据 |
|---|---|---|---|---|---|---|
| `Click` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `Drag` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `FillForm` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `Hover` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `ListObservers` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'list', but the official schema has no read-only annotation; manual audit is still required. |
| `Observe` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `PressKey` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `Screenshot` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `SelectOption` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `Snapshot` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `Type` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `Unobserve` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `WaitFor` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `Windows` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |

### state_tree_toolset.toolsets.state_tree.StateTreeTools

| Tool | 分类 | 副作用推断 | 风险 | Bridge | 保存 | 依据 |
|---|---|---|---|---|---|---|
| `get_children` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_editor_data` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_enter_conditions` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_evaluators` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_global_tasks` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_node_description` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_root_states` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_tasks` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `get_transitions` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |

### ToolsetRegistry.AgentSkillToolset

| Tool | 分类 | 副作用推断 | 风险 | Bridge | 保存 | 依据 |
|---|---|---|---|---|---|---|
| `CreateSkill` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `GetSkills` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `ListSkills` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'list', but the official schema has no read-only annotation; manual audit is still required. |
| `UpdateSkill` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |

### UMGToolSet.UMGToolSet

| Tool | 分类 | 副作用推断 | 风险 | Bridge | 保存 | 依据 |
|---|---|---|---|---|---|---|
| `AddUIComponent` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `AddWidget` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `BindToEventProperty` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `CompileWidgetBlueprint` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `CreateWidgetBlueprint` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `GetNamedSlots` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `GetWidgetClassInfo` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `GetWidgetDescription` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `GetWidgets` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `GetWidgetTreeDepth` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `ListWidgetBlueprints` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'list', but the official schema has no read-only annotation; manual audit is still required. |
| `ListWidgetClasses` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'list', but the official schema has no read-only annotation; manual audit is still required. |
| `MoveUIComponent` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `MoveWidget` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `RemoveUIComponent` | Reject | Destructive | Destructive | Rejected | ProviderDefined | Destructive annotation or destructive operation name. |
| `RemoveWidget` | Reject | Destructive | Destructive | Rejected | ProviderDefined | Destructive annotation or destructive operation name. |
| `RenameWidget` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `ReplaceWidgetWithChild` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `ReplaceWidgetWithNamedSlot` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `ReplaceWidgetWithTemplate` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `SetNamedSlotContent` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `ToggleWidgetAsVariable` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |
| `WrapWidgets` | Extend | MutatingCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Official schema does not provide enough side-effect metadata. |

### UnrealBridge

| Tool | 分类 | 副作用推断 | 风险 | Bridge | 保存 | 依据 |
|---|---|---|---|---|---|---|
| `Capabilities` | Reuse | ReadOnlyDeclared | ReadOnly | Allowed | Never | UnrealBridge-owned read-only capability query. |
| `RegistryHash` | Reuse | ReadOnlyDeclared | ReadOnly | Allowed | Never | UnrealBridge-owned read-only registry hash query. |

### WorldConditionsToolset.WorldConditionTools

| Tool | 分类 | 副作用推断 | 风险 | Bridge | 保存 | 依据 |
|---|---|---|---|---|---|---|
| `GetConditionDescription` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
| `GetQueryDescription` | Extend | ReadOnlyCandidate | Unknown | BlockedPendingAudit | ProviderDefined | Name begins with 'get', but the official schema has no read-only annotation; manual audit is still required. |
