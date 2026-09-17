# UE 5.8.1 ToolsetRegistry 能力审计

> 生成时间：`2026-08-26T10:17:23+00:00`
> 引擎：`5.8.1-0+UE5`
> Provider：`EpicToolsetRegistry`
> Toolset：`53`；Tool：`832`

3.0 执行策略：每个允许项都绑定精确 `toolset|tool` 和结构化 input schema hash；未出现在策略中的工具默认拒绝。只读查询、运行态交互和非破坏性资产修改分别走 `ReadOnly`、`RuntimeInteraction`、`TransactionalSync` 执行面；任意文件/路径、Source Control、显式保存和破坏性操作保持 `Rejected`。

## 汇总

| ReadOnly | RuntimeInteraction | TransactionalSync | Rejected |
|---:|---:|---:|---:|
| 387 | 49 | 326 | 70 |

允许执行不等于允许保存：三个可执行面都固定 `save_behavior=Never`；`TransactionalSync` 还要求显式目标、立即完成和 ChangeSet 回滚能力，`RuntimeInteraction` 要求调用者显式 opt-in。

## Toolset 明细

| Toolset | Module | Version | Tools | ReadOnly | Runtime | Transactional | Rejected |
|---|---|---|---:|---:|---:|---:|---:|
| `aimodule_toolset.toolsets.behavior_tree.BehaviorTreeTools` | `AIModuleToolset` | `1.0` | 7 | 7 | 0 | 0 | 0 |
| `animation_toolset.toolsets.conditions.SequencerConditionTools` | `AnimationAssistantToolset` | `1.0` | 9 | 3 | 0 | 6 | 0 |
| `animation_toolset.toolsets.controlrig.ControlRigTools` | `AnimationAssistantToolset` | `1.0` | 44 | 21 | 0 | 20 | 3 |
| `animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools` | `AnimationAssistantToolset` | `1.0` | 72 | 26 | 5 | 37 | 4 |
| `animation_toolset.toolsets.custom_bindings.SequencerCustomBindingTools` | `AnimationAssistantToolset` | `1.0` | 8 | 3 | 0 | 5 | 0 |
| `animation_toolset.toolsets.import_export.SequencerImportExportTools` | `AnimationAssistantToolset` | `1.0` | 6 | 2 | 0 | 2 | 2 |
| `animation_toolset.toolsets.keyframing.SequencerKeyframingTools` | `AnimationAssistantToolset` | `1.0` | 22 | 9 | 5 | 7 | 1 |
| `animation_toolset.toolsets.outliner.SequencerOutlinerTools` | `AnimationAssistantToolset` | `1.0` | 18 | 11 | 1 | 6 | 0 |
| `animation_toolset.toolsets.sequencer.SequencerTools` | `AnimationAssistantToolset` | `1.0` | 140 | 55 | 14 | 60 | 11 |
| `AutomationTestToolset.AutomationTestToolset` | `AutomationTestToolset` | `1.0` | 7 | 3 | 4 | 0 | 0 |
| `ConfigSettingsToolset.ConfigSettingsToolset` | `ConfigSettingsToolset` | `1.0` | 8 | 5 | 0 | 2 | 1 |
| `conversation_toolset.toolsets.conversation.ConversationTools` | `ConversationToolset` | `1.0` | 7 | 7 | 0 | 0 | 0 |
| `DataflowAgent.DataflowAgentToolset` | `DataflowAgent` | `1.0` | 22 | 7 | 0 | 12 | 3 |
| `DataRegistryToolset.DataRegistryTools` | `DataRegistryToolset` | `1.0` | 7 | 7 | 0 | 0 | 0 |
| `editor_toolset.toolsets.actor.ActorTools` | `EditorToolset` | `1.0` | 17 | 9 | 0 | 6 | 2 |
| `editor_toolset.toolsets.asset.AssetTools` | `EditorToolset` | `1.0` | 21 | 14 | 0 | 5 | 2 |
| `editor_toolset.toolsets.blueprint.BlueprintTools` | `EditorToolset` | `1.0` | 53 | 22 | 0 | 26 | 5 |
| `editor_toolset.toolsets.curve_table.CurveTableTools` | `EditorToolset` | `1.0` | 9 | 2 | 0 | 5 | 2 |
| `editor_toolset.toolsets.data_asset.DataAssetTools` | `EditorToolset` | `1.0` | 1 | 0 | 0 | 1 | 0 |
| `editor_toolset.toolsets.data_table.DataTableTools` | `EditorToolset` | `1.0` | 10 | 4 | 0 | 4 | 2 |
| `editor_toolset.toolsets.material.MaterialTools` | `EditorToolset` | `1.0` | 22 | 8 | 0 | 11 | 3 |
| `editor_toolset.toolsets.material_instance.MaterialInstanceTools` | `EditorToolset` | `1.0` | 13 | 5 | 0 | 8 | 0 |
| `editor_toolset.toolsets.object.ObjectTools` | `EditorToolset` | `1.0` | 6 | 4 | 0 | 2 | 0 |
| `editor_toolset.toolsets.primitive.PrimitiveTools` | `EditorToolset` | `1.0` | 4 | 0 | 0 | 4 | 0 |
| `editor_toolset.toolsets.programmatic.ProgrammaticToolset` | `EditorToolset` | `1.0` | 2 | 1 | 0 | 0 | 1 |
| `editor_toolset.toolsets.scene.SceneTools` | `EditorToolset` | `1.0` | 20 | 7 | 1 | 8 | 4 |
| `editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools` | `EditorToolset` | `1.0` | 22 | 15 | 0 | 5 | 2 |
| `editor_toolset.toolsets.static_mesh.StaticMeshTools` | `EditorToolset` | `1.0` | 16 | 8 | 0 | 5 | 3 |
| `editor_toolset.toolsets.string_table.StringTableTools` | `EditorToolset` | `1.0` | 8 | 4 | 0 | 2 | 2 |
| `editor_toolset.toolsets.texture.TextureTools` | `EditorToolset` | `1.0` | 2 | 1 | 0 | 0 | 1 |
| `EditorToolset.EditorAppToolset` | `EditorToolset` | `1.0` | 21 | 8 | 2 | 11 | 0 |
| `EditorToolset.LogsToolset` | `EditorToolset` | `1.0` | 4 | 3 | 0 | 1 | 0 |
| `GameFeaturesToolset.GameFeaturesToolset` | `GameFeaturesToolset` | `1.0` | 7 | 5 | 2 | 0 | 0 |
| `GameplayTagsToolset.GameplayTagsToolset` | `GameplayTagsToolset` | `1.0` | 6 | 3 | 0 | 2 | 1 |
| `GASToolsets.AbilitySystemInspectorToolset` | `GASToolsets` | `1.0` | 4 | 4 | 0 | 0 | 0 |
| `GASToolsets.AttributeSetToolset` | `GASToolsets` | `1.0` | 2 | 2 | 0 | 0 | 0 |
| `GASToolsets.GameplayCueToolset` | `GASToolsets` | `1.0` | 8 | 4 | 1 | 2 | 1 |
| `NiagaraToolsets.NiagaraToolset_Assets` | `NiagaraToolsets` | `1.0` | 3 | 3 | 0 | 0 | 0 |
| `NiagaraToolsets.NiagaraToolset_Blueprint` | `NiagaraToolsets` | `1.0` | 2 | 0 | 0 | 2 | 0 |
| `NiagaraToolsets.NiagaraToolset_Component` | `NiagaraToolsets` | `1.0` | 4 | 2 | 0 | 2 | 0 |
| `NiagaraToolsets.NiagaraToolset_Info` | `NiagaraToolsets` | `1.0` | 1 | 1 | 0 | 0 | 0 |
| `NiagaraToolsets.NiagaraToolset_System` | `NiagaraToolsets` | `1.0` | 46 | 28 | 0 | 13 | 5 |
| `PCGToolset.PCGSpatialToolset` | `PCGToolset` | `1.0` | 1 | 0 | 1 | 0 | 0 |
| `PCGToolset.PCGToolset` | `PCGToolset` | `1.0` | 30 | 10 | 2 | 15 | 3 |
| `PhysicsToolsets.PhysicsAssetToolset` | `PhysicsToolsets` | `1.0` | 17 | 5 | 0 | 9 | 3 |
| `PluginToolset.PluginToolset` | `PluginToolset` | `1.0` | 17 | 12 | 0 | 4 | 1 |
| `SemanticSearchToolset.SemanticSearchToolset` | `SemanticSearchToolset` | `1.0` | 2 | 2 | 0 | 0 | 0 |
| `SlateInspectorToolset.SlateInspectorToolset` | `SlateInspectorToolset` | `1.0` | 14 | 3 | 11 | 0 | 0 |
| `state_tree_toolset.toolsets.state_tree.StateTreeTools` | `StateTreeToolset` | `1.0` | 9 | 9 | 0 | 0 | 0 |
| `ToolsetRegistry.AgentSkillToolset` | `ToolsetRegistry` | `1.0` | 4 | 2 | 0 | 2 | 0 |
| `UMGToolSet.UMGToolSet` | `UMGToolSet` | `1.0` | 23 | 7 | 0 | 14 | 2 |
| `UnrealBridge` | `UnrealBridge` | `3.0.0` | 2 | 2 | 0 | 0 | 0 |
| `WorldConditionsToolset.WorldConditionTools` | `WorldConditionsToolset` | `1.0` | 2 | 2 | 0 | 0 | 0 |

## 工具级决策

### aimodule_toolset.toolsets.behavior_tree.BehaviorTreeTools

| Tool | 初始分类 | 风险 | 3.0 执行面 | 保存 | 约束与依据 |
|---|---|---|---|---|---|---|
| `get_blackboard` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `37073246ff1f…`. |
| `get_children` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `997a6e16f05b…`. |
| `get_node_depth` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `4e008cc915d2…`. |
| `get_node_depths` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `37073246ff1f…`. |
| `get_root_decorators` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `37073246ff1f…`. |
| `get_subtree` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `e12a4f16e846…`. |
| `list_nodes` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `37073246ff1f…`. |

### animation_toolset.toolsets.conditions.SequencerConditionTools

| Tool | 初始分类 | 风险 | 3.0 执行面 | 保存 | 约束与依据 |
|---|---|---|---|---|---|---|
| `clear_section_condition` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `4d6a2550c8b8…`. |
| `clear_track_condition` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `dfb2a9b00d15…`. |
| `clear_track_row_condition` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `62d779a1b5de…`. |
| `get_section_condition` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `4d6a2550c8b8…`. |
| `get_track_condition` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `dfb2a9b00d15…`. |
| `get_track_row_condition` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `62d779a1b5de…`. |
| `set_section_condition` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `6dee159fcb28…`. |
| `set_track_condition` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `547945d152eb…`. |
| `set_track_row_condition` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `56710fb873a3…`. |

### animation_toolset.toolsets.controlrig.ControlRigTools

