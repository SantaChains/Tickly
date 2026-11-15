# Tickly

#### 一个极简，极快的GTD任务管理应用，也可一键添加任意界面选中的文本至应用保存，用作临时记事本

![](image/1.png)

![](image/2.png)

![](image/3.png)

## 快捷键

| 操作 | 快捷键 |
| --- | --- |
| 显示主窗口（全局） | `Alt+Q`（设置里可改） |
| 划词添加任务（全局） | `Alt+1`（设置里可改） |
| 搜索过滤 | `Ctrl+F` 唤出，`Esc` 退出并清空 |
| 撤销最近一次删除 | `Ctrl+Z`（保留 20 层：单条 / 批量 / 清除已完成） |
| 完成或取消完成所选 | `空格` |
| 删除所选 | `Delete` |
| 全选 | `Ctrl+A` |

重复启动不会产生第二个进程：已运行的实例会被还原并置前；`--autostart` 那一路遇到已有实例则静默退出。

## 构建

需要 CMake ≥ 3.20 + Ninja + Visual Studio 2022（C++20）。只维护 MSVC 一条工具链，
产物 `build-msvc/Tickly.exe`：单文件、`/MT` 静态链接运行库。

```bash
# 在 VS 开发者环境里配置一次，之后只增量构建
cmake -S . -B build-msvc -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-msvc
```

PowerShell 里没有开发者环境时，先导入再构建：

```powershell
Import-Module '<VS>\Common7\Tools\Microsoft.VisualStudio.DevShell.dll'
Enter-VsDevShell -VsInstallPath '<VS>' -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'
cmake --build build-msvc
```

工具链取舍：

| 工具链 | 定位 | 理由 |
| --- | --- | --- |
| **MSVC + Ninja** | 唯一 | 产物最小（451 KB vs clang-cl 499 KB、MinGW 1688 KB）；`/MT` 静态 CRT 兼容性最好；调试/PDB 最稳 |
| clang-cl + lld-link | 已停用 | 诊断更细，但与 cl 混用 PDB/CRT 有坑，维护成本大于收益 |
| mingw-w64 | 已停用 | 产物 3 倍大，清单/代码页需要额外的 specs 与 `--codepage` 才能对齐 |

Ninja 本身推荐：增量构建远快于 Make/MSBuild。

资源必须参与链接：

| 资源 | 位置 | 缺失后果 |
| --- | --- | --- |
| 图标（数值 ID **1**）、版本信息 | `assets/resource.rc` | 白板图标 / 文件属性没有版本号 |
| DPI 感知清单 | MSVC：`assets/app.manifest`（link 的 `/MANIFESTINPUT`）<br>mingw：`assets/manifest.rc` | 非 100% 缩放下整窗位图拉伸，文字发虚 |

清单为什么分两条路：MSVC 下 CMake 会自动 `/MANIFEST:EMBED`，若 `.rc` 里再放一份
`RT_MANIFEST` 就报 `CVT1100 资源重复`；mingw 的 `ld` 又会报
`multiple non-default manifests`（默认清单来自 gcc 的 `default-manifest.o`，
由 `cmake/mingw64.specs` 去掉）。

## 目录结构

```
src/
  main.cpp            入口：DPI、单实例、注册窗口类、消息循环（组合根）
  common.h            公共包含与编译期开关

  core/               纯逻辑层（不引用任何 ui/ 代码）
    task.h              Task 数据模型
    text_util.*         宽窄字符串、空白整理、多行转义
    time_util.*         epoch/本地时间、周月边界、日期文案
    date_parse.*        「明天下午三点」这类自然语言解析

  ui/                 界面层
    context.*           跨模块共享的界面上下文：窗口/控件句柄、字体、DPI、布局矩形、悬停态
    theme.*             明暗调色板、字体设置、主题切换
    gdi_util.*          DPI 缩放、字体创建、画刷/画笔缓存
    dwm_shell.*         Win11 圆角 / 深色标题栏 / 边框色
    dark_mode.*         系统控件深色接入（uxtheme 逆序导出，菜单/编辑框/日期选择器）
    main_window.*       布局、标题区绘制、窗口过程
    list_view.*         自绘列表：行几何、平滑滚动、命中测试、交互、折行缓存
    list_paint.cpp      列表绘制（任务行 / 分组标题 / 汇总行）
    row_editor.*        行内编辑与自绘蓝色光标
    input_box.*         输入框子类化、占位文案、光标居中
    tray.*              托盘图标与菜单
    toast.*             划词结果气泡
    capture.*           划词添加任务
    autostart.*         开机自启（HKCU Run）
    hotkeys.*           全局快捷键
    reminder.*          到点提醒
    dialogs_*.         设置 / 日期 / 内存对话框模板

  app/                应用编排层（连接 core、ui 与持久化）
    ids.h               控件 / 菜单 / 自定义消息 ID
    version.h           版本号（唯一来源）
    rows.*              行模型：分组归属、展开状态、显示行重建
    actions.*           任务用例：新增/完成/删除/批量/清空/导出
    store.*             tasks.txt 读写（任务 + 全部设置项）

  platform/
    dpi.*               进程 DPI 感知（清单失效时的运行期兜底）

assets/               图标 app.ico、清单 app.manifest、资源脚本 resource.rc / manifest.rc
cmake/                mingw 链接 specs（去掉默认清单，避免与 app.manifest 冲突）
tools/                图标生成（make-icon.ps1）与变更记录归档（archive-changelog.ps1）
```

