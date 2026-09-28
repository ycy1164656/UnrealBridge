# UE 5.8 可靠执行与上下文接口

适用于 UnrealBridge 3.1 的 World/Trace/Catalog 接口；3.0/UE 5.8.1 为历史验证基线。先核对精确项目、`ping ready=true`、health、PIE 和 Dirty，运行补丁以当前读回为准。技能文档不能扩大用户授权。以下工具均保留可追溯的身份、版本与输出边界。

## 接口选择

| 需求 | 接口 | 关键契约 |
|---|---|---|
| 多 World 定位 | `World.get_world_contexts`、`resolve_actor_reference`、`validate_actor_reference`；`bridge_submit_job(world_handle=...)` | Session 内 opaque handle；作用域覆盖 start/poll 每个执行片段；歧义拒绝；旧 World/Actor 不回退到首个对象 |
| Trace 摘要 | `Perf.start_trace_analysis`、`get_trace_analysis_status`、`get_trace_analysis_result`、`cancel_trace_analysis` | 解析与聚合在后台；取消请求与已取消终态不同；结果可显式 release |
| 目录发现 | `bridge_list_domains`、`bridge_search_tools`、`bridge_describe_tools` 等 | 按项目、endpoint、Editor session/revision 隔离；目录变化使旧 cursor 失效；stale 只供离线参考 |
| 项目上下文 | `bridge_project_context`、`bridge_project_impact` | 限定源码根、文件/字节/结果预算；精确行号、Hash、资产引用；词法证据不等于编译器调用图 |
| Blueprint 图 | `bridge_graph_export`、`bridge_graph_decode`、`bridge_graph_diff`、`bridge_graph_dry_run` | v2 原生 Pin 数据，v1 紧凑表；完整/切片区分；提案必须匹配新鲜 revision |
| 运行验收 | `bridge_runtime_run`、`bridge_runtime_status`、`bridge_runtime_cancel` | typed recipe、持久化 Scenario/报告/Artifact；只清理自有 PIE；不自动重放未知副作用 |

## World 与 PIE 身份

World/Actor handle 仅在对应 Editor session 和对象生命周期内有效。不要拼接 handle，不用 Actor label 假装全局唯一标识。`pie_session_id` 来自 BeginPIE/EndPIE，每次 PIE 不同；Client travel 会换 World，但不会因临时 `/Temp/Untitled` 就获得验收通过。

有多个 PIE World 时必须指定 World。单次 `bridge_submit_job(world_handle=...)` 自动在每个 start/poll 片段进入和退出作用域；无权保留作用域给下一任务。持续输入也绑定原 World/controller，World 销毁后失效。

## 后台 Trace

```python
from unreal_bridge import Perf
import json
started = json.loads(Perf.start_trace_analysis(
    utrace_path="C:/project/Saved/Profiling/sample.utrace",
    summary_kind="performance", max_file_size_mb=512))
```

支持 `performance`、`alloc`、`net`、`cook`。旧四个 `parse_*_summary` 同步入口返回迁移错误，不再在 GameThread 上等待解析。状态和结果查询需用返回的 analysis ID；最终 `get_trace_analysis_result(analysis_id=..., release=True)` 可释放，worker 尚在收尾时按错误提示稍后查询。Python `time.sleep` 只能放在 host 进程。

默认 512 MiB，上限 1 GiB；最多两个 worker，同文件并发拒绝，最多 16 个保留记录、15 分钟 TTL。结果预算 1 MiB；无法提供特定事件时显式 warning。取消是协作式，不能承诺所有 Provider 遍历都具有严格耗时上界。当前性能证据只有约 33–35 MiB 样本，不能推断 GB 级性能。

## 项目证据与语义候选

`target_paths` 可以是项目内源码文件/目录和 Unreal 资产路径。返回 `evidence`、`coverage`、`source_fingerprint`、`next_cursor`；变化的源码、资产采样或目录快照使旧 cursor 失效。`max_bytes` 按紧凑 UTF-8 JSON 计算，不包含传输层格式化开销。

源码最多扫描 4096 文件/32 MiB、单文件 1 MiB；排除 Content、构建缓存、链接/junction 越界以及敏感配置。每次最多采样四个资产和各 64 项依赖/引用，覆盖不足必须读 `coverage`。Blueprint 父类来自已生成类；没有为查询编译或保存资产。