| Tool | 初始分类 | 风险 | 3.0 执行面 | 保存 | 约束与依据 |
|---|---|---|---|---|---|---|
| `add_backward_solve_graph` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `172830b94296…`. |
| `add_bone` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `ea0b865a4363…`. |
| `add_control` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `0856ad6e20a6…`. |
| `add_element` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `7d9f192e52f5…`. |
| `add_event_graph` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `5790a2a9c92d…`. |
| `add_event_node` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `ac0b9abbaf4b…`. |
| `add_graph` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `4b72490f87f1…`. |
| `add_interaction_graph` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `172830b94296…`. |
| `add_null` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `ea0b865a4363…`. |
| `add_variable` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `cd49b2b34236…`. |
| `add_variable_node` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `b117338d9d3a…`. |
| `change_variable_type` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `26c1ee35ad88…`. |
| `connect_pins` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `5d127d1a607d…`. |
| `create` | Extend | Unknown | Rejected | Rejected | UE 5.8.1 implementation calls EditorAssetLibrary.save_asset and cannot participate in a no-save preview ChangeSet. Constraints: schema `923b083c9bca…`. |
| `create_node` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `595f1066c24b…`. |
| `delete_node` | Reject | Destructive | Rejected | Rejected | Destructive tools are never exposed by the generic official adapter. Constraints: schema `e1446b10aeb3…`. |
| `disconnect_pins` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `5d127d1a607d…`. |
| `get_all_bones` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `cabb0558baf3…`. |
| `get_all_controls` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `cabb0558baf3…`. |
| `get_all_nulls` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `cabb0558baf3…`. |
| `get_backward_solve_graph` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `cabb0558baf3…`. |
| `get_children` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `222a9c670692…`. |
| `get_connected_pins` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `ffd88ce5906f…`. |
| `get_elements` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `c42f86b2bc4a…`. |
| `get_event_graph` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `05fce1fd0d90…`. |
| `get_forward_solve_graph` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `cabb0558baf3…`. |
| `get_global_transform` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `4c86d07f5124…`. |
| `get_graph` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `47475622cb2a…`. |
| `get_interaction_graph` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `cabb0558baf3…`. |
| `get_local_transform` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `4c86d07f5124…`. |
| `get_node_position` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `76ec610e7dc4…`. |
| `get_parent` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `1463f7e5089a…`. |
| `get_pin_value` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `ffd88ce5906f…`. |
| `get_variable` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `4b72490f87f1…`. |
| `import_bones_from_asset` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `68817ce4edd2…`. |
| `list_graphs` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `cabb0558baf3…`. |
| `list_nodes` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `218ca8193d0c…`. |
| `list_pins` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `76ec610e7dc4…`. |
| `list_variables` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `cabb0558baf3…`. |
| `remove_variable` | Reject | Destructive | Rejected | Rejected | Destructive tools are never exposed by the generic official adapter. Constraints: schema `4b72490f87f1…`. |
| `set_global_transform` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `513b34bb360e…`. |
| `set_local_transform` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `513b34bb360e…`. |
| `set_node_position` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `1ee4fb3c191f…`. |
| `set_pin_value` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `e6d5a4d3efe7…`. |

### animation_toolset.toolsets.controlrig_sequencer.SequencerControlRigTools

| Tool | 初始分类 | 风险 | 3.0 执行面 | 保存 | 约束与依据 |
|---|---|---|---|---|---|---|
| `add_layer_from_selection` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `a2c799262a3c…`. |
| `bake_space` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `18580e4b04e6…`. |
| `bake_to_control_rig` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `604c0757cdf7…`. |
| `blend_values_on_selected` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `7baed8445b9d…`. |
| `clear_selection` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `a2c799262a3c…`. |
| `collapse_anim_layers` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `8f9eed1ab13e…`. |
| `delete_anim_layer` | Reject | Destructive | Rejected | Rejected | Destructive tools are never exposed by the generic official adapter. Constraints: schema `d9a644ad09b3…`. |
| `delete_space` | Reject | Destructive | Rejected | Rejected | Destructive tools are never exposed by the generic official adapter. Constraints: schema `732123edb636…`. |
| `duplicate_anim_layer` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `d9a644ad09b3…`. |
| `export_fbx_from_rig` | Extend | Unknown | Rejected | Rejected | External file/source-control or asset-path mutations require a dedicated typed workflow. Constraints: schema `36180e6ba8ac…`. |
| `find_or_create_track` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `be9420c21ab9…`. |
| `frame_selection` | Extend | Unknown | RuntimeInteraction | Never | Audited runtime/editor interaction; caller must opt in to runtime side effects. Constraints: runtime opt-in, schema `a2c799262a3c…`. |
| `get_actor_transform_at_frame` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `0e6cd0b49a92…`. |
| `get_anim_layers` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `a2c799262a3c…`. |
| `get_anim_mode_gizmo_scale` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `a2c799262a3c…`. |
| `get_anim_mode_hide_manips` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `a2c799262a3c…`. |
| `get_anim_mode_hierarchy` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `a2c799262a3c…`. |
| `get_anim_mode_local_spaces` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `a2c799262a3c…`. |
| `get_anim_mode_nulls` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `a2c799262a3c…`. |
| `get_anim_mode_only_rig_sel` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `a2c799262a3c…`. |
| `get_bool` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `732123edb636…`. |
| `get_control_rigs` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `23e8b6379dc0…`. |
| `get_controls_info` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `731676100711…`. |
| `get_controls_mask` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `fd78266c5fc3…`. |
| `get_euler_transform` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `732123edb636…`. |
| `get_float` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `732123edb636…`. |
| `get_int` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `732123edb636…`. |
| `get_position` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `732123edb636…`. |
| `get_priority_order` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `731676100711…`. |
| `get_rotator` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `732123edb636…`. |
| `get_scale` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `732123edb636…`. |
| `get_selected_controls` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `731676100711…`. |
| `get_transform` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `732123edb636…`. |
| `get_vector2d` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `732123edb636…`. |
| `get_world_transform` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `732123edb636…`. |
| `hide_all_controls` | Extend | Unknown | RuntimeInteraction | Never | Audited runtime/editor interaction; caller must opt in to runtime side effects. Constraints: runtime opt-in, schema `4d6a2550c8b8…`. |
| `import_fbx_to_rig` | Extend | Unknown | Rejected | Rejected | External file/source-control or asset-path mutations require a dedicated typed workflow. Constraints: schema `2fb9f40288c1…`. |
| `is_fk_control_rig` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `731676100711…`. |
| `is_layered_control_rig` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `731676100711…`. |
| `key_controls` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `f99d608d7f6f…`. |
| `key_controls_at_frames` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `f0f1284f297a…`. |
| `load_anim_into_rig` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `5de709dba18c…`. |
| `merge_anim_layers` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `df19796c661c…`. |
| `mirror_selected_controls` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `a2c799262a3c…`. |
| `move_space` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `9b0a4e2735ad…`. |
| `reorder_anim_layers` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `f845aeb1bf4f…`. |
| `select_control` | Extend | Unknown | RuntimeInteraction | Never | Audited runtime/editor interaction; caller must opt in to runtime side effects. Constraints: runtime opt-in, schema `24da30463c68…`. |
| `select_mirrored_controls` | Extend | Unknown | RuntimeInteraction | Never | Audited runtime/editor interaction; caller must opt in to runtime side effects. Constraints: runtime opt-in, schema `a2c799262a3c…`. |
| `set_anim_mode_gizmo_scale` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `2952daf4fe9c…`. |
| `set_anim_mode_hide_manips` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `587ec2439ebe…`. |
| `set_anim_mode_hierarchy` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `7a360f55297d…`. |
| `set_anim_mode_local_spaces` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `7a360f55297d…`. |
| `set_anim_mode_nulls` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `7a360f55297d…`. |
| `set_anim_mode_only_rig_sel` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `3f4e9adfa677…`. |
| `set_bool` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `fc7bfee5ad59…`. |
| `set_controls_mask` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `fa57dde4ae5d…`. |
| `set_euler_transform` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `626ba0aadc5d…`. |
| `set_float` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `76d10867f6d2…`. |
| `set_int` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `cf998c7316c3…`. |
| `set_layered_mode` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `a26c1c126496…`. |
| `set_position` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `e8334aceeb5b…`. |
| `set_priority_order` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `113cd64ff174…`. |
| `set_rotator` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `c1bc19fe206c…`. |
| `set_scale` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `2487ec35f434…`. |
| `set_space` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `74a5f6ccfd58…`. |
| `set_transform` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `30b9d53e51ad…`. |
| `set_vector2d` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `7a7d316cce14…`. |
| `set_world_transform` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `30b9d53e51ad…`. |
| `show_all_controls` | Extend | Unknown | RuntimeInteraction | Never | Audited runtime/editor interaction; caller must opt in to runtime side effects. Constraints: runtime opt-in, schema `4d6a2550c8b8…`. |
| `snap_control_rig` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `66679632a199…`. |
| `tween_control_rig` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `4736637de016…`. |
| `zero_transforms` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `beb869d2b32a…`. |

### animation_toolset.toolsets.custom_bindings.SequencerCustomBindingTools

| Tool | 初始分类 | 风险 | 3.0 执行面 | 保存 | 约束与依据 |
|---|---|---|---|---|---|---|
| `change_actor_template_class` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `4586c8c1740e…`. |
| `convert_to_custom_binding` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `0e469409fbc2…`. |
| `convert_to_possessable` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `bbaba2591e33…`. |
| `convert_to_spawnable` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `bbaba2591e33…`. |
| `get_custom_binding_objects` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `bbaba2591e33…`. |
| `get_custom_binding_type` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `bbaba2591e33…`. |
| `get_custom_bindings_of_type` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `6b18ecaf9b0d…`. |
| `save_default_spawnable_state` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `bbaba2591e33…`. |

### animation_toolset.toolsets.import_export.SequencerImportExportTools

| Tool | 初始分类 | 风险 | 3.0 执行面 | 保存 | 约束与依据 |
|---|---|---|---|---|---|---|
| `export_anim_sequence` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `4448f0efe16c…`. |
| `export_fbx` | Extend | Unknown | Rejected | Rejected | External file/source-control or asset-path mutations require a dedicated typed workflow. Constraints: schema `2b915886af08…`. |
| `get_linked_anim_sequences` | Extend | Unknown | ReadOnly | Never | Exact UE 5.8.1 implementation was source-audited as inspection-only. Constraints: schema `23e8b6379dc0…`. |
| `get_linked_level_sequence` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `95b1307e78ef…`. |
| `import_fbx` | Extend | Unknown | Rejected | Rejected | External file/source-control or asset-path mutations require a dedicated typed workflow. Constraints: schema `9b7160fa7ba6…`. |
| `link_anim_sequence` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `b6d547b2f549…`. |

### animation_toolset.toolsets.keyframing.SequencerKeyframingTools

