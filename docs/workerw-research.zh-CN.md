# WorkerW / 桌面挂载研究

研究日期：2026-10-26。范围为当前 PaperCalendar（Qt 6.8.3 Widgets、半透明、可交互）。本次不修改应用的桌面后端，不重启 Explorer，不向 Progman 发送 0x052C，不重父化正在使用的窗口。

## 结论

WorkerW 方案有实际应用，也有已发生的兼容问题。它能减少依靠前台事件反复提升窗口的需求，但不能保证所有 Windows 版本无闪烁。对本项目而言，最需要验证的并非“能否 SetParent 成功”，而是 **Qt 半透明绘制、直接鼠标输入、弹出框、Explorer 重建** 能否同时正常。

推荐保留当前已提交的实现作为回退，另做可关闭的桌面挂载实验。交互组件优先研究桌面图标视图层的子窗口挂载，而不是直接照抄图标下方的壁纸宿主方案。这个推荐是根据应用需求作出的工程判断，尚未实测证明。

## 1. 本机只读观察

通过新增的 `tools/desktop_probe.cpp` 枚举桌面宿主，不读取其他应用窗口标题。诊断程序将自身设为 Per-Monitor V2 再读取坐标，避免 DPI 虚拟化。

| 项目 | 本机结果 |
| --- | --- |
| Windows | 11 25H2，build 26200.9711 |
| 桌面区域 | 2560 × 1600，DPI 144（150%） |
| Progman | 可见，扩展样式 0x200080，含 WS_EX_NOREDIRECTIONBITMAP |
| SHELLDLL_DefView | Progman 的可见子窗口，扩展样式 0x80000，含 WS_EX_LAYERED |
| SysListView32 | DefView 下的图标列表 |
| 顶层 WorkerW | 存在多个不可见窗口，不能任选一个作为宿主 |
| Progman 下的 WorkerW | 本次被动枚举没有发现；没有强制创建，因此不能据此认定系统不支持 |

这与较新的 raised-desktop 结构一致。不要只依据系统版本判断，更应检查窗口树和样式。本机结果仅证明当前这台电脑的当前状态，不能替代多屏、HDR、隐藏图标和其他系统版本测试。

运行方法（编译器 bin 在 PATH 中）：

```powershell
g++ -std=c++17 tools/desktop_probe.cpp -o build-calendar/desktop_probe.exe -ldwmapi -luser32
.\build-calendar\desktop_probe.exe
```

原始本次输出保存在被 Git 忽略的 `build-calendar/desktop-topology.txt`；句柄、PID 是会话数据，不写死进程序。

## 2. 已确认的问题与证据

### 2.1 旧的 WorkerW 查找方式有版本兼容问题

Lively 的实际问题记录显示，Windows 11 的桌面渲染改变后，旧挂载方法出现不可见和挂载失败。其维护者转述的 Microsoft 回复描述了 Progman 的 NOREDIRECTIONBITMAP 与 layered DefView、新的 child WorkerW 层级；这属于维护者发布的工程交流，不应当作稳定公开 API 合同。

