<p align="right">
  <strong>简体中文</strong> · <a href="workspace.md">English</a>
</p>

# AI Passport 工作区约定

> 本文随 `template/app-base` 模板分支分发，是**新会话的第一份上下文**。
> 工作区根目录的 `WORKSPACE.md` 是同一份内容的入口副本。
> 目的：多个独立应用共用一个已装好的 ESP-IDF 环境和一套板级驱动，不重复安装、不重复造轮子。

## 1. 环境（工作区之外，机器级）

| 项 | 值 |
| --- | --- |
| ESP-IDF | `C:\esp\esp-idf-v5.5.3` |
| 工具安装目录 | `C:\esp\.espressif`（**必须**设置，不要用默认的 `%USERPROFILE%\.espressif`） |
| 工具链 | riscv32-esp-elf 14.2.0、cmake、ninja、ccache 均已就位 |
| 设备 | ESP32-C3，8 MB Flash，USB 枚举为 COM 口（`VID_303A:1001`） |

每次新终端激活：

```powershell
$env:IDF_TOOLS_PATH='C:\esp\.espressif'
& 'C:\esp\esp-idf-v5.5.3\export.ps1'
python "$env:IDF_PATH\tools\idf.py" --version   # 应输出 ESP-IDF v5.5.3
```

## 2. 目录结构

```text
C:\Dev\AI-Passport\
  WORKSPACE.md              ← 工作区入口副本（指向本文）
  ai-passport\              ← 主仓库（git 主工作树）
  apps\
    <应用名>\               ← 每个应用一个 git worktree（独立 build/ 与 sdkconfig）
  tools\
    new_app.ps1             ← 新应用脚手架
```

## 3. 分支与工作树模型

- `main`：与上游同步的基线，不放业务改动；
- `template/app-base`：**应用模板分支**——只含 BSP、深睡模块、UI 基础件、字体流水线和一个最小主屏；
- `feature/<应用名>`：每个应用一个分支，配套 `apps/<应用名>` 工作树；
- 需要留档时打 tag：`app/<应用名>/v1`。

不要复制 `build/` 与 `sdkconfig`，让 IDF 在工作树里重新生成。

## 4. 新应用流程

```powershell
.\tools\new_app.ps1 -Name my-app
# 等价于 git worktree add apps\my-app -b feature/my-app template/app-base
# 并写入 apps\my-app\README.md 骨架
```

开发、构建、烧录：

```powershell
cd apps\my-app
idf.py set-target esp32c3       # 第一次
idf.py build
idf.py -p COMx flash monitor    # 或先 merge-bin 再整片烧写
```

交付门禁：

```powershell
.\tools\validate.sh --firmware  # 构建 + 合并镜像布局校验
.\tools\validate.sh --static    # 仓库检查 + 宿主测试（缺主机编译器时跳过）
```

## 5. 可直接复用的资产

| 资产 | 位置 | 说明 |
| --- | --- | --- |
| 板级驱动 BSP | `components/bsp/` | 屏幕/按键/音频/电量/I2C，已真机验证 |
| 深睡模块 | `main/power.c`、`power_state.c` | 定时/按键唤醒、RTC 快照、时长测量 |
| UI 基础件 | `main/pq_ui.c` | 配色、页面（不透明底）、文字、选中行 |
| 中文字体 | `assets/fonts/` + `main/pq_fonts.h` | 16/20 px 子集，生成器可复现 |
| 字体流水线 | `tools/gen_font.py`、`font_charset.txt` | 改文案后重新生成，带缺字门禁 |
| 硬件事实 | `components/bsp/include/bsp_pins.h` | 引脚、电压窗口、时序的单一事实来源 |

## 6. 每个应用必须自己决定的事

1. **中文文案**：改 `main/ui_strings.h` → 重算 `tools/font_charset.txt` → 重新生成字体；
2. **分区表**：模板默认 app 4 MB + storage 4 MB（SPIFFS），联网/存文件时按需调整；
3. **NVS / SPIFFS 数据**：设备级共享，不同应用可能看到彼此的旧数据；
4. **烧录**：一个应用一个 ROM，切换应用 = 重新烧录；整片刷合并镜像会重置 NVS。

## 7. 新会话怎么开

**新建应用**（还没有 `apps/<名字>/`）：

```text
工作区在 C:\Dev\AI-Passport。先读 WORKSPACE.md。
我想新建应用 <名字>：<一句话需求>
```

流程：读 `WORKSPACE.md` → 讨论需求与验收 → `tools/new_app.ps1` 建分支/worktree/README
→ 把目标写进 README → 开发。**README 由脚手架生成，不需要事先存在。**

**继续已有应用**：

```text
工作区在 C:\Dev\AI-Passport。先读 WORKSPACE.md 和 apps\<应用名>\README.md，
按里面的约定继续开发 <应用名>：<这次要做什么>
```

**不建议 fork 长会话**：会继承大量无关历史，既贵又容易带偏。需要延续某个决策时，
写进 `apps/<应用名>/README.md` 或本目录，而不是靠会话记忆。

## 8. `apps/<应用名>/README.md` 的生命周期

| 时机 | 由谁 | 写什么 |
| --- | --- | --- |
| 脚手架创建应用时 | `new_app.ps1` | 目标、当前状态、下一步的骨架 |
| 需求/验收确认后 | 开发者或 AI | 一句话目标、明确的完成标准 |
| 每个里程碑结束 | 开发者或 AI | 勾掉已完成、更新“下一步” |
| 遇到卡点或换会话前 | 开发者或 AI | 卡在哪、试过什么、下一个假设 |

原则：README 是给“下一个会话/AI”看的交接件，不是项目介绍书。

## 9. 规范说法对照

| 通俗说法 | 规范术语 |
| --- | --- |
| 一个仓库放多个独立应用 | monorepo（单仓库多应用） |
| 每个应用一个分支 + 独立目录 | branch-per-application + git worktree（并行工作树） |
| 从模板开始新应用 | project template / scaffolding；工程文化里叫 golden path（铺好的路） |
| 板级驱动与业务分离 | BSP（Board Support Package）+ application layer，即 platform/application 分层 |
| 共用已装好的工具链 | pre-provisioned / shared toolchain |
| 每个应用一个固件 | product variants / build flavors |
| 提交前必须跑检查 | validation gate / CI pipeline |
| 短分支、从主干拉 | trunk-based development |