| Tool | 初始分类 | 风险 | 3.0 执行面 | 保存 | 约束与依据 |
|---|---|---|---|---|---|---|
| `add_key_bool` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `b80269ee30a2…`. |
| `add_key_float` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `f19f2adc54bc…`. |
| `add_key_integer` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `d248e3d9fc76…`. |
| `add_key_string` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `84570c269a23…`. |
| `bake_channel_keys` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `221cdaf718a9…`. |
| `close_curve_editor` | Extend | Unknown | RuntimeInteraction | Never | Audited runtime/editor interaction; caller must opt in to runtime side effects. Constraints: runtime opt-in, schema `a2c799262a3c…`. |
| `curve_editor_empty_selection` | Extend | Unknown | RuntimeInteraction | Never | Audited runtime/editor interaction; caller must opt in to runtime side effects. Constraints: runtime opt-in, schema `a2c799262a3c…`. |
| `curve_editor_select_keys` | Extend | Unknown | RuntimeInteraction | Never | Audited runtime/editor interaction; caller must opt in to runtime side effects. Constraints: runtime opt-in, schema `184a6b2d59f0…`. |
| `get_channel_names` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `4d6a2550c8b8…`. |
| `get_curve_editor_selected_keys` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `25ce4ee3a559…`. |
| `get_default_value` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `36b983257263…`. |
| `get_keys` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `36b983257263…`. |
| `get_keys_by_index` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `e10832b21b7e…`. |
| `get_selected_channels` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `a2c799262a3c…`. |
| `get_selected_key_channels` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `a2c799262a3c…`. |
| `is_curve_editor_open` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `a2c799262a3c…`. |
| `is_curve_shown` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `25ce4ee3a559…`. |
| `open_curve_editor` | Extend | Unknown | RuntimeInteraction | Never | Audited runtime/editor interaction; caller must opt in to runtime side effects. Constraints: runtime opt-in, schema `a2c799262a3c…`. |
| `remove_key_at_frame` | Reject | Destructive | Rejected | Rejected | Destructive tools are never exposed by the generic official adapter. Constraints: schema `418397195118…`. |
| `select_channels` | Extend | Unknown | RuntimeInteraction | Never | Audited runtime/editor interaction; caller must opt in to runtime side effects. Constraints: runtime opt-in, schema `c4259412d8f0…`. |
| `set_default_value` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `c5201cd80145…`. |
| `show_curve` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `bd097cddd6af…`. |

### animation_toolset.toolsets.outliner.SequencerOutlinerTools

| Tool | 初始分类 | 风险 | 3.0 执行面 | 保存 | 约束与依据 |
|---|---|---|---|---|---|---|
| `get_deactivated_nodes` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `a2c799262a3c…`. |
| `get_locked_nodes` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `a2c799262a3c…`. |
| `get_muted_nodes` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `a2c799262a3c…`. |
| `get_node_label` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `585e2221a661…`. |
| `get_outliner_children` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `611ea7d9dae2…`. |
| `get_outliner_selection` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `a2c799262a3c…`. |
| `get_outliner_tree` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `a2c799262a3c…`. |
| `get_pinned_nodes` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `a2c799262a3c…`. |
| `get_sections_for_nodes` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `8405e008e2f0…`. |
| `get_soloed_nodes` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `a2c799262a3c…`. |
| `is_node_expanded` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `585e2221a661…`. |
| `set_node_deactivated` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `18ad7855077f…`. |
| `set_node_expanded` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `24af7cb989db…`. |
| `set_node_locked` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `309fcc904c96…`. |
| `set_node_muted` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `53e7c4df2b47…`. |
| `set_node_pinned` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `4f47c370e670…`. |
| `set_node_solo` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `f4c5ca6e6df3…`. |
| `set_outliner_selection` | Extend | Unknown | RuntimeInteraction | Never | Audited runtime/editor interaction; caller must opt in to runtime side effects. Constraints: runtime opt-in, schema `8405e008e2f0…`. |

### animation_toolset.toolsets.sequencer.SequencerTools

| Tool | 初始分类 | 风险 | 3.0 执行面 | 保存 | 约束与依据 |
|---|---|---|---|---|---|---|
| `add_actors` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `9a7663192c9f…`. |
| `add_actors_by_name` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `184e86e4bf42…`. |
| `add_actors_to_binding` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `dab4b903b005…`. |
| `add_binding_to_folder` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `050c189a1270…`. |
| `add_event_repeater_section` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `dfb2a9b00d15…`. |
| `add_event_trigger_section` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `dfb2a9b00d15…`. |
| `add_marked_frame` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `e1e97aef3f2a…`. |
| `add_root_folder` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `b52b63bef004…`. |
| `add_section` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `dfb2a9b00d15…`. |
| `add_spawnable_from_class` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `d8cd8f72c97f…`. |
| `add_spawnable_from_instance` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `e1245ca5388b…`. |
| `add_track_to_binding` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `cc5221395f0e…`. |
| `add_track_to_folder` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `5b2e618896eb…`. |
| `add_track_to_sequence` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `f14e4f028741…`. |
| `bake_transform` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `5494d329f7e7…`. |
| `close_sequence` | Extend | Unknown | RuntimeInteraction | Never | Audited runtime/editor interaction; caller must opt in to runtime side effects. Constraints: runtime opt-in, schema `a2c799262a3c…`. |
| `copy_bindings` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `5494d329f7e7…`. |
| `copy_folders` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `51353efca751…`. |
| `copy_sections` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `46ea88aa868a…`. |
| `copy_tracks` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `b43adb0e0d1d…`. |
| `create_camera` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `acadeef1efc0…`. |
| `create_level_sequence` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `b249f58fbf53…`. |
| `delete_all_marked_frames` | Reject | Destructive | Rejected | Rejected | Destructive tools are never exposed by the generic official adapter. Constraints: schema `23e8b6379dc0…`. |
| `delete_marked_frame` | Reject | Destructive | Rejected | Rejected | Destructive tools are never exposed by the generic official adapter. Constraints: schema `8c8ddc07e96d…`. |
| `empty_selection` | Extend | Unknown | RuntimeInteraction | Never | Audited runtime/editor interaction; caller must opt in to runtime side effects. Constraints: runtime opt-in, schema `a2c799262a3c…`. |
| `find_binding_by_name` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `b52b63bef004…`. |
| `find_binding_by_tag` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `e1c2d2ae0fe2…`. |
| `find_bindings_by_tag` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `e1c2d2ae0fe2…`. |
| `find_marked_frame_by_label` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `79c14eddbf42…`. |
| `find_tracks_by_type` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `cc5221395f0e…`. |
| `fix_actor_references` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `a2c799262a3c…`. |
| `focus_parent_sequence` | Extend | Unknown | RuntimeInteraction | Never | Audited runtime/editor interaction; caller must opt in to runtime side effects. Constraints: runtime opt-in, schema `a2c799262a3c…`. |
| `focus_sub_sequence` | Extend | Unknown | RuntimeInteraction | Never | Audited runtime/editor interaction; caller must opt in to runtime side effects. Constraints: runtime opt-in, schema `f00140830eff…`. |
| `force_evaluate` | Extend | Unknown | RuntimeInteraction | Never | Audited runtime/editor interaction; caller must opt in to runtime side effects. Constraints: runtime opt-in, schema `a2c799262a3c…`. |
| `get_all_binding_tags` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `23e8b6379dc0…`. |
| `get_binding_id` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `ae30768f8a96…`. |
| `get_binding_name` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `bbaba2591e33…`. |
| `get_binding_tags` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `bbaba2591e33…`. |
| `get_bindings` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `23e8b6379dc0…`. |
| `get_bound_objects` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `bbaba2591e33…`. |
| `get_child_possessables` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `bbaba2591e33…`. |
| `get_clock_source` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `23e8b6379dc0…`. |
| `get_current_sequence` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `a2c799262a3c…`. |
| `get_display_rate` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `23e8b6379dc0…`. |
| `get_evaluation_type` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `23e8b6379dc0…`. |
| `get_focused_sequence` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `a2c799262a3c…`. |
| `get_folder_contents` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `ede9eb3e2f38…`. |
| `get_loop_mode` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `a2c799262a3c…`. |
| `get_marked_frames` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `23e8b6379dc0…`. |
| `get_playback_range` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `23e8b6379dc0…`. |
| `get_playback_speed` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `a2c799262a3c…`. |
| `get_playhead_frame` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `a2c799262a3c…`. |
| `get_root_folders` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `23e8b6379dc0…`. |
| `get_section_blend_type` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `4d6a2550c8b8…`. |
| `get_section_completion_mode` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `4d6a2550c8b8…`. |
| `get_section_ease_in` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `4d6a2550c8b8…`. |
| `get_section_ease_out` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `4d6a2550c8b8…`. |
| `get_section_post_roll_frames` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `4d6a2550c8b8…`. |
| `get_section_pre_roll_frames` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `4d6a2550c8b8…`. |
| `get_section_properties` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `4d6a2550c8b8…`. |
| `get_section_range` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `4d6a2550c8b8…`. |
| `get_section_to_key` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `dfb2a9b00d15…`. |
| `get_sections` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `dfb2a9b00d15…`. |
| `get_selected_bindings` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `a2c799262a3c…`. |
| `get_selected_folders` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `a2c799262a3c…`. |
| `get_selected_sections` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `a2c799262a3c…`. |
| `get_selected_tracks` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `a2c799262a3c…`. |
| `get_selection_range` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `a2c799262a3c…`. |
| `get_sequence_lock_state` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `a2c799262a3c…`. |
| `get_sub_sequence_hierarchy` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `a2c799262a3c…`. |
| `get_tick_resolution` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `23e8b6379dc0…`. |
| `get_track_display_name` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `dfb2a9b00d15…`. |
| `get_track_filter_names` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `a2c799262a3c…`. |
| `get_tracks_on_binding` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `bbaba2591e33…`. |
| `get_tracks_on_sequence` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `23e8b6379dc0…`. |
| `get_view_range` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `23e8b6379dc0…`. |
| `get_work_range` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `23e8b6379dc0…`. |
| `has_section_end_frame` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `4d6a2550c8b8…`. |
| `has_section_start_frame` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `4d6a2550c8b8…`. |
| `is_camera_cut_locked` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `a2c799262a3c…`. |
| `is_playback_range_locked` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `23e8b6379dc0…`. |
| `is_playing` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `a2c799262a3c…`. |
| `is_sequence_locked` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `a2c799262a3c…`. |
| `is_track_filter_active` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `098974972159…`. |
| `open_sequence` | Extend | Unknown | RuntimeInteraction | Never | Audited runtime/editor interaction; caller must opt in to runtime side effects. Constraints: runtime opt-in, schema `23e8b6379dc0…`. |
| `paste_bindings` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `f7e4e29ea4d9…`. |
| `paste_folders` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `f7e4e29ea4d9…`. |
| `paste_sections` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `1f6585a8df39…`. |
| `paste_tracks` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `adc67046dd2d…`. |
| `pause` | Extend | Unknown | RuntimeInteraction | Never | Audited runtime/editor interaction; caller must opt in to runtime side effects. Constraints: runtime opt-in, schema `a2c799262a3c…`. |
| `play` | Extend | Unknown | RuntimeInteraction | Never | Audited runtime/editor interaction; caller must opt in to runtime side effects. Constraints: runtime opt-in, schema `a2c799262a3c…`. |
| `play_to` | Extend | Unknown | RuntimeInteraction | Never | Audited runtime/editor interaction; caller must opt in to runtime side effects. Constraints: runtime opt-in, schema `df50446c9b41…`. |
| `rebind_component` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `60a9dfc4d581…`. |
| `refresh_sequence` | Extend | Unknown | RuntimeInteraction | Never | Audited runtime/editor interaction; caller must opt in to runtime side effects. Constraints: runtime opt-in, schema `a2c799262a3c…`. |
| `remove_actors_from_binding` | Reject | Destructive | Rejected | Rejected | Destructive tools are never exposed by the generic official adapter. Constraints: schema `dab4b903b005…`. |
| `remove_all_bindings` | Reject | Destructive | Rejected | Rejected | Destructive tools are never exposed by the generic official adapter. Constraints: schema `bbaba2591e33…`. |
| `remove_binding` | Reject | Destructive | Rejected | Rejected | Destructive tools are never exposed by the generic official adapter. Constraints: schema `bbaba2591e33…`. |
| `remove_binding_tag` | Reject | Destructive | Rejected | Rejected | Destructive tools are never exposed by the generic official adapter. Constraints: schema `e1c2d2ae0fe2…`. |
| `remove_invalid_bindings` | Reject | Destructive | Rejected | Rejected | Destructive tools are never exposed by the generic official adapter. Constraints: schema `bbaba2591e33…`. |
| `remove_root_folder` | Reject | Destructive | Rejected | Rejected | Destructive tools are never exposed by the generic official adapter. Constraints: schema `01b3d63d305a…`. |
| `remove_section` | Reject | Destructive | Rejected | Rejected | Destructive tools are never exposed by the generic official adapter. Constraints: schema `f46d3576ed43…`. |
| `remove_track` | Reject | Destructive | Rejected | Rejected | Destructive tools are never exposed by the generic official adapter. Constraints: schema `899d54dd925b…`. |
| `remove_track_from_sequence` | Reject | Destructive | Rejected | Rejected | Destructive tools are never exposed by the generic official adapter. Constraints: schema `78ff984b57c7…`. |
| `replace_binding_with_actors` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `dab4b903b005…`. |
| `select_bindings` | Extend | Unknown | RuntimeInteraction | Never | Audited runtime/editor interaction; caller must opt in to runtime side effects. Constraints: runtime opt-in, schema `5494d329f7e7…`. |
| `select_folders` | Extend | Unknown | RuntimeInteraction | Never | Audited runtime/editor interaction; caller must opt in to runtime side effects. Constraints: runtime opt-in, schema `51353efca751…`. |
| `select_sections` | Extend | Unknown | RuntimeInteraction | Never | Audited runtime/editor interaction; caller must opt in to runtime side effects. Constraints: runtime opt-in, schema `46ea88aa868a…`. |
| `select_tracks` | Extend | Unknown | RuntimeInteraction | Never | Audited runtime/editor interaction; caller must opt in to runtime side effects. Constraints: runtime opt-in, schema `b43adb0e0d1d…`. |
| `set_binding_name` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `7f7db7aaeb14…`. |
| `set_byte_track_enum` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `5c5624337bca…`. |
| `set_camera_cut_binding` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `ad570b24d01e…`. |
| `set_camera_lock` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `1d0fe0068c00…`. |
| `set_clock_source` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `f459ee82e8ed…`. |
| `set_display_rate` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `f9b92aa4c9e0…`. |
| `set_evaluation_type` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `630ef33d01b6…`. |
| `set_loop_mode` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `3c18ec4b3684…`. |
| `set_playback_range` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `6316556255ad…`. |
| `set_playback_range_locked` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `f3cd3b40fabd…`. |
| `set_playback_speed` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `3dc6dd4d068d…`. |
| `set_playhead_frame` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `df50446c9b41…`. |
| `set_property_name_and_path` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `520e02376b55…`. |
| `set_section_animation` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `cb2f7e482d94…`. |
| `set_section_blend_type` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `d9006a35d89e…`. |
| `set_section_completion_mode` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `e9178b3111c5…`. |
| `set_section_ease_in` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `8254dc0f5aaa…`. |
| `set_section_ease_out` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `8254dc0f5aaa…`. |
| `set_section_end_bounded` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `f1e197e75163…`. |
| `set_section_post_roll_frames` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `2124dbfc559a…`. |
| `set_section_pre_roll_frames` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `2124dbfc559a…`. |
| `set_section_range` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `082de8290670…`. |
| `set_section_start_bounded` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `f1e197e75163…`. |
| `set_selection_range` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `8a7cdc23f4a0…`. |
| `set_sequence_locked` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `1d0fe0068c00…`. |
| `set_tick_resolution` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `f9b92aa4c9e0…`. |
| `set_track_display_name` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `026d4bc48285…`. |
| `set_track_filter_active` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `0c15f2d49e35…`. |
| `set_view_range` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `e92f8e14ba32…`. |
| `set_work_range` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `e92f8e14ba32…`. |
| `tag_binding` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `09fc546a2373…`. |
| `untag_binding` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `09fc546a2373…`. |