分层约定：

- `core/` 只做纯逻辑，`grep '#include "ui/' src/core` 应为空；
- 跨模块只通过头文件里的声明交互，模块私有状态/函数保持 `static`；
- `app/` 是编排层：负责把领域数据算成显示行（`rows`）、把用例落到数据（`actions`）、
  把状态写盘（`store`）。消息分发（`ui/main_window.cpp`）和入口（`main.cpp`）
  会向上调用 `app/`，属于 Win32 单窗口程序的正常交叉，不再强行拆一层控制器。

## 版本号

唯一来源是 `src/app/version.h` 的 `TICKLY_VERSION`。改版本时同步：

1. `src/app/version.h`
2. `assets/resource.rc` 的 `FILEVERSION` / `PRODUCTVERSION`
3. `assets/app.manifest` 的 `assemblyIdentity version`

CMake 在配置阶段校验三者一致，不一致直接失败；CI 也会重新校验图标 ID 与
内嵌清单（用 `LoadIcon(1)` 真实取一次图标）。

发版后把新版本的 `## vX.Y.Z` 段落写在 `CHANGELOG.md` 顶部，再运行
`powershell -File tools/archive-changelog.ps1`：旧版本按原顺序移入
`CHANGELOG.old`，`CHANGELOG.md` 始终只留最新一段。

## 数据文件（v4）

数据文件是 exe 同目录的 `tasks.txt`（UTF-8，带 BOM），文本格式、一行一条记录：

```
V	4                                        格式版本（v4 起；旧文件没有本行）
E	1                                        兼容旧版：文本已转义
S	b	b	b	b	b	b	b	b	b	b	b              分组展开 + 已完成区收纳（11 个布尔）
C	b	min	b	mod	vk	mod	vk                 提醒 / 托盘 / 两个全局快捷键
W	max	left	top	right	bottom              窗口位置
F	字号	R	G	B ...                         字体与主题（追加式扩展）
T	done	doneAt	due	hasTime	notified	text   任务（text 末尾，可含转义换行）
```

- **版本行 `V`**：写入端恒定输出；读取端没有也不报错（旧文件按 v1~v3 规则解析）。
- **兼容性**：新版本只追加字段/未知行会被忽略，所以旧版本打开新文件也不会崩；
  实测旧版二进制能正常读出 v4 文件里的全部任务。