`include_semantic` 默认 false。只在已配置的 embedding 与 caption 服务都指向 loopback 时调用官方语义搜索；外部或未知服务返回降级状态，不自动上传项目或开通付费服务。语义相似度仅是候选；pending 返回 Job ID，不代表后台已完成或已取消。

## 图与提案

原生 `SnapshotGraphJson` 保留既有 `nodes/wires`，增加 PinId/PersistentGuid、完整 PinType、连接、split pin、默认值、FText 表示及非 transient 节点属性文本。未知 JSON 字段在紧凑编码中保留。这是支持字段的往返契约，不能用作整个 `.uasset` 的导入/序列化替代。

默认导出完整 layout；`node_ids` 与 `hops` 可切片，`include_layout=False` 可省坐标。切片保留边界连接并声明 `complete=false`。输出超预算时拒绝，不能把截断图当完整图。原始与紧凑字节数是 JSON 测量，不是 token 测量。

`bridge_graph_dry_run` 重新读完整图并核对 `expected_revision`，仅接受已有 Pin 的保守 default/connection 提案。参数使用 Pin ID；映射旧 ApplyGraphOps 前拒绝重名、方向/类型冲突、替换连接和节点删除。它不会执行或保存；仍需原生 schema 验证和精确资产写授权才能实施。ApplyGraphOps 不是失败后自动全回滚的原子事务。

## 运行配方参考

只读基线示例：

```json
{"schema":"unrealbridge.runtime_recipe.v1","mode":"observe","steps":[{"id":"no-pie","type":"assert","field":"world_count","value":0}]}
```

Listen Server + 两个 Client 示例（通过 UE API 临时启动，需任务授权）：

```json
{"schema":"unrealbridge.runtime_recipe.v1","mode":"owned_pie","client_count":3,"timeout_seconds":120,"steps":[{"id":"peers","type":"assert","field":"world_count","value":3},{"id":"client-pawn","type":"wait","world":{"net_mode":"Client","pie_instance":1},"field":"pawn_available","value":true,"timeout_seconds":30}]}
```

`steps` 支持 `assert`、`wait`、`input`，最多 32 项。条件支持 `world_count`、`shader_jobs`、`asset_jobs`、`has_begun_play`、`controller_available`、`pawn_available`、`actor_count`。World 字段必须提供能唯一解析的 selector。`input` 仅在 owned_pie 中接受 `/ShooterRoyal/` 的明确 InputAction、三个 [-1,1] 分量，执行单帧 EnhancedInput pulse；不接受脚本、控制台命令或桌面输入。

owned_pie 拒绝已有 PIE/Dirty 基线；启动意图先落盘，目标地图、BeginPlay、World handle 连续稳定及三端角色符合预期后才运行断言。每个 Job 记录参数/脚本 Hash、ID、Session/World、实际终态。`cancel_requested`、等待超时和 Job terminal 不是同一状态。

无论成功或失败，finally 都只停止持有本次 lease 且 PIE session 完全匹配的运行，输出 Dirty/PIE/Shader/Asset 编译与日志片段，不保存资产。报告在项目 `Saved/UnrealBridge/Artifacts/runtime`，最终报告另有内容寻址 Artifact。

host 重启后原 running 记录展示 `needs_reconciliation`；不能假装后台线程仍活着，也不能盲重放。已知 Job 可查询确认，未知 dispatch 必须先核对。Editor 重启或用户重开 PIE 时，旧运行拒绝清理替代会话。这些断言证明运行路径与声明条件，不替代 gameplay、画面或手感验收。

## 目录与部署边界

live Toolset 必须与已审计的精确名称/schema Hash 匹配才能执行；发现新增工具不会自动授予写权限。schema 相同但描述变化不影响授权。目录缓存有 TTL 回读，文件 manifest/策略变更会刷新 host 快照。

原生函数变化仍需要构建及生成 manifest/wrapper。新增 Python MCP 工具需要重载对应 host 进程后才进入客户端工具清单；动态目录刷新不能替代进程重载。本次已验证 fresh host，不能由此声称当前客户端的旧常驻 MCP 进程自动加载了新增工具。
