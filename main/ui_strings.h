// main/ui_strings.h —— 所有用户可见的中文文案。
//
// 这是字体覆盖检查的唯一输入：`tools/gen_font.py --check` 扫这个文件里的字符串，
// 任何字符不在 tools/font_charset.txt 里都会让门禁失败。
// 加文案的顺序：先改这里 → 更新字符集 → 重新生成字体 → 再改界面代码。
#pragma once

// 主屏
#define PQ_TXT_HOME_TITLE   "工牌管家"
#define PQ_TXT_BATTERY      "电量"
#define PQ_TXT_WAKE_COUNT   "唤醒次数"
#define PQ_TXT_LAST_SLEEP   "上次睡眠"
#define PQ_TXT_SELF_CHECK   "硬件自检"
#define PQ_TXT_FOCUS        "番茄钟"
#define PQ_TXT_REPORT       "体检报告"
#define PQ_TXT_LEGACY       "旧演示"
#define PQ_TXT_FONT_PROBE   "字体探针"
#define PQ_TXT_HOME_HINT    "上下选择  确定进入  长按返回"
#define PQ_TXT_COMING_SOON  "即将推出"
#define PQ_TXT_TIMER        "定时器"
#define PQ_TXT_KEY          "按键"

// 自检关卡
#define PQ_TXT_DEEP_SLEEP   "深睡测试"
#define PQ_TXT_DURATION     "时长"
#define PQ_TXT_SECONDS      "秒"
#define PQ_TXT_SLEEPING     "正在入睡"
#define PQ_TXT_RELEASE_KEY  "请松开按键"
#define PQ_TXT_ERROR        "误差"
#define PQ_TXT_WAKE         "唤醒"
#define PQ_TXT_SLEPT        "睡了"
#define PQ_TXT_NO_PREVIOUS  "没有历史记录"
#define PQ_TXT_UNMEASURED   "时长不可测"
#define PQ_TXT_SLEEP_FAILED "入睡失败"
#define PQ_TXT_QUEST_HINT   "确定 入睡   长按 返回"

// 字体探针
#define PQ_TXT_COVERAGE     "覆盖"
#define PQ_TXT_MISSING      "缺字"
#define PQ_TXT_FONT_SAMPLE  "以上中文应正常显示"
#define PQ_TXT_BACK_HINT    "长按返回"
#define PQ_TXT_NO_MISSING   "无缺字"

static const char *const PQ_UI_STRINGS[] = {
    PQ_TXT_HOME_TITLE, PQ_TXT_BATTERY, PQ_TXT_WAKE_COUNT, PQ_TXT_LAST_SLEEP,
    PQ_TXT_SELF_CHECK, PQ_TXT_FOCUS, PQ_TXT_REPORT, PQ_TXT_LEGACY,
    PQ_TXT_FONT_PROBE, PQ_TXT_HOME_HINT, PQ_TXT_COMING_SOON, PQ_TXT_TIMER, PQ_TXT_KEY,
    PQ_TXT_DEEP_SLEEP, PQ_TXT_DURATION, PQ_TXT_SECONDS, PQ_TXT_SLEEPING,
    PQ_TXT_RELEASE_KEY, PQ_TXT_ERROR, PQ_TXT_WAKE, PQ_TXT_SLEPT,
    PQ_TXT_NO_PREVIOUS, PQ_TXT_UNMEASURED, PQ_TXT_SLEEP_FAILED, PQ_TXT_QUEST_HINT,
    PQ_TXT_COVERAGE, PQ_TXT_MISSING, PQ_TXT_FONT_SAMPLE, PQ_TXT_BACK_HINT, PQ_TXT_NO_MISSING,
};

#define PQ_UI_STRING_COUNT (sizeof(PQ_UI_STRINGS) / sizeof(PQ_UI_STRINGS[0]))
