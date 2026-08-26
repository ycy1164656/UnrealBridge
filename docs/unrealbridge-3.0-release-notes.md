# UnrealBridge 3.0 发布说明

> 版本：`3.0.0`
> 协议：`2`（保持线协议兼容）
> 发布日期：`2026-08-26`
> 主要验证目标：Unreal Engine `5.8.1`

## 发布结论

UnrealBridge 3.0 已完成 UE 5.8.1 兼容升级，并接入官方 Experimental
`ToolsetRegistry`。官方能力作为可发现、可审计的第二 provider 存在，不替换
UnrealBridge 已有的 TCP/HTTP MCP、durable Job、ChangeSet、鉴权、幂等和断线恢复层。

## 主要变化

- 插件版本从 `2.0.0` 升为 `3.0.0`，Protocol 保持 `2`。
- 新增 UE 5.8 条件编译适配层 `UnrealBridgeUE58Library`；UE 5.3–5.7 不引用
  `ToolsetRegistry` module 或 Experimental headers。
- capabilities 增加官方 registry 是否编译、是否运行可用、provider、执行模型、
  安全策略、保存行为和取消语义。
- HTTP MCP 与 Codex grouped adapter 新增：
  - `bridge_list_official_toolsets`
  - `bridge_describe_official_toolset`
  - `bridge_submit_official_toolset_job`
- 官方调用被包装为 durable polling Job，支持统一查询和断线后的结果恢复。
- manifest/wrapper 元数据新增 `provider` 与 `engine_min`；UE 5.8.1 实测生成
  35 个 library、1185 个 function、5 个 enum。
- MCP Python 依赖固定为 `mcp>=1.6.0,<2`，避免 MCP 2.x 的 FastMCP import
  surface 变化导致启动失败。
- preflight 修复 Python 3.14 下 `vswhere` 输出解码问题。

## 官方 Toolset 安全边界

UE 5.8.1 运行态审计得到 53 个 Toolset、832 个 Tool：

| 分类 | 数量 | 3.0 行为 |
|---|---:|---|
| Reuse | 2 | 允许；均为 UnrealBridge 自有只读 meta tool |
| Wrap | 0 | 暂无满足 typed wrapper 条件的官方工具 |
| Extend | 776 | 阻止，等待明确副作用审计或 typed wrapper |
| Reject | 54 | 拒绝潜在破坏性工具 |

通用执行器只接受 schema 明确声明 `annotations.readOnlyHint=true` 且没有
destructive hint 的工具。调用者不能通过提交自定义 risk 字段绕过判断；实际
Tool schema 是唯一授权来源。通用官方调用永不保存 package。

完整审计见
[`ue58-toolset-capability-matrix.md`](ue58-toolset-capability-matrix.md)。

## 验证记录

- UE 5.8.1 clean `BuildPlugin`：通过。
- `C:\dev\ShooterRoyal_5_8_DirectUpgrade` 的
  `LyraEditor Win64 Development`：通过。
- UE Automation `UnrealBridge.*`：5/5 通过。
- Python tests：15/15 通过。
- HTTP MCP protocol tests：7/7 通过，包括只读 Toolset Job 与未标注工具拒绝。
- 60 秒 polling soak：100/100 health；幂等与断线恢复通过；平均延迟
  `69.02 ms`，p95 `86.85 ms`。
- 无保存 ChangeSet smoke：成功回滚；`saved=false`；最终无 Dirty Package。
- 实测 manifest hash：`d875bc64f49145b4bfffa2ad2dff8c4a2e3cb6ae5a9ce816d8686593f83e0945`。
- 实测 registry hash：`f5239640c6d79dc425a9797d75b28310`。

## 安装与迁移

1. 将 `Plugin/UnrealBridge` 同步至目标项目的 `Plugins/UnrealBridge`。
2. 正常构建 Editor Target。使用 UE 5.8 官方 Toolsets 时，构建期需要启用
   `AllToolsets+ModelContextProtocol`，运行期启用
   `AllToolsets,ModelContextProtocol`；无需修改 `.uproject`。
3. 将 `.codex/skills/unreal-bridge` 安装到
   `%USERPROFILE%\.codex\skills\unreal-bridge`。
4. Codex 的 `~/.codex/config.toml` 中将 MCP adapter 固定到
   `mcp>=1.6.0,<2`，然后重启 Codex 使 MCP server 配置重新加载。

## 已知限制

- UE 5.8.1 编译仍报告 StructUtils、Sequencer、Networking、PoseSearch、PCG、
  Material 等引擎 API 的 deprecation warnings；当前不阻塞 5.8.1 构建，但需要在
  后续引擎升级前逐项迁移。
- 本机 UE 5.6.1 BuildPlugin 触发 UBT 内部
  `ModuleRules.IsValidForTarget` `ArgumentNullException`，且可在未修改的 2.0 快照
  复现。3.0 本次不把该引擎列为重新验证通过。
- Epic shipping Toolsets 普遍缺少 read-only/destructive annotations，因此除两个
  UnrealBridge 自有 meta tool 外，3.0 不开放通用执行。后续接入必须采用 typed
  wrapper，并明确事务、幂等、取消与保存语义。
