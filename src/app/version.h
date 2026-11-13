// 版本号唯一来源：改版本只改这里，CI 会校验 resource.rc / app.manifest 与之一致。
#pragma once

#include "common.h"


// ---------------------------- 版本号（唯一来源）-----------------------------
// 界面提示（设置对话框里的状态行）只从这里取版本号，不要再各写各的 ——
// 之前 4 条提示各带一份 "v1.13.x"，改版本时最容易漏改其中一条。
// 改版本时需同步三处：本宏、文件名 Tickly_vX.Y.cpp、resource.rc 的 FILEVERSION
// （CI 会校验后两者与此处导出的 exe 版本是否一致）。
#define TICKLY_VERSION   L"2.3.1"
#define TICKLY_VER_PFX   L"v" TICKLY_VERSION      // 界面显示用："v2.3.1"