### AutomationTestToolset.AutomationTestToolset

| Tool | 初始分类 | 风险 | 3.0 执行面 | 保存 | 约束与依据 |
|---|---|---|---|---|---|---|
| `DiscoverTests` | Extend | Unknown | RuntimeInteraction | Never | Audited runtime/editor interaction; caller must opt in to runtime side effects. Constraints: runtime opt-in, schema `f88266cc368b…`. |
| `GetTestResults` | Extend | Unknown | ReadOnly | Never | Exact UE 5.8.1 implementation was source-audited as inspection-only. Constraints: schema `a2c799262a3c…`. |
| `GetTestStatus` | Extend | Unknown | ReadOnly | Never | Exact UE 5.8.1 implementation was source-audited as inspection-only. Constraints: schema `a2c799262a3c…`. |
| `ListTests` | Extend | Unknown | ReadOnly | Never | Exact UE 5.8.1 implementation was source-audited as inspection-only. Constraints: schema `a9f1d5a956d5…`. |
| `RunTests` | Extend | Unknown | RuntimeInteraction | Never | Audited runtime/editor interaction; caller must opt in to runtime side effects. Constraints: runtime opt-in, schema `291bec64c385…`. |
| `RunTestsByFilter` | Extend | Unknown | RuntimeInteraction | Never | Audited runtime/editor interaction; caller must opt in to runtime side effects. Constraints: runtime opt-in, schema `e791f7ddffee…`. |
| `StopTests` | Extend | Unknown | RuntimeInteraction | Never | Audited runtime/editor interaction; caller must opt in to runtime side effects. Constraints: runtime opt-in, schema `a2c799262a3c…`. |

### ConfigSettingsToolset.ConfigSettingsToolset

| Tool | 初始分类 | 风险 | 3.0 执行面 | 保存 | 约束与依据 |
|---|---|---|---|---|---|---|
| `GetSectionPropertyValues` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `7fec4969c1cb…`. |
| `GetSectionSchema` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `9f848349e469…`. |
| `ListCategories` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `8404fa99abb4…`. |
| `ListContainers` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `a2c799262a3c…`. |
| `ListSections` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `d09e68be8b60…`. |
| `ResetSectionToDefaults` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `9f848349e469…`. |
| `SaveSection` | Extend | Unknown | Rejected | Rejected | External file/source-control or asset-path mutations require a dedicated typed workflow. Constraints: schema `9f848349e469…`. |
| `SetSectionProperties` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `c6e13b6c53fe…`. |

### conversation_toolset.toolsets.conversation.ConversationTools

| Tool | 初始分类 | 风险 | 3.0 执行面 | 保存 | 约束与依据 |
|---|---|---|---|---|---|---|
| `get_all_nodes` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `f2696b8e3244…`. |
| `get_node_by_guid` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `dd0fcf99c1e4…`. |
| `get_node_connections` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `e12a4f16e846…`. |
| `get_node_guids` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `f2696b8e3244…`. |
| `get_sub_nodes` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `e12a4f16e846…`. |
| `list_entry_points` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `f2696b8e3244…`. |
| `list_speakers` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `f2696b8e3244…`. |

### DataflowAgent.DataflowAgentToolset

| Tool | 初始分类 | 风险 | 3.0 执行面 | 保存 | 约束与依据 |
|---|---|---|---|---|---|---|
| `AddCommentBox` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `bceccb532be8…`. |
| `AddNode` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `e2b9f168977b…`. |
| `AddVariable` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `4987ab8a3b32…`. |
| `AssignDataflowTemplate` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `ecdaea44520e…`. |
| `ConnectNodePins` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `2ee5fade0f70…`. |
| `CreateDataflowCompatibleAsset` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `df6c4fb19101…`. |
| `CreateDataflowCompatibleAssetFromTemplate` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `af0c5b5ab0c1…`. |
| `CreateGraph` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `795f594e4a34…`. |
| `DisconnectNodePins` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `2ee5fade0f70…`. |
| `GetGraphStructure` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `476f8f771c9c…`. |
| `GetNodeInfo` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `e12a4f16e846…`. |
| `GetNodeTypeSchema` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `eb9856a3a515…`. |
| `ListDataflowCompatibleAssetTypes` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `a2c799262a3c…`. |
| `ListDataflowTemplatesForAssetClass` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `ed79771d61c1…`. |
| `ListNodeTypes` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `9addb92f2c92…`. |
| `ListVariables` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `476f8f771c9c…`. |
| `RemoveCommentBox` | Reject | Destructive | Rejected | Rejected | Destructive tools are never exposed by the generic official adapter. Constraints: schema `05f1f13f6e39…`. |
| `RemoveNode` | Reject | Destructive | Rejected | Rejected | Destructive tools are never exposed by the generic official adapter. Constraints: schema `a3b6a73d09c3…`. |
| `RemoveVariable` | Reject | Destructive | Rejected | Rejected | Destructive tools are never exposed by the generic official adapter. Constraints: schema `98dcac869d30…`. |
| `RepositionNode` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `b78a54f8944f…`. |
| `SetVariable` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `26558a4d2fb5…`. |
| `UpdateNode` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `d7c9e959c699…`. |

### DataRegistryToolset.DataRegistryTools

| Tool | 初始分类 | 风险 | 3.0 执行面 | 保存 | 约束与依据 |
|---|---|---|---|---|---|---|
| `GetItems` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `03a52327f155…`. |
| `GetRegistryInfo` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `616a793e52c3…`. |
| `GetSchema` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `616a793e52c3…`. |
| `ListDataSources` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `616a793e52c3…`. |
| `ListItems` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `616a793e52c3…`. |
| `ListRegistries` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `90233872897d…`. |
| `ListRuntimeSources` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `616a793e52c3…`. |

### editor_toolset.toolsets.actor.ActorTools

| Tool | 初始分类 | 风险 | 3.0 执行面 | 保存 | 约束与依据 |
|---|---|---|---|---|---|---|
| `add_component` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `9b2c5fcab39a…`. |
| `add_tag` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `2341ef82af71…`. |
| `get_actor_bounds` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `b13e970e2b13…`. |
| `get_actor_transform` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `b13e970e2b13…`. |
| `get_component_actor` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `51fcb41efe92…`. |
| `get_components` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `be6ed8711aea…`. |
| `get_label` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `b13e970e2b13…`. |
| `get_parent_component` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `51fcb41efe92…`. |
| `get_root_component` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `b13e970e2b13…`. |
| `get_tags` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `b13e970e2b13…`. |
| `has_tag` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `2341ef82af71…`. |
| `look_at` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `03eff3c500f3…`. |
| `remove_component` | Reject | Destructive | Rejected | Rejected | Destructive tools are never exposed by the generic official adapter. Constraints: schema `51fcb41efe92…`. |
| `remove_tag` | Reject | Destructive | Rejected | Rejected | Destructive tools are never exposed by the generic official adapter. Constraints: schema `2341ef82af71…`. |
| `set_actor_transform` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `a2a243c9f2f2…`. |
| `set_label` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `f5db04870ee9…`. |
| `set_parent_component` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `5ca8bd0d4f82…`. |

