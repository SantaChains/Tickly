// ============================================================================
//  Tickly —— 极简 Win32 待办工具
//
//  分层：
//    src/app/      应用层：入口状态、ID 常量、任务用例
//    src/core/     领域层：任务模型、时间/文本工具、日期解析、持久化、分组
//    src/ui/       界面层：主题、GDI、主窗口、自绘列表、行编辑、各对话框、托盘…
//    src/platform/ 平台细节：DPI 感知
//
//  约定：
//    * 每个模块自带头文件，跨模块只通过头文件里的声明交互；
//    * 模块私有的状态/函数保持 static，不出现在头文件里；
//    * 函数体与既有行为保持逐字一致（本次拆分为纯结构性重构）；
//    * 构建见仓库根 CMakeLists.txt。资源（图标 / DPI 清单 / 版本信息）
//      仍由 assets/resource.rc 提供，必须参与链接。
// ============================================================================
#pragma once

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS   // MSVC：不提示 localtime/_wfopen 的安全告警
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <commctrl.h>
#include <commdlg.h>     // GetSaveFileNameW（分组导出）
#include <cstdio>        // _wfopen/fwrite
#include <string>
#include <vector>
#include <algorithm>
#include <cwchar>
#include <ctime>
#include <utility>
#include <cstdlib>
#include <cstring>
#include <cstdarg>

#ifndef MOD_NOREPEAT
#define MOD_NOREPEAT 0x4000   // RegisterHotKey：按住不重复触发（Vista+）
#endif