- **转义**：`\` → 反斜杠，`
` → 换行，`	` → 空格，`` 丢弃。
- **原子写入**：先写 `tasks.txt.tmp` → 旧文件备份为 `tasks.txt.bak` →
  `MoveFileEx(REPLACE_EXISTING|WRITE_THROUGH)` 替换。断电/崩溃不会留下半截文件。
- **首次运行的数据来源**：exe 同目录没有数据时，依次尝试 当前目录 →
  `%APPDATA%Tickly	asks.txt` → `%APPDATA%TodoWin	asks.txt`，找到就复制过来
  （复制而非移动，源文件保留）。exe 同目录不可写时退回 `%APPDATA%Tickly`。

## 深色模式

- 应用层：自有绘制全部走 `COL_*` 主题色（`ui/theme.cpp` 的两套色板）。
- 系统层：`ui/dark_mode.cpp` 动态取 uxtheme 逆序导出 104/135/136，
  设置进程首选外观、刷新菜单主题，并给每个对话框套 DWM 深色标题栏 +
  给 Edit / ComboBox / HotKey（`DarkMode_CFD`）、DateTimePicker / ScrollBar
  （`DarkMode_Explorer`）换主题名。取不到这些导出（Win7/8）时静默降级。
- 快捷键采集框（`msctls_hotkey32`）不接受深色主题，改为只接管
  `WM_PAINT`/`WM_ERASEBKGND` 自绘，键盘采集仍由原控件处理。

## 性能

启动到「窗口可交互」（`MainWindowHandle` 非空且 `Responding`）。测量脚本会等上一个
实例**完全退出**再计时（否则单实例互斥体会让第二实例直接退出，被误判成快），
同一台机器、同一份数据、每个配置 **n=3**：

| 数据 | 重构前单文件版 | 当前 | 变化 |
| --- | --- | --- | --- |
| 5000 条任务 | 1785 ms（1671–1883） | **935 ms**（902–1002） | −48% |
| 空列表 | 229 ms（224–235） | 232 ms（232–233） | 持平 |

做法：

- 行高/宽度测量改用一个进程内内存 DC（原来每行 `GetDC`/`ReleaseDC` 各一次，
  5000 条任务就是 1 万次窗口 DC 往返）；
- 折行结果按「文本 + 可用宽度 + 字体高度」缓存，字号/DPI 变化自动失效；
- 深色模式只给主窗口套深色标题栏（子控件都是自绘或走 `WM_CTLCOLOR`），
  对话框仍全量套主题——否则空列表启动会多花约 100 ms。

复跑：`powershell -File build/_refactor/startup.ps1 -Exe <exe> -Runs 3`
（临时脚本，在 `.gitignore` 内的 `build/`）。

v2.3.3 补充测量（同一份 5000 条合成数据：4000 条短文本 + 1000 条约 80 字长文本）：

| 数据 | v2.3.2 | v2.3.3 | 变化 |
| --- | --- | --- | --- |
| 5000 条（短 + 长混合） | 1350 ms | **571 ms**（555–711，n=5） | −58% |
| 5000 条纯短任务 | 700 ms | **347 ms** | −50% |
| 空列表 | 293 ms | 288 ms | 持平 |

做法：去掉启动时重复的一次全量重建；行高每行只算一次（`g_rowH`）；行高按任务缓存，签名覆盖文本、宽度、字号/DPI、完成态、展开态与截止时间，命中即跳过折行与 GDI 度量——勾选/删除后的全量重建从 362 ms 降到 3 ms（进程内实测）。

剩余大头是首帧把全部 5000 行测一遍。v2.3.4 又动了折行本身：`GetTextExtentExPointW` 一次取「宽度内可容纳字符数」替代逐前缀二分测量，折行缓存改按任务下标 + 指纹（不再是全文为键的哈希表），过滤用的小写文本一轮重建只算一遍。同一份 5000 条数据冷启动中位 518~556 ms（n=5）。再往下需要「可见行懒测高」（估算 + 滚动时校正），会牵动像素级滚动几何，未做。

## 图标

`tools/make-icon.ps1` 生成 `assets/app.ico`（微信绿纯色基调）：

- 底：圆角方块，竖直渐变 `#0FCB6E`（顶）→ `#05BC5C`（底）。两端色相都在 ~149°，
  只变明度——不偏黄也不偏青；
- 字形：纯白 `#FFFFFF` 圆头对勾。**不叠白色高光、不加白色内描边、不加投影**：
  白叠在绿上会变成奶绿（"色不纯"），投影会在勾边压出一圈脏边；
- 尺寸：16/20/24/32/40/48/64/96/128/256 十档，≤48 用 BMP 条目、≥64 用 PNG 条目；
  小尺寸自动加粗笔画（16px 用 2.2px，避免细到断成虚线）。

改完图标直接构建即可（`OBJECT_DEPENDS` 已声明 `resource.rc → app.ico/app.manifest`，
实测 `touch app.ico` 会触发 `Building RC` + `Linking`；否则会出现"构建成功但 exe 还是旧图标"）。

```bash
powershell -File tools/make-icon.ps1 -Preview build/icon-preview.png   # 生成 ICO
powershell -File build/_refactor/icon-verify.ps1                       # 结构/尺寸/纯色采样
```