### editor_toolset.toolsets.asset.AssetTools

| Tool | 初始分类 | 风险 | 3.0 执行面 | 保存 | 约束与依据 |
|---|---|---|---|---|---|---|
| `can_edit_asset` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `aee1d26a1edd…`. |
| `create_folder` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `923b083c9bca…`. |
| `delete` | Reject | Destructive | Rejected | Rejected | Destructive tools are never exposed by the generic official adapter. Constraints: schema `923b083c9bca…`. |
| `duplicate` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `a5e69d2694f4…`. |
| `exists` | Extend | Unknown | ReadOnly | Never | Exact UE 5.8.1 implementation was source-audited as inspection-only. Constraints: schema `923b083c9bca…`. |
| `find_assets` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `742445a30c87…`. |
| `get_asset_class` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `aee1d26a1edd…`. |
| `get_asset_tags` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `aee1d26a1edd…`. |
| `get_dependencies` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `aee1d26a1edd…`. |
| `get_metadata_tags` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `aee1d26a1edd…`. |
| `get_plugin_content_paths` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `193a73255849…`. |
| `get_referencers` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `aee1d26a1edd…`. |
| `is_checked_out` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `aee1d26a1edd…`. |
| `is_dirty` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `aee1d26a1edd…`. |
| `list_folders` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `9e434667c32e…`. |
| `load_asset` | Extend | Unknown | ReadOnly | Never | Exact UE 5.8.1 implementation was source-audited as inspection-only. Constraints: schema `aee1d26a1edd…`. |
| `move` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `a5e69d2694f4…`. |
| `read_file` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `93f80aab9fb3…`. |
| `save_assets` | Extend | Unknown | Rejected | Rejected | Source audit found explicit disk-save call(s): eas.save_asset, unreal.EditorLoadingAndSavingUtils.save_dirty_packages. Constraints: schema `7decfa9116b6…`. |
| `update_metadata_tags` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `52c0dec05c65…`. |
| `write_file` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `0c531496444a…`. |

### editor_toolset.toolsets.blueprint.BlueprintTools

| Tool | 初始分类 | 风险 | 3.0 执行面 | 保存 | 约束与依据 |
|---|---|---|---|---|---|---|
| `add_component_bound_event` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `24b3e35158c6…`. |
| `add_event` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `d5a2a9b12272…`. |
| `add_event_dispatcher` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `a129e1f87b7b…`. |
| `add_function_graph` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `9eeda9bdc3bf…`. |
| `add_function_param` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `8eb33abd0d8d…`. |
| `add_node_pin` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `e12a4f16e846…`. |
| `add_object_function_param` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `114ded2fe5f8…`. |
| `add_object_variable` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `75619b5bf181…`. |
| `add_struct_function_param` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `328bc43001bf…`. |
| `add_struct_variable` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `1de206abda73…`. |
| `add_variable` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `fa466ada4cfc…`. |
| `arrange_nodes` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `db23b10c74b0…`. |
| `break_pins` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `c017635d4676…`. |
| `compile_blueprint` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `1ad16af3e634…`. |
| `connect_pins` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `c017635d4676…`. |
| `create` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `1c7a96476496…`. |
| `create_node` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `d03c15fd0500…`. |
| `delete_node` | Reject | Destructive | Rejected | Rejected | Destructive tools are never exposed by the generic official adapter. Constraints: schema `e12a4f16e846…`. |
| `find_node_categories` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `6d9df9ae06c7…`. |
| `find_node_types` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `e857d61b08db…`. |
| `find_nodes` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `d4f56fba727a…`. |
| `get_connected_subgraph` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `e12a4f16e846…`. |
| `get_create_event_function` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `e12a4f16e846…`. |
| `get_default_object` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `310d5911f7d7…`. |
| `get_graph` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `9eeda9bdc3bf…`. |
| `get_graph_dsl_docs` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `a2c799262a3c…`. |
| `get_node_infos` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `db23b10c74b0…`. |
| `get_node_type_pins` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `fc0070e2c636…`. |
| `get_parent` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `310d5911f7d7…`. |
| `get_pin_value` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `31adcb757946…`. |
| `get_variable_category` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `7d0b603f062f…`. |
| `get_variable_replication` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `7d0b603f062f…`. |
| `list_compatible_event_functions` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `e12a4f16e846…`. |
| `list_component_events` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `51fcb41efe92…`. |
| `list_event_dispatchers` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `310d5911f7d7…`. |
| `list_events` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `310d5911f7d7…`. |
| `list_functions` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `310d5911f7d7…`. |
| `list_graphs` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `310d5911f7d7…`. |
| `list_variables` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `1ea326a21c65…`. |
| `read_graph_dsl` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `476f8f771c9c…`. |
| `remove_function_graph` | Reject | Destructive | Rejected | Rejected | Destructive tools are never exposed by the generic official adapter. Constraints: schema `9eeda9bdc3bf…`. |
| `remove_function_param` | Reject | Destructive | Rejected | Rejected | Destructive tools are never exposed by the generic official adapter. Constraints: schema `1b3103737ff2…`. |
| `remove_node_pin` | Reject | Destructive | Rejected | Rejected | Destructive tools are never exposed by the generic official adapter. Constraints: schema `1fa430f9f841…`. |
| `remove_variable` | Reject | Destructive | Rejected | Rejected | Destructive tools are never exposed by the generic official adapter. Constraints: schema `99c9462d8073…`. |
| `retarget_node_class` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `070c35a436a3…`. |
| `set_create_event_function` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `0eb5125d6dea…`. |
| `set_node_position` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `cfb964de429c…`. |
| `set_parent` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `cad637c19ed3…`. |
| `set_pin_value` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `978aa3dfcbcc…`. |
| `set_variable_category` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `af2de13ee4ca…`. |
| `set_variable_instance_editable` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `df46b5684ff8…`. |
| `set_variable_replication` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `dd207ccda671…`. |
| `write_graph_dsl` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `e4d96967a1d6…`. |

### editor_toolset.toolsets.curve_table.CurveTableTools

| Tool | 初始分类 | 风险 | 3.0 执行面 | 保存 | 约束与依据 |
|---|---|---|---|---|---|---|
| `add_key` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `4005d606b639…`. |
| `add_row` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `d23535336e22…`. |
| `create` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `7874c52fef0e…`. |
| `get_keys` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `3fb348840809…`. |
| `import_file` | Extend | Unknown | Rejected | Rejected | External file/source-control or asset-path mutations require a dedicated typed workflow. Constraints: schema `46c4b526d734…`. |
| `list_rows` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `a8254018ae07…`. |
| `remove_row` | Reject | Destructive | Rejected | Rejected | Destructive tools are never exposed by the generic official adapter. Constraints: schema `3fb348840809…`. |
| `rename_row` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `40c174bde36e…`. |
| `set_keys` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `3248b9965615…`. |

### editor_toolset.toolsets.data_asset.DataAssetTools

| Tool | 初始分类 | 风险 | 3.0 执行面 | 保存 | 约束与依据 |
|---|---|---|---|---|---|---|
| `create` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `1c7a96476496…`. |

### editor_toolset.toolsets.data_table.DataTableTools

| Tool | 初始分类 | 风险 | 3.0 执行面 | 保存 | 约束与依据 |
|---|---|---|---|---|---|---|
| `add_rows` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `cb665fabaa51…`. |
| `create` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `7df1e3693064…`. |
| `get_rows` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `cb665fabaa51…`. |
| `get_schema` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `0fd78fc483f9…`. |
| `import_file` | Extend | Unknown | Rejected | Rejected | External file/source-control or asset-path mutations require a dedicated typed workflow. Constraints: schema `d9876aeabeb8…`. |
| `list_rows` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `0fd78fc483f9…`. |
| `remove_rows` | Reject | Destructive | Rejected | Rejected | Destructive tools are never exposed by the generic official adapter. Constraints: schema `cb665fabaa51…`. |
| `rename_rows` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `ae5a1775ce62…`. |
| `search_row_structs` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `ef32e8610449…`. |
| `set_rows` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `4eaa0724d77d…`. |

### editor_toolset.toolsets.material.MaterialTools

| Tool | 初始分类 | 风险 | 3.0 执行面 | 保存 | 约束与依据 |
|---|---|---|---|---|---|---|
| `add_expression` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `16a41018922b…`. |
| `connect_expressions` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `70b7464a8f40…`. |
| `connect_to_output` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `443c4ff40275…`. |
| `create_function` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `7874c52fef0e…`. |
| `create_material` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `7874c52fef0e…`. |
| `create_parameter_collection` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `7874c52fef0e…`. |
| `delete_expression` | Reject | Destructive | Rejected | Rejected | Destructive tools are never exposed by the generic official adapter. Constraints: schema `d556faaa5840…`. |
| `delete_parameter_group` | Reject | Destructive | Rejected | Rejected | Destructive tools are never exposed by the generic official adapter. Constraints: schema `c2140a7cf284…`. |
| `delete_unused_expressions` | Reject | Destructive | Rejected | Rejected | Destructive tools are never exposed by the generic official adapter. Constraints: schema `8722e436cd21…`. |
| `disconnect_expressions` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `3e1e1bec96bb…`. |
| `disconnect_from_output` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `c4c1149f4f6f…`. |
| `get_expression_input_names` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `e61dbc37b0ff…`. |
| `get_expression_inputs` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `d556faaa5840…`. |
| `get_expression_output_names` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `e61dbc37b0ff…`. |
| `get_expressions` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `e7314d055aac…`. |
| `get_property_input` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `c4c1149f4f6f…`. |
| `get_referencing_materials` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `4d2404ec6877…`. |
| `layout_expressions` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `e7314d055aac…`. |
| `list_expression_classes` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `f838c7a116ca…`. |
| `list_parameter_groups` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `e7314d055aac…`. |
| `recompile` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `e7314d055aac…`. |
| `rename_parameter_group` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `d9c255e8aa30…`. |

### editor_toolset.toolsets.material_instance.MaterialInstanceTools

| Tool | 初始分类 | 风险 | 3.0 执行面 | 保存 | 约束与依据 |
|---|---|---|---|---|---|---|
| `clear_parameters` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `3be740528eb1…`. |
| `create` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `478504905f86…`. |
| `get_scalar_parameter` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `b63a84c0ff74…`. |
| `get_static_switch_parameter` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `b63a84c0ff74…`. |
| `get_texture_parameter` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `b63a84c0ff74…`. |
| `get_vector_parameter` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `b63a84c0ff74…`. |
| `list_parameters` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `8722e436cd21…`. |
| `set_parameter_override` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `72f4b513509d…`. |
| `set_parent` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `9168169f54c4…`. |
| `set_scalar_parameter` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `42c074abc6fd…`. |
| `set_static_switch_parameter` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `479056690d95…`. |
| `set_texture_parameter` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `53ed6fb571b4…`. |
| `set_vector_parameter` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `16080dcf702c…`. |