来源：[Lively #2074，桌面结构与维护者讨论](https://github.com/lively-community/lively/issues/2074)。

Lively 2025-09 发布记录宣告了 24H2 兼容改进，说明这条路线能够实现，不能把早期问题直接解释成今天完全不可用。

来源：[Lively 发布记录](https://lively-community.github.io/lively/changelog/)。

固定到已查阅的源码提交 `d27589c821ed48248f2b9ff95e236400c20540a3` 可以看到：先检查样式区分桌面结构，新结构挂到 Progman 并调整与 DefView 的相对层级，旧结构使用 WorkerW；不是统一 `FindWindow("WorkerW")`。

来源：[WinDesktopCore.cs 固定提交](https://github.com/lively-community/lively/blob/d27589c821ed48248f2b9ff95e236400c20540a3/src/Lively/Lively/Core/WinDesktopCore.cs)。这不是对当前最新分支的断言。

### 2.2 半透明可以支持，但不能混用两套 Alpha 更新方式

Windows 8 起 layered child windows 是受支持的，因此“半透明窗口绝对不能作为子窗口”不成立。

来源：[微软 Extended Window Styles](https://learn.microsoft.com/en-us/windows/win32/winmsg/extended-window-styles)。

Qt 6.8.3 的 backing store 在无边框 Alpha 窗口路径调用 `UpdateLayeredWindowIndirect`。本项目正使用这一类 QWidget 渲染。

来源：[Qt 6.8.3 qwindowsbackingstore.cpp](https://github.com/qt/qtbase/blob/v6.8.3/src/plugins/platforms/windows/qwindowsbackingstore.cpp)。

微软说明：对同一 layered window 调用 `SetLayeredWindowAttributes` 后，后续 `UpdateLayeredWindow` 调用会失败，除非先清除再设置 layered 样式位。因此不能把某些壁纸示例中 `SetLayeredWindowAttributes(...,255,LWA_ALPHA)` 原封不动加到当前 Qt 窗口上。这里确认的是 API 混用约束；并没有通过本次实验证明 Qt 重父化必然黑屏。

来源：[微软 SetLayeredWindowAttributes](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setlayeredwindowattributes)。

### 2.3 壁纸输入方式不等于交互组件输入方式

图标下方的壁纸可以只负责显示，而本应用需要滚轮、Ctrl、拖动排序、名称编辑及菜单。Lively 的桌面核心有 RawInput 转发处理；维护者在另一个讨论中确认新挂载调整对部分应用出现输入和挂起问题。该讨论在 2026 年仍有用户反馈，但不能由此判断当前所有版本均存在相同故障。

来源：[Lively #3004 实际输入/挂起反馈](https://github.com/lively-community/lively/discussions/3004)。

有交互便签项目选择将窗口挂到 `SHELLDLL_DefView`，其代码展示了不同于壁纸层的宿主选择。这可作为实验对照，但项目规模较小，作者对其环境的判断不能推广为“所有系统只能这样挂”。

来源：[Sticky Note DesktopPin.cs](https://github.com/Samuel8946/sticky-note/blob/main/DesktopPin.cs)。

对本项目的推论：应先验证直接命中输入、Tab 焦点、中文输入法、拖放和弹出框；不要先引入全局鼠标转发，这会增加事件坐标、焦点与安全边界方面的复杂度。

### 2.4 DPI 和坐标是明确的接口风险

`SetParent` 不会自动改 WS_CHILD / WS_POPUP；跨 DPI awareness 模式的重父化可能产生异常或进程 DPI 重置。当前原生边缘命中和 QWindow::startSystemMove 仍按顶层窗口设计，重父化后必须重新验证，不能只改一个父句柄。

来源：[微软 SetParent](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setparent)。

不能用“DPI 数值相同”代替 awareness context 兼容检查，也不能把逻辑像素直接用于宿主客户区坐标。跨屏需使用正确的屏幕—客户区映射，同时检查 Qt 自身的 geometry 是否仍同步。这些是本项目需要补的实现工作，尚未验证完成。

### 2.5 Explorer 重建必须作为生命周期事件处理

Lively 核心监听 WorkerW 销毁、TaskbarCreated 和显示设置变化，表明其实现有专门的恢复流程。微软的窗口销毁语义也说明父/拥有者窗口销毁涉及子/被拥有窗口的销毁；跨进程重建的实际 Qt 行为仍需验证，不能只做一次 SetParent 就结束。

来源：[微软 DestroyWindow](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-destroywindow)。

实验后端应保留独立的隐藏顶层控制窗口来接收 Shell 广播，不能仅依赖被挂载的主窗口。message-only window 不接收广播，因此不能把它直接当作 TaskbarCreated 广播接收器。

来源：[微软 Window Features / Message-Only Windows](https://learn.microsoft.com/en-us/windows/win32/winmsg/window-features#message-only-windows)。

## 3. 值得做的实验形式

1. 保留默认的临时置顶后端；实验通过命令行或开发配置启用，不给普通用户增加模式选择。
2. 将日期模型与渲染内容同桌面宿主生命周期分离，不把唯一的 QObject 窗口和原生 HWND 生存期当成一回事。
3. 先在独立最小 Qt 窗口上比较 DefView 子窗口与新版 Progman 图标层上方的子窗口。不要移动或改样式到 Explorer 自己的 DefView / WorkerW 上。
4. 保留 Qt 的逐像素 Alpha 更新；不额外调用 SetLayeredWindowAttributes 操纵 Qt 的 HWND。
5. 编辑、颜色选择器和节点详情保持独立的应用顶层窗口，验证父属关系及焦点；不能让弹出框意外挂进背景层。
6. 有限重试重新枚举宿主；恢复失败回到当前后端。禁止无限发送 0x052C、无限重父化或重启 Explorer。

DefView 挂载是否优于 Progman 分层，必须以本项目的真实点击与半透明测试决定；目前只有实现方向和他人案例，不宣称已经验证可用。

## 4. 验收与停止条件

| 场景 | 必须通过 |
| --- | --- |
| 普通窗口 → 点击桌面，反复切换 | 不闪、不抢焦点，窗口留在普通应用下面 |
| Win+D / Win+M | 返回桌面后持续可见 |
| 节点、日期、滚轮、Ctrl、拖柄 | 输入准确，避免重复事件 |
| 编辑、颜色选择、节点卡片、中文输入法 | 正确激活与关闭，不藏到图标层下面 |
| Lock / 解锁 | 移动和缩放行为与现版一致 |
| 背景半透明 | 显示真实桌面内容，不是黑底或旧壁纸截图 |
| 100% / 150% / 200%，跨屏和负坐标 | 无位置跳变、缩放错误或错误命中 |
| 隐藏桌面图标、换壁纸、HDR | 组件不随图标消失；失败时可回退 |
| Explorer 重启 / 崩溃、虚拟桌面、休眠恢复 | 不丢数据，可重建宿主，不无限重试 |
| 同时运行其他壁纸软件 | 不改变它们的窗口层级，不破坏桌面图标 |

本次没有执行挂载、Win+D 自动化、Explorer 重启或黑屏复现，因此没有证据宣称 WorkerW 在本机已完全可用或完全不可用。结论是：**有值得尝试的技术基础，也有明确的实际故障记录；作为独立实验有价值，直接替换当前默认实现证据不足。**

## 5. 当前保存与清理记录

- 沿用原有 Git 历史，当前实现快照为 `5d1de2b`；标签 `paper-calendar-baseline-20261026`。
- 删除 `build-calendar/{updated,updated-v2,latest,start-today,compact-header,countdown}`、旧 `cmake-build-debug` 以及构建根目录的旧 exe，释放约 362.1 MiB。
- 保留当前 `build-calendar/desktop-fix` 和必要 CMake 构建缓存，不再保留多个历史发布目录。
- 从旧调试目录发现的 `Data/cfg.ini` 和 `Data/todo.txt` 已先备份到 `local-backups/before-build-cleanup-20261026/`，该目录不进入 Git。
- 真实 AppData 会议数据和根目录原有 Data 未改动。
