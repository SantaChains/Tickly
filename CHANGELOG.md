# Tickly 变更记录

本文件只保留最新版本的更新说明。旧版本由 `tools/archive-changelog.ps1` 归档到 `CHANGELOG.old`（新版本在前），不要手工搬运。

## v2.3.2 工程重构、配色升级与缺陷修复

### 工程重构

- 单文件源码 `Tickly_v2.3.1.cpp`（6797 行）按职责拆为 `src/app`、`src/core`、`src/ui`、`src/platform` 四层，模块之间只通过头文件里的声明交互。
- 构建方式由 CI 里手写的 `cl.exe` 命令改为 CMake + Ninja，产物统一为 `build/Tickly.exe`，CI 改为直接调用 CMake。
- 版本号唯一来源收敛到 `src/app/version.h`：CMake 在配置阶段校验 `assets/resource.rc` 的 `FILEVERSION` 与 `assets/app.manifest` 的 `assemblyIdentity version`，不一致直接配置失败。
- 资源集中到 `assets/`：图标 `app.ico`、DPI 清单 `app.manifest`、资源脚本 `resource.rc`，以及 mingw 用的 `manifest.rc`。
- 新增 mingw-w64 构建支持；`cmake/mingw64.specs` 去掉 mingw 默认清单，避免与 PerMonitorV2 清单冲突。
- 资源编译显式声明 `OBJECT_DEPENDS`，改图标或清单必定触发重新编译与重链，不再出现「构建成功但 exe 还是旧图标」。
- README 按构建、目录结构、数据格式重写。

### 配色升级

- 主色由 Apple 蓝 `#007AFF` 改为与应用图标同源的微信绿 `#07C160`，浅色底 `#F5F6F7`、深色底 `#1A1C1E`，逾期红由 `#FF3B30` 换成 `#FA5151`。
- 选中行改为淡绿底 `#E8F5EE`（深色 `#26302B`）；主按钮与行内光标颜色改为由 `COL_ACCENT` 派生，之后换主题色不必再改第二处。

### 缺陷修复

- 设置对话框长标签在初始化时按实际字体测宽，非 100% 缩放下不再被自身控件宽度截断。
- 补上 Delete 键删除所选任务，多选时按批量删除处理，并在删除前关闭正在编辑的行。
- 清理 5 条 dead-code 告警：`IsToday`、`DeleteSelected`、`date_parse` 的 `hm`、`dialogs_common` 的 `left`、`rows.cpp` 的 lambda 捕获 `g`。
- MinGW 资源编译不再向 RC 传 windres 专有的 `--codepage`（llvm-rc 会直接失败），代码页统一由 .rc 的 `#pragma code_page` 声明，llvm-rc 追加 `/C65001`。
- CI 拆分构建与发布任务：构建/PR 只用只读令牌，仅 tag 发布任务提权；补 MinGW 构建任务，覆盖 `manifest.rc` 与 `mingw64.specs` 分支。
- 开机自启改为 `--autostart` 静默启动：只驻留托盘，不弹主窗口，提醒、热键与托盘菜单照常；旧注册表项在启动时自动改写，升级后下一次开机即生效。