### editor_toolset.toolsets.object.ObjectTools

| Tool | 初始分类 | 风险 | 3.0 执行面 | 保存 | 约束与依据 |
|---|---|---|---|---|---|---|
| `get_class` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `3be740528eb1…`. |
| `get_properties` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `64a0c8786d9c…`. |
| `list_properties` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `3be740528eb1…`. |
| `reset_properties` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `64a0c8786d9c…`. |
| `search_subclasses` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `d158e934f1c5…`. |
| `set_properties` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `9c60cd257a39…`. |

### editor_toolset.toolsets.primitive.PrimitiveTools

| Tool | 初始分类 | 风险 | 3.0 执行面 | 保存 | 约束与依据 |
|---|---|---|---|---|---|---|
| `add_cone` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `169354b473c5…`. |
| `add_cube` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `5b34c92e3498…`. |
| `add_cylinder` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `169354b473c5…`. |
| `add_sphere` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `2bbdc4f4bda6…`. |

### editor_toolset.toolsets.programmatic.ProgrammaticToolset

| Tool | 初始分类 | 风险 | 3.0 执行面 | 保存 | 约束与依据 |
|---|---|---|---|---|---|---|
| `execute_tool_script` | Extend | Unknown | Rejected | Rejected | Arbitrary provider-side Python execution is outside the typed official adapter. Constraints: schema `70b1c2f5190f…`. |
| `get_execution_environment` | Extend | Unknown | ReadOnly | Never | Exact UE 5.8.1 implementation was source-audited as inspection-only. Constraints: schema `a2c799262a3c…`. |

### editor_toolset.toolsets.scene.SceneTools

| Tool | 初始分类 | 风险 | 3.0 执行面 | 保存 | 约束与依据 |
|---|---|---|---|---|---|---|
| `add_to_scene_from_asset` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `f33cea19b7b2…`. |
| `add_to_scene_from_class` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `e924d21f401d…`. |
| `can_edit` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `b13e970e2b13…`. |
| `commit_level_instance` | Extend | Unknown | Rejected | Rejected | UE 5.8.1 implementation can call save_dirty_packages and commit level instance changes to disk. Constraints: schema `f197db805d25…`. |
| `create_level_instance` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `0590a268955b…`. |
| `delete_folder` | Reject | Destructive | Rejected | Rejected | Destructive tools are never exposed by the generic official adapter. Constraints: schema `56d92ca8c617…`. |
| `edit_level_instance` | Extend | Unknown | RuntimeInteraction | Never | Enters persistent Editor level-instance edit mode; explicit runtime-side-effect opt-in is required. Constraints: runtime opt-in, schema `d246bcaeb957…`. |
| `find_actors` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `721a3cfc057a…`. |
| `get_actors_in_folder` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `adb521bdc95b…`. |
| `get_collision_channels` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `a2c799262a3c…`. |
| `get_current_level` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `a2c799262a3c…`. |
| `get_folders` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `a2c799262a3c…`. |
| `is_checked_out` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `b13e970e2b13…`. |
| `load_level` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `4ee5082a6b24…`. |
| `merge_actors` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `e75b2aa13620…`. |
| `remove_from_scene` | Reject | Destructive | Rejected | Rejected | Destructive tools are never exposed by the generic official adapter. Constraints: schema `b13e970e2b13…`. |
| `rename_folder` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `4d5ee41ffe3a…`. |
| `save_actor` | Extend | Unknown | Rejected | Rejected | Source audit found explicit disk-save call(s): AssetTools.save_assets. Constraints: schema `b13e970e2b13…`. |
| `set_actor_folder` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `d32dc4fcfb4f…`. |
| `trace_world` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `6f8ac034ec81…`. |

### editor_toolset.toolsets.skeletal_mesh.SkeletalMeshTools

| Tool | 初始分类 | 风险 | 3.0 执行面 | 保存 | 约束与依据 |
|---|---|---|---|---|---|---|
| `add_socket` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `cdd6abae8297…`. |
| `assign_physics_asset` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `a92f5dded12c…`. |
| `get_bone_children` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `c4b774560010…`. |
| `get_bone_names` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `e18906c3ff8e…`. |
| `get_bone_parent` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `c4b774560010…`. |
| `get_bounds` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `e18906c3ff8e…`. |
| `get_lod_count` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `e18906c3ff8e…`. |
| `get_material` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `ada36e223248…`. |
| `get_material_slots` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `e18906c3ff8e…`. |
| `get_morph_target_names` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `e18906c3ff8e…`. |
| `get_physics_asset` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `e18906c3ff8e…`. |
| `get_section_count` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `036bd038c199…`. |
| `get_skeleton` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `e18906c3ff8e…`. |
| `get_socket_bone` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `0c9505657ebc…`. |
| `get_socket_names` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `e18906c3ff8e…`. |
| `get_socket_transform` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `0c9505657ebc…`. |
| `get_vertex_count` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `036bd038c199…`. |
| `import_file` | Extend | Unknown | Rejected | Rejected | External file/source-control or asset-path mutations require a dedicated typed workflow. Constraints: schema `91694593aff7…`. |
| `remove_socket` | Reject | Destructive | Rejected | Rejected | Destructive tools are never exposed by the generic official adapter. Constraints: schema `0c9505657ebc…`. |
| `rename_socket` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `65c1c3080773…`. |
| `set_material` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `bec5f8eef779…`. |
| `set_socket_transform` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `75cacecc7aa1…`. |

### editor_toolset.toolsets.static_mesh.StaticMeshTools

| Tool | 初始分类 | 风险 | 3.0 执行面 | 保存 | 约束与依据 |
|---|---|---|---|---|---|---|
| `generate_convex_collisions` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `68cc2120fa12…`. |
| `generate_lods` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `be593067b4bd…`. |
| `get_bounds` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `e18906c3ff8e…`. |
| `get_lod_count` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `e18906c3ff8e…`. |
| `get_lod_thresholds` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `e18906c3ff8e…`. |
| `get_material` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `ada36e223248…`. |
| `get_material_slots` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `e18906c3ff8e…`. |
| `get_triangle_count` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `036bd038c199…`. |
| `get_vertex_count` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `036bd038c199…`. |
| `import_file` | Extend | Unknown | Rejected | Rejected | External file/source-control or asset-path mutations require a dedicated typed workflow. Constraints: schema `5629b870d050…`. |
| `is_nanite_enabled` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `e18906c3ff8e…`. |
| `remove_collisions` | Reject | Destructive | Rejected | Rejected | Destructive tools are never exposed by the generic official adapter. Constraints: schema `e18906c3ff8e…`. |
| `remove_lods` | Reject | Destructive | Rejected | Rejected | Destructive tools are never exposed by the generic official adapter. Constraints: schema `e18906c3ff8e…`. |
| `set_lod_thresholds` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `d5434924195d…`. |
| `set_material` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `bec5f8eef779…`. |
| `set_nanite_enabled` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `e56a2b59657c…`. |

### editor_toolset.toolsets.string_table.StringTableTools

| Tool | 初始分类 | 风险 | 3.0 执行面 | 保存 | 约束与依据 |
|---|---|---|---|---|---|---|
| `create` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `7874c52fef0e…`. |
| `get_entry` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `6bf639796672…`. |
| `get_namespace` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `2c394fb461b2…`. |
| `get_table_id` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `2c394fb461b2…`. |
| `import_file` | Extend | Unknown | Rejected | Rejected | External file/source-control or asset-path mutations require a dedicated typed workflow. Constraints: schema `1b17139e907a…`. |
| `list_keys` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `2c394fb461b2…`. |
| `remove_entry` | Reject | Destructive | Rejected | Rejected | Destructive tools are never exposed by the generic official adapter. Constraints: schema `6bf639796672…`. |
| `set_entry` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `34fa2fe5ebe4…`. |

### editor_toolset.toolsets.texture.TextureTools

| Tool | 初始分类 | 风险 | 3.0 执行面 | 保存 | 约束与依据 |
|---|---|---|---|---|---|---|
| `get_size` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `2c42c55bd405…`. |
| `import_file` | Extend | Unknown | Rejected | Rejected | External file/source-control or asset-path mutations require a dedicated typed workflow. Constraints: schema `1b17139e907a…`. |

### EditorToolset.EditorAppToolset

| Tool | 初始分类 | 风险 | 3.0 执行面 | 保存 | 约束与依据 |
|---|---|---|---|---|---|---|
| `CaptureAssetImage` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `a0c3f824ae75…`. |
| `CaptureEditorImage` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `a2c799262a3c…`. |
| `CaptureViewport` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `d9c6e514f041…`. |
| `FocusOnActors` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `9a7663192c9f…`. |
| `GetCameraTransform` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `a2c799262a3c…`. |
| `GetContentBrowserPath` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `a2c799262a3c…`. |
| `GetOpenAssets` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `a2c799262a3c…`. |
| `GetSelectedActors` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `a2c799262a3c…`. |
| `GetSelectedAssets` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `a2c799262a3c…`. |
| `GetVisibleActors` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `a2c799262a3c…`. |
| `IsPIERunning` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `a2c799262a3c…`. |
| `OpenEditorForAsset` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `a0c3f824ae75…`. |
| `ScreenCoordsToWorld` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `4a11d656e051…`. |
| `SearchCVars` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `098974972159…`. |
| `SelectActors` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `9a7663192c9f…`. |
| `SelectAssets` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `9c39f110cdbe…`. |
| `SetCameraTransform` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `5109064cc096…`. |
| `SetContentBrowserPath` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `923b083c9bca…`. |
| `StartPIE` | Extend | Unknown | RuntimeInteraction | Never | Audited runtime/editor interaction; caller must opt in to runtime side effects. Constraints: runtime opt-in, schema `c4adc54ca52b…`. |
| `StopPIE` | Extend | Unknown | RuntimeInteraction | Never | Audited runtime/editor interaction; caller must opt in to runtime side effects. Constraints: runtime opt-in, schema `a2c799262a3c…`. |
| `WorldPosToScreenCoords` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `2d0eaec094be…`. |

### EditorToolset.LogsToolset

| Tool | 初始分类 | 风险 | 3.0 执行面 | 保存 | 约束与依据 |
|---|---|---|---|---|---|---|
| `GetLogCategories` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `0375780fbf4d…`. |
| `GetLogEntries` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `c087579d5fcd…`. |
| `GetVerbosity` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `c07911e51fdb…`. |
| `SetVerbosity` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `e3fa6da4f0e3…`. |

### GameFeaturesToolset.GameFeaturesToolset

