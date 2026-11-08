// main/demo_menu.h —— 上游 demo 菜单的入口。
//
// 本应用的主屏在 M1 期间保留它作为参考页（LEGACY DEMOS）；M2 重做界面后删除。
#pragma once

// 建立并载入 demo 菜单。调用方必须持 LVGL 锁。
void demo_menu_show(void);
