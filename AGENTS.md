# Codex 全局规则（仓库级）

本文件是 **Codex CLI** 在本仓库的规则入口（参考你本机 `E:\lxh\.claude\CLAUDE.md` 的思路，但允许未来与 Claude 规则分叉）。

## 1) 语言与沟通

- 默认使用中文回复。
- 变更前先澄清目标/验收标准；不确定就先问。

## 2) bd（Beads）任务流（必用）

本机存在多个 `bd`，npm shim 可能失效；本仓库统一使用 `C:\Program Files\bd\bd.exe`。

```powershell
& 'C:\Program Files\bd\bd.exe' ready
& 'C:\Program Files\bd\bd.exe' show <id>
& 'C:\Program Files\bd\bd.exe' update <id> --status in_progress
& 'C:\Program Files\bd\bd.exe' close <id> --reason "Done"
& 'C:\Program Files\bd\bd.exe' sync
```

规则：
- 开始动手前：创建/认领一个 issue，并置为 `in_progress`。
- 过程中发现后续工作：立刻建 issue（不要只写在聊天里）。
- 避免交互式命令（例如会弹编辑器的）。

## 3) MCU / Keil（MDK-ARM）工程规则（适用时必遵守）

- 源码与注释：优先中文；文件编码用 UTF-8。
- 新增/移动 BSP 等源文件后：必须同步更新 Keil 工程（`.uvprojx`）里的 Group、源文件、Include Path。
- 修改后必须编译验证，且要求 **0 错误 / 0 警告**：

```powershell
& 'C:\Keil_v5\UV4\UV4.exe' -b <工程文件路径> -o build_log.txt -j0
```

## 4) 代码“简化/重构”（能替代就替代，做不到就先不用）

交付前的目标是：可读、可维护、最小必要复杂度。

- 优先替代方案：小步重构 + 删除重复/死代码 + 命名/结构统一 +（若仓库已有配置）运行格式化/静态检查。
- 如果没有可靠的自动化工具或会引入大范围无意义 diff：**跳过“简化”步骤**，以正确性和可验证为先。

## 5) 结束会话：必须提交并推送到同一个 GitHub 仓库

**Work is NOT complete until `git push` succeeds.**

1. 为未完成/后续工作建 issue（bd）
2. 跑质量门禁（与本次改动相关的测试/编译/构建）
3. 更新 issue 状态（完成的 close，进行中的更新）
4. 推送到远端（同一个仓库 / 同一个 `origin`）：

```powershell
git pull --rebase
& 'C:\Program Files\bd\bd.exe' sync
git push
git status --ignore-submodules=dirty  # 必须显示 up to date 且无待提交改动
```

5. 清理：确认没有遗留 stash/临时分支（如必须保留，需建 issue 说明原因）