| Tool | 初始分类 | 风险 | 3.0 执行面 | 保存 | 约束与依据 |
|---|---|---|---|---|---|---|
| `GetGameFeatureState` | Extend | Unknown | ReadOnly | Never | Exact UE 5.8.1 implementation was source-audited as inspection-only. Constraints: schema `1557486e4339…`. |
| `IsGameFeatureActive` | Extend | Unknown | ReadOnly | Never | Exact UE 5.8.1 implementation was source-audited as inspection-only. Constraints: schema `1557486e4339…`. |
| `IsGameFeaturePlugin` | Extend | Unknown | ReadOnly | Never | Exact UE 5.8.1 implementation was source-audited as inspection-only. Constraints: schema `1557486e4339…`. |
| `ListDiscoveredGameFeaturePlugins` | Extend | Unknown | ReadOnly | Never | Exact UE 5.8.1 implementation was source-audited as inspection-only. Constraints: schema `a2c799262a3c…`. |
| `ListEnabledGameFeaturePlugins` | Extend | Unknown | ReadOnly | Never | Exact UE 5.8.1 implementation was source-audited as inspection-only. Constraints: schema `a2c799262a3c…`. |
| `RequestActivateGameFeature` | Extend | Unknown | RuntimeInteraction | Never | Audited runtime/editor interaction; caller must opt in to runtime side effects. Constraints: runtime opt-in, schema `1557486e4339…`. |
| `RequestDeactivateGameFeature` | Extend | Unknown | RuntimeInteraction | Never | Audited runtime/editor interaction; caller must opt in to runtime side effects. Constraints: runtime opt-in, schema `1557486e4339…`. |

### GameplayTagsToolset.GameplayTagsToolset

| Tool | 初始分类 | 风险 | 3.0 执行面 | 保存 | 约束与依据 |
|---|---|---|---|---|---|---|
| `AddTag` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `eb596227bb7d…`. |
| `FindReferencersByTag` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `10ffc0729307…`. |
| `GetTagInfo` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `10ffc0729307…`. |
| `ListTags` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `5813e648e00e…`. |
| `RemoveTag` | Reject | Destructive | Rejected | Rejected | Destructive tools are never exposed by the generic official adapter. Constraints: schema `10ffc0729307…`. |
| `RenameTag` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `bd11ff9a237d…`. |

### GASToolsets.AbilitySystemInspectorToolset

| Tool | 初始分类 | 风险 | 3.0 执行面 | 保存 | 约束与依据 |
|---|---|---|---|---|---|---|
| `GetActiveEffects` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `b13e970e2b13…`. |
| `GetActiveTags` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `b13e970e2b13…`. |
| `GetAttributeValues` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `b13e970e2b13…`. |
| `GetGrantedAbilities` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `b13e970e2b13…`. |

### GASToolsets.AttributeSetToolset

| Tool | 初始分类 | 风险 | 3.0 执行面 | 保存 | 约束与依据 |
|---|---|---|---|---|---|---|
| `FindAttributeSetClasses` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `a2c799262a3c…`. |
| `ListAttributes` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `cf37a86786f8…`. |

### GASToolsets.GameplayCueToolset

| Tool | 初始分类 | 风险 | 3.0 执行面 | 保存 | 约束与依据 |
|---|---|---|---|---|---|---|
| `AddCueTag` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `0d5de8b6b478…`. |
| `CreateCueNotifyAsset` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `2074e443fd2d…`. |
| `ExecuteCueOnSelectedActor` | Extend | Unknown | RuntimeInteraction | Never | Audited runtime/editor interaction; caller must opt in to runtime side effects. Constraints: runtime opt-in, schema `15c1e3aa4256…`. |
| `FindCueNotifyAssets` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `5813e648e00e…`. |
| `FindCueTagsWithoutNotifies` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `a2c799262a3c…`. |
| `GetCueInfo` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `710519e43855…`. |
| `ListCues` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `5813e648e00e…`. |
| `RemoveCueTag` | Reject | Destructive | Rejected | Rejected | Destructive tools are never exposed by the generic official adapter. Constraints: schema `710519e43855…`. |

### NiagaraToolsets.NiagaraToolset_Assets

| Tool | 初始分类 | 风险 | 3.0 执行面 | 保存 | 约束与依据 |
|---|---|---|---|---|---|---|
| `FindNiagaraScripts` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `da4edba2b945…`. |
| `GetAssetDiscoveryInfo` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `a2c799262a3c…`. |
| `GetNiagaraScriptDigest` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `33d4463b72d0…`. |

### NiagaraToolsets.NiagaraToolset_Blueprint

| Tool | 初始分类 | 风险 | 3.0 执行面 | 保存 | 约束与依据 |
|---|---|---|---|---|---|---|
| `ConstructNiagaraBPWrapperFromComponent` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `718b94feb221…`. |
| `ConstructNiagaraBPWrapperFromSystem` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `ef55cb119e38…`. |

### NiagaraToolsets.NiagaraToolset_Component

| Tool | 初始分类 | 风险 | 3.0 执行面 | 保存 | 约束与依据 |
|---|---|---|---|---|---|---|
| `GetUserVariables` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `51fcb41efe92…`. |
| `GetVariable` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `8c7e8e191d12…`. |
| `SetSystem` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `edd6746f49b5…`. |
| `SetVariable` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `a7eb6d4c3656…`. |

### NiagaraToolsets.NiagaraToolset_Info

| Tool | 初始分类 | 风险 | 3.0 执行面 | 保存 | 约束与依据 |
|---|---|---|---|---|---|---|
| `UEnum_Info` | Extend | Unknown | ReadOnly | Never | Exact UE 5.8.1 implementation was source-audited as inspection-only. Constraints: schema `dfdefb527061…`. |

### NiagaraToolsets.NiagaraToolset_System

| Tool | 初始分类 | 风险 | 3.0 执行面 | 保存 | 约束与依据 |
|---|---|---|---|---|---|---|
| `AddEmitter` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `f9da5d0ecdee…`. |
| `AddModule` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `f7133cd160bb…`. |
| `AddRenderer` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `6ee8a509173f…`. |
| `AddSetParameterEntry` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `f008bf7d75db…`. |
| `AddSetParametersModule` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `7a0de68410ec…`. |
| `AddUserVariables` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `3101f9b8e6e2…`. |
| `ApplyStackIssueFix` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `11c18ab86639…`. |
| `CreateNiagaraSystem` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `633d4e865375…`. |
| `GetAvailableDynamicInputs` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `9f04d8e7da07…`. |
| `GetDataInterfaceSchema` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `59aec1351557…`. |
| `GetDynamicInputChain` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `ec9d24b179c2…`. |
| `GetDynamicInputSchema` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `84c6e5c8c26c…`. |
| `GetDynamicInputSchemaFromAsset` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `6ec628240579…`. |
| `GetEmitterData` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `86aff523c1db…`. |
| `GetEmitterInputValues` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `86aff523c1db…`. |
| `GetEmitterSchema` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `a2c799262a3c…`. |
| `GetEmitterSummary` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `86aff523c1db…`. |
| `GetEmitterTopology` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `86aff523c1db…`. |
| `GetModuleInputValues` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `157e81da61ad…`. |
| `GetModuleSchema` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `9e7e9715fc11…`. |
| `GetModuleSchemaFromAsset` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `09aa10ae2b2b…`. |
| `GetModuleTopology` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `157e81da61ad…`. |
| `GetRendererData` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `13dba7bf24ff…`. |
| `GetRendererSchema` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `6c1baae98f41…`. |
| `GetScriptStackInputValues` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `95f9f40a1482…`. |
| `GetScriptStackTopology` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `95f9f40a1482…`. |
| `GetStackInputData` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `ec9d24b179c2…`. |
| `GetStackInputSchema` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `24a659c87392…`. |
| `GetStackInputTopology` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `ec9d24b179c2…`. |
| `GetStackIssues` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `bac35ce65046…`. |
| `GetSystemCompileState` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `bac35ce65046…`. |
| `GetSystemData` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `bac35ce65046…`. |
| `GetSystemDependencies` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `bac35ce65046…`. |
| `GetSystemSchema` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `a2c799262a3c…`. |
| `GetSystemSummary` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `bac35ce65046…`. |
| `GetUserVariables` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `bac35ce65046…`. |
| `RemoveEmitter` | Reject | Destructive | Rejected | Rejected | Destructive tools are never exposed by the generic official adapter. Constraints: schema `fa6c69b160b2…`. |
| `RemoveModule` | Reject | Destructive | Rejected | Rejected | Destructive tools are never exposed by the generic official adapter. Constraints: schema `3f6c44a655f2…`. |
| `RemoveRenderer` | Reject | Destructive | Rejected | Rejected | Destructive tools are never exposed by the generic official adapter. Constraints: schema `d540182bb117…`. |
| `RemoveSetParameterEntry` | Reject | Destructive | Rejected | Rejected | Destructive tools are never exposed by the generic official adapter. Constraints: schema `5c5f377a035b…`. |
| `RemoveUserVariables` | Reject | Destructive | Rejected | Rejected | Destructive tools are never exposed by the generic official adapter. Constraints: schema `ccd859cc3e33…`. |
| `SetEmitterData` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `ca7cd0b9d2ef…`. |
| `SetModuleEnabled` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `d05167ca0a2e…`. |
| `SetRendererData` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `6ac414c74553…`. |
| `SetStackInputData` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `e3bcb986c8e8…`. |
| `SetSystemData` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `5c0f4659451e…`. |

### PCGToolset.PCGSpatialToolset

| Tool | 初始分类 | 风险 | 3.0 执行面 | 保存 | 约束与依据 |
|---|---|---|---|---|---|---|
| `RunPCGInstantGraph` | Extend | Unknown | RuntimeInteraction | Never | Audited runtime/editor interaction; caller must opt in to runtime side effects. Constraints: runtime opt-in, schema `8367611c48d9…`. |

### PCGToolset.PCGToolset

| Tool | 初始分类 | 风险 | 3.0 执行面 | 保存 | 约束与依据 |
|---|---|---|---|---|---|---|
| `AddCommentBox` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `f263c2cf9190…`. |
| `AddNode` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `3936b9e7b767…`. |
| `AddSubgraphNode` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `22b397833aea…`. |
| `ConnectNodePins` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `7aa89c997e64…`. |
| `CreateGraph` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `aa349e0d2fb6…`. |
| `DisconnectNodePins` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `7aa89c997e64…`. |
| `DrawSpline` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `9d2ac0ed0f83…`. |
| `ExecuteGraphInstance` | Extend | Unknown | RuntimeInteraction | Never | Audited runtime/editor interaction; caller must opt in to runtime side effects. Constraints: runtime opt-in, schema `a31f7e36adf4…`. |
| `GetGraphDescription` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `476f8f771c9c…`. |
| `GetGraphInstanceParams` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `a31f7e36adf4…`. |
| `GetGraphSchema` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `476f8f771c9c…`. |
| `GetGraphStructure` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `476f8f771c9c…`. |
| `GetNativeNodeSchema` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `a981d484f859…`. |
| `GetNodeDataView` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `6c0841fc43d1…`. |
| `GetNodeInfo` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `e12a4f16e846…`. |
| `ListAvailableSubgraphs` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `a2c799262a3c…`. |
| `ListGraphInstances` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `a2c799262a3c…`. |
| `ListNativeNodes` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `9addb92f2c92…`. |
| `RemoveCommentBox` | Reject | Destructive | Rejected | Rejected | Destructive tools are never exposed by the generic official adapter. Constraints: schema `05f1f13f6e39…`. |
| `RemoveGraphParams` | Reject | Destructive | Rejected | Rejected | Destructive tools are never exposed by the generic official adapter. Constraints: schema `cee1ac86836e…`. |
| `RemoveNode` | Reject | Destructive | Rejected | Rejected | Destructive tools are never exposed by the generic official adapter. Constraints: schema `a3b6a73d09c3…`. |
| `RepositionNode` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `5dea989b4bb0…`. |
| `ResetGraphInstanceParams` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `5d75450d0055…`. |
| `SetGraphDescription` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `c1764e6bb7d6…`. |
| `SetGraphInstanceParams` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `e288bb68a393…`. |
| `SetGraphParams` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `eb5a3b00e499…`. |
| `SetNodeComment` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `c173b092c3e0…`. |
| `SpawnGraphInstance` | Extend | Unknown | RuntimeInteraction | Never | Audited runtime/editor interaction; caller must opt in to runtime side effects. Constraints: runtime opt-in, schema `12df3da122e1…`. |
| `UpdateCommentBox` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `dafa6cda1093…`. |
| `UpdateNode` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `947b1d0bd83a…`. |

