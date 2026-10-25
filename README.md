# Paper Calendar

基于原桌面便签框架的 Qt 会议时间轴，布局与 `docs/ui-prototype.html` 保持一致。详细设计见 `docs/design.zh-CN.md`。

## 使用

- 滚轮平移时间段；Ctrl + 滚轮缩放；Shift + 滚轮上下滚动列表。
- 单击会议名称编辑，编辑框内可删除。
- 拖动左侧六点拖柄排序；选中行后 Alt + ↑ / ↓ 也可排序，顺序自动保存。
- 点击图例换色，节点、浅色标签与区间同步更新，重启保留。
- Unlock 时可拖动标题区域、拖动边缘缩放；Lock 锁定窗口位置和大小。
- 越界时点击“箭头＋今天日期”归位，× 退出。固定使用桌面伴随，不再提供模式选择。窗口背景为半透明灰色。

图例与会议数量合并在底栏。五项日期可直接输入、粘贴或用日历选择，留空表示尚未公布；不再提供“日期已确定”勾选。新增第五个“最终结果”日期；旧记录兼容保留，补填后显示节点。AoE 和时区转换不做改动。

摘要前不绘制人为准备区间。日期标签自动错层并用引线对应节点；悬停查看完整事件、日期及剩余天数；点击节点或日期标签展开详情卡片，点击外部或按 Esc 关闭。首次运行没有演示记录。

数据存于 Qt AppDataLocation 下的 conferences.json 和 settings.ini，Windows 通常为 `%APPDATA%/PaperCalendar/PaperCalendar/`。也可以指定便携目录：

```powershell
.\build-calendar\PaperCalendar.exe --data-dir .\my-data
```

## 构建与验证

需要 CMake 3.16+、C++17、Qt 5.15+ 或 Qt 6 Widgets。编译器须与 Qt ABI 匹配，Qt 与编译器的 bin 应加入 PATH。默认构建测试需要 Qt Test，可用 `-DBUILD_TESTING=OFF` 关闭。

```powershell
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH="你的 Qt 安装目录"
cmake --build build
ctest --test-dir build --output-on-failure
```

本机当前新版位于 build-calendar/desktop-fix（旧版运行中，避免覆盖），已附 Qt 和编译器运行库。测试使用构建目录下的临时数据目录，不访问真实会议数据；Qt 快照中的会议为演示记录。Windows 原生平台测试需以 `QT_QPA_PLATFORM=windows` 运行 CalendarTests；CTest 默认使用 offscreen。

桌面伴随使用前台事件、去抖、复核和不抢焦点的临时置顶。Win+D、多显示器、全屏应用、Explorer 重启尚待人工验收。原生边缘锁定逻辑面向 Windows。

旧 StickyNote、ToolBar 文件保留但退出构建。当前数据为日期精度、单实例编辑设计，不支持云同步或并发编辑。

## 本地恢复点与桌面后端研究

当前实现恢复标签：`paper-calendar-baseline-20261026`。已清理历史发布构建，保留 `build-calendar/desktop-fix`。

WorkerW 的本机只读检查、实际问题与实验建议见 `docs/workerw-research.zh-CN.md`。诊断源码为 `tools/desktop_probe.cpp`，本轮没有切换应用后端。

## 可选桌面挂载实验

桌面挂载实验在本机实际显示验证失败，已取消自动启用。两个 exe 普通双击均使用原来的后端；仅显式 `--desktop-host` 启用隔离数据的实验。当前挂载关系检查通过但真实显示失败，原有桌面伴随后端仍为默认。具体运行和已知验证范围见 `docs/workerw-experiment.zh-CN.md`。
