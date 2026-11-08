// main/ui_strings.h —— 所有用户可见的中文文案。
//
// 这是字体覆盖检查的唯一输入：`tools/gen_font.py --check` 扫这个文件里的字符串，
// 任何字符不在 tools/font_charset.txt 里都会让门禁失败。
// 加文案的顺序：先改这里 → 更新字符集 → 重新生成字体 → 再改界面代码。
#pragma once

static const char *const PQ_UI_STRINGS[] = {
    "工牌管家",
    "硬件自检",
    "番茄钟",
    "体检报告",
    "深睡测试",
    "正在入睡",
    "请松开按键",
    "电量",
    "唤醒次数",
    "上次睡眠",
    "误差",
    "完成",
    "失败",
    "旧演示",
    "字体探针",
};

#define PQ_UI_STRING_COUNT (sizeof(PQ_UI_STRINGS) / sizeof(PQ_UI_STRINGS[0]))