### PhysicsToolsets.PhysicsAssetToolset

| Tool | 初始分类 | 风险 | 3.0 执行面 | 保存 | 约束与依据 |
|---|---|---|---|---|---|---|
| `AddBody` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `50767e2493d4…`. |
| `AddConstraint` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `3323ea53aa2a…`. |
| `CreateFromMesh` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `780cd130b4a5…`. |
| `GetBodyMassScale` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `50767e2493d4…`. |
| `GetBodyNames` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `bd636a3ced47…`. |
| `GetBodyPhysicsMode` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `50767e2493d4…`. |
| `GetBodyShapes` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `50767e2493d4…`. |
| `GetConstraints` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `bd636a3ced47…`. |
| `RemoveBody` | Reject | Destructive | Rejected | Rejected | Destructive tools are never exposed by the generic official adapter. Constraints: schema `50767e2493d4…`. |
| `RemoveConstraint` | Reject | Destructive | Rejected | Rejected | Destructive tools are never exposed by the generic official adapter. Constraints: schema `3323ea53aa2a…`. |
| `RemoveShape` | Reject | Destructive | Rejected | Rejected | Destructive tools are never exposed by the generic official adapter. Constraints: schema `178817c5a5d5…`. |
| `SetBodyMassScale` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `6cc2b40289ab…`. |
| `SetBodyPhysicsMode` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `f574466a962f…`. |
| `SetBox` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `37b31a3c6d61…`. |
| `SetCapsule` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `6665a51449eb…`. |
| `SetConstraintLimits` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `7c7987b9ab46…`. |
| `SetSphere` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `a60ace487a87…`. |

### PluginToolset.PluginToolset

| Tool | 初始分类 | 风险 | 3.0 执行面 | 保存 | 约束与依据 |
|---|---|---|---|---|---|---|
| `AddPluginDependency` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `b13a5c69d7e9…`. |
| `CreatePlugin` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `50ade35c4196…`. |
| `GetPluginDependencies` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `1557486e4339…`. |
| `GetPluginDependents` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `1557486e4339…`. |
| `GetPluginDescriptor` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `1557486e4339…`. |
| `GetPluginForAsset` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `a0c3f824ae75…`. |
| `GetPluginInfo` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `1557486e4339…`. |
| `GetPluginTemplateDescriptions` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `a2c799262a3c…`. |
| `IsEnabled` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `1557486e4339…`. |
| `IsPluginCreationAllowed` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `a2c799262a3c…`. |
| `IsPluginModificationAllowed` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `a2c799262a3c…`. |
| `ListDiscoveredPlugins` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `a2c799262a3c…`. |
| `ListEnabledPlugins` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `a2c799262a3c…`. |
| `RemovePluginDependency` | Reject | Destructive | Rejected | Rejected | Destructive tools are never exposed by the generic official adapter. Constraints: schema `9f8581e04b1f…`. |
| `SetPluginEnabled` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `0e27f23000e7…`. |
| `UpdatePluginDescriptor` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `7bbbae53afac…`. |
| `ValidateNewPluginNameAndLocation` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `e0e5394c9e0e…`. |

### SemanticSearchToolset.SemanticSearchToolset

| Tool | 初始分类 | 风险 | 3.0 执行面 | 保存 | 约束与依据 |
|---|---|---|---|---|---|---|
| `FindSimilar` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `c5cf016cf788…`. |
| `Search` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `d8cdb2c9e9df…`. |

### SlateInspectorToolset.SlateInspectorToolset

| Tool | 初始分类 | 风险 | 3.0 执行面 | 保存 | 约束与依据 |
|---|---|---|---|---|---|---|
| `Click` | Extend | Unknown | RuntimeInteraction | Never | Audited runtime/editor interaction; caller must opt in to runtime side effects. Constraints: runtime opt-in, schema `68c6019addd4…`. |
| `Drag` | Extend | Unknown | RuntimeInteraction | Never | Audited runtime/editor interaction; caller must opt in to runtime side effects. Constraints: runtime opt-in, schema `9d487dd4f9fd…`. |
| `FillForm` | Extend | Unknown | RuntimeInteraction | Never | Audited runtime/editor interaction; caller must opt in to runtime side effects. Constraints: runtime opt-in, schema `74dbd182cddd…`. |
| `Hover` | Extend | Unknown | RuntimeInteraction | Never | Audited runtime/editor interaction; caller must opt in to runtime side effects. Constraints: runtime opt-in, schema `9bfc399a2510…`. |
| `ListObservers` | Extend | Unknown | ReadOnly | Never | Exact UE 5.8.1 implementation was source-audited as inspection-only. Constraints: schema `a2c799262a3c…`. |
| `Observe` | Extend | Unknown | RuntimeInteraction | Never | Audited runtime/editor interaction; caller must opt in to runtime side effects. Constraints: runtime opt-in, schema `93ee7cc6bd35…`. |
| `PressKey` | Extend | Unknown | RuntimeInteraction | Never | Audited runtime/editor interaction; caller must opt in to runtime side effects. Constraints: runtime opt-in, schema `038edcb516ef…`. |
| `Screenshot` | Extend | Unknown | RuntimeInteraction | Never | Audited runtime/editor interaction; caller must opt in to runtime side effects. Constraints: runtime opt-in, schema `9bfc399a2510…`. |
| `SelectOption` | Extend | Unknown | RuntimeInteraction | Never | Audited runtime/editor interaction; caller must opt in to runtime side effects. Constraints: runtime opt-in, schema `250feeea7f45…`. |
| `Snapshot` | Extend | Unknown | ReadOnly | Never | Exact UE 5.8.1 implementation was source-audited as inspection-only. Constraints: schema `1b7002ca0d7b…`. |
| `Type` | Extend | Unknown | RuntimeInteraction | Never | Audited runtime/editor interaction; caller must opt in to runtime side effects. Constraints: runtime opt-in, schema `4094cb039794…`. |
| `Unobserve` | Extend | Unknown | RuntimeInteraction | Never | Audited runtime/editor interaction; caller must opt in to runtime side effects. Constraints: runtime opt-in, schema `6d5ea0f29f09…`. |
| `WaitFor` | Extend | Unknown | RuntimeInteraction | Never | Audited runtime/editor interaction; caller must opt in to runtime side effects. Constraints: runtime opt-in, schema `d05ec77abb0d…`. |
| `Windows` | Extend | Unknown | ReadOnly | Never | Exact UE 5.8.1 implementation was source-audited as inspection-only. Constraints: schema `a1d020b21038…`. |

### state_tree_toolset.toolsets.state_tree.StateTreeTools

| Tool | 初始分类 | 风险 | 3.0 执行面 | 保存 | 约束与依据 |
|---|---|---|---|---|---|---|
| `get_children` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `327ac20f55e7…`. |
| `get_editor_data` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `a814ccf78094…`. |
| `get_enter_conditions` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `327ac20f55e7…`. |
| `get_evaluators` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `a814ccf78094…`. |
| `get_global_tasks` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `a814ccf78094…`. |
| `get_node_description` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `4024855ea372…`. |
| `get_root_states` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `a814ccf78094…`. |
| `get_tasks` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `327ac20f55e7…`. |
| `get_transitions` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `327ac20f55e7…`. |

### ToolsetRegistry.AgentSkillToolset

| Tool | 初始分类 | 风险 | 3.0 执行面 | 保存 | 约束与依据 |
|---|---|---|---|---|---|---|
| `CreateSkill` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `7a146d8afe37…`. |
| `GetSkills` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `8ffb333001e2…`. |
| `ListSkills` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `a2c799262a3c…`. |
| `UpdateSkill` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `5a7114d3bb6f…`. |

### UMGToolSet.UMGToolSet

| Tool | 初始分类 | 风险 | 3.0 执行面 | 保存 | 约束与依据 |
|---|---|---|---|---|---|---|
| `AddUIComponent` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `4f2c35862669…`. |
| `AddWidget` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `257b4b2675bf…`. |
| `BindToEventProperty` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `9d3f2eeb351d…`. |
| `CompileWidgetBlueprint` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `377c0b01c68c…`. |
| `CreateWidgetBlueprint` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `8b97ec07e861…`. |
| `GetNamedSlots` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `377c0b01c68c…`. |
| `GetWidgetClassInfo` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `3ee29fe2174e…`. |
| `GetWidgetDescription` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `ea88cabe6be8…`. |
| `GetWidgets` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `377c0b01c68c…`. |
| `GetWidgetTreeDepth` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `4a68614ba186…`. |
| `ListWidgetBlueprints` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `93ce90efc9af…`. |
| `ListWidgetClasses` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `0375780fbf4d…`. |
| `MoveUIComponent` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `2eb1f24f0b1f…`. |
| `MoveWidget` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `1088085ca560…`. |
| `RemoveUIComponent` | Reject | Destructive | Rejected | Rejected | Destructive tools are never exposed by the generic official adapter. Constraints: schema `4f2c35862669…`. |
| `RemoveWidget` | Reject | Destructive | Rejected | Rejected | Destructive tools are never exposed by the generic official adapter. Constraints: schema `e048a78a2944…`. |
| `RenameWidget` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `278025f5e63c…`. |
| `ReplaceWidgetWithChild` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `bcdc56616fb1…`. |
| `ReplaceWidgetWithNamedSlot` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `194baae50e75…`. |
| `ReplaceWidgetWithTemplate` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `e80cb9864c53…`. |
| `SetNamedSlotContent` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `1e340ee83ced…`. |
| `ToggleWidgetAsVariable` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `bf0c8fcaa5d1…`. |
| `WrapWidgets` | Extend | Unknown | TransactionalSync | Never | Audited non-destructive mutation; explicit targets, immediate completion, and ChangeSet rollback are required. Constraints: explicit targets, immediate completion, schema `80ffc7735974…`. |

### UnrealBridge

| Tool | 初始分类 | 风险 | 3.0 执行面 | 保存 | 约束与依据 |
|---|---|---|---|---|---|---|
| `Capabilities` | Reuse | ReadOnly | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `99334726611c…`. |
| `RegistryHash` | Reuse | ReadOnly | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `99334726611c…`. |

### WorldConditionsToolset.WorldConditionTools

| Tool | 初始分类 | 风险 | 3.0 执行面 | 保存 | 约束与依据 |
|---|---|---|---|---|---|---|
| `GetConditionDescription` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `1842c7ed4cda…`. |
| `GetQueryDescription` | Extend | Unknown | ReadOnly | Never | Audited query with no asset-save or destructive behavior. Constraints: schema `7eb97b831fd5…`. |
