# 当前状态：桌面挂载显示验证失败，已回退

2026-10-26 实机反馈确认：原生 DefView 子窗口、Progman 同级子窗口、Qt 同步父子关系并重建 layered 表面的修订，都未在显示桌面后看到时间轴。原生父属关系、visible / cloaked、内部渲染和生命周期检查通过，不能证明桌面实际合成成功。

因此已取消 `PaperCalendarWorkerW.exe` 的自动挂载；普通双击也使用原有桌面伴随后端。代码保留在实验分支，仅显式加 `--desktop-host` 时启用，供后续诊断。不要把本报告下方的早期结构检查结果当成可用性验收。

已排除缺少 Windows 8/10 兼容性清单，未观察到 Qt 的 UpdateLayeredWindowIndirect 错误。具体不可见根因仍未确认，不能断言是某个单一 API 或版本造成。添加了日志、单实例锁和实验托盘恢复入口，但这些并未修复实机不可见。

# 早期实验记录

分支：`codex/workerw-experiment`。基线恢复标签：`paper-calendar-baseline-20261026`。

## 范围与运行

实验程序为 `build-calendar/workerw-experiment/PaperCalendarWorkerW.exe`；默认 `PaperCalendar.exe` 和已保留的 `build-calendar/desktop-fix/PaperCalendar.exe` 不自动启用该后端。

本机的新版 Shell 结构下，实验选择 `SHELLDLL_DefView` 作为应用 HWND 的原生父窗口，并将应用自己的 HWND 置于该宿主子窗口序列上方。它属于桌面子窗口挂载实验，不是旧式“顶层 WorkerW 作为壁纸宿主”的实现。没有重启 Explorer、没有发送 0x052C，也没有更改 Explorer 自己的样式或 Alpha API。

直接打开实验 exe 使用独立的应用数据目录：组织名仍为 PaperCalendar，应用名为 PaperCalendarWorkerW。因此不会读取或覆盖正式应用的会议和偏好。带 `--data-dir` 可以进一步指定项目内的隔离目录。

```powershell
.\build-calendar\workerw-experiment\PaperCalendarWorkerW.exe --desktop-host --data-dir .\build-calendar\workerw-interactive-data --experiment-demo
```

`--experiment-demo` 只在内存里生成三条演示会议，不自动保存；编辑保存时写入所指定的隔离数据目录。标题显示“实验”，底栏标记演示会议。真实记录不会被复制进实验目录。

## 实现边界

- `DesktopAttachment` 检查父子 DPI awareness context 相同后才调用 SetParent；不一致则拒绝挂载，保留默认后端。
- 保留 Qt 自己的 layered / 逐像素 Alpha 绘制，不调用 SetLayeredWindowAttributes。
- 挂载期间通过 `desktopAttached` 属性停用临时置顶控制，避免两套后端相互争夺层级。
- 独立隐藏顶层接收窗口监听 TaskbarCreated，定时验证宿主、PID 和父属关系；重试最多三次。
- 宿主丢失时允许重建应用原生窗口并重新挂载。实验事件循环不会因原生主窗口短暂丢失而自动退出；用户通过关闭按钮或正常关闭事件退出。
- 编辑、颜色选择和节点详情使用独立顶层弹出窗口，避免将输入弹窗直接放入桌面背景层。
- 正常结束前脱离宿主，恢复应用自己的原始窗口样式。未通过验证时应使用基线程序。

## 本机已经确认

系统：Windows 11 25H2，build 26200.9711，150% DPI。普通桌面环境原生检查结果：

| 检查 | 结果 |
| --- | --- |
| 应用父窗口为 SHELLDLL_DefView | 通过 |
| WS_CHILD 且保留 WS_EX_LAYERED | 通过 |
| 父子 DPI context 兼容 | 通过 |
| 挂载后原生位置和大小 | 与挂载前一致 |
| Qt 内部渲染 Alpha | 背景 Alpha 为 128 |
| 从程序调用打开并关闭编辑框 | 通过 |
| 编辑框关闭后仍是桌面子窗口 | 通过 |
| 主动脱离并恢复顶层窗口 | 通过 |
| 主动重建应用 HWND 后再次挂载 | 通过 |
| 原有 Windows 回归检查 | 18 项通过 |

可重复的隔离检查：

```powershell
.\build-calendar\workerw-experiment\PaperCalendarWorkerW.exe --desktop-host --data-dir .\build-calendar\workerw-check-data --experiment-check --report .\build-calendar\workerw-check.json --snapshot .\build-calendar\workerw-check.png
```

`--experiment-check` 自动结束，不会保存演示会议。报告与截图位于 Git 忽略的构建目录。截图是 Qt 自身的渲染结果，不是桌面合成正确的证明。编辑框检查是程序调用，不是实际鼠标命中的证明。

沙箱下的 SetParent 返回 ERROR_ACCESS_DENIED（5），程序保持默认后端；解除执行沙箱的普通桌面环境中挂载成功。这项结果不能解释成程序要求管理员权限；用户从正常 Explorer 环境启动并不等同于工具沙箱进程。

## 仍需真实操作验证

1. 退出旧 PaperCalendar 后端实例，避免旧钩子继续产生闪烁；实验程序可与其他普通应用一起运行。
2. 显示桌面，查看演示时间轴是否可见且背景确实透出壁纸。
3. 点击会议名称、节点和图例，滚动时间轴，拖动会议排序；验证弹窗及中文输入。
4. 在其他应用与桌面之间切换，比较是否还闪；再测试 Win+D / Win+M。
5. 测试 Unlock 下的标题移动、边缘缩放，以及 Lock 下禁止移动/缩放。
6. 后续再测试混合 DPI 多屏、虚拟桌面、隐藏桌面图标、壁纸切换和 Explorer 重启。

本次没有自动执行 Win+D 或重启 Explorer。Windows 窗口自动化工具的应用清单不暴露已挂载的子窗口，不能据此生成可信的真实点击结论。当前只能称为“原生挂载与生命周期检查通过的实验版本”，不能称为桌面交互完整验收通过。
