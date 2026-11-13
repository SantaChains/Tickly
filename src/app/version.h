// 版本号唯一来源：改版本只改这里，CI 会校验 resource.rc / app.manifest 与之一致。
#pragma once

#include "common.h"


// ---------------------------- 版本号（唯一来源）-----------------------------
// 界面提示（设置对话框里的状态行）只从这里取版本号，不要再各写各的 ——
// 之前 4 条提示各带一份 "v1.13.x"，改版本时最容易漏改其中一条。
// 改版本时需同步三处：本宏、assets/resource.rc 的 FILEVERSION、
// assets/app.manifest 的 version（CMake 配置阶段校验，不一致直接失败）。
#define TICKLY_VERSION   L"2.3.2"
#define TICKLY_VER_PFX   L"v" TICKLY_VERSION      // 界面显示用："v2.3.2"
