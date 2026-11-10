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


// 自检关卡（M2）
#define PQ_TXT_LEVEL_DISPLAY "屏幕测试"
#define PQ_TXT_LEVEL_BACKLIGHT "背光测试"
#define PQ_TXT_LEVEL_I2C     "接口扫描"
#define PQ_TXT_LEVEL_BATTERY "电量计"
#define PQ_TXT_LEVEL_BUTTON  "按键测试"
#define PQ_TXT_PENDING       "待测"
#define PQ_TXT_PASS          "通过"
#define PQ_TXT_FAIL          "失败"
#define PQ_TXT_MANUAL        "人工确认"
#define PQ_TXT_NEXT          "下一个"
#define PQ_TXT_BRIGHTNESS    "亮度"
#define PQ_TXT_VOLTAGE       "电压"
#define PQ_TXT_DEVICES       "设备"
#define PQ_TXT_ALL_DONE      "全部完成"
#define PQ_TXT_CLICK_UP      "请单击上键"
#define PQ_TXT_CLICK_DOWN    "请单击下键"
#define PQ_TXT_CLICK_OK      "请单击确定"
#define PQ_TXT_LONG_OK       "请长按确定"
#define PQ_TXT_RELEASE       "请松开"
#define PQ_TXT_WHITE         "白色"
#define PQ_TXT_BLACK         "黑色"
#define PQ_TXT_RED           "红色"
#define PQ_TXT_GREEN         "绿色"
#define PQ_TXT_BLUE          "蓝色"
#define PQ_TXT_GRAYSCALE     "灰阶"
#define PQ_TXT_QUEST_REPORT  "自检报告"
#define PQ_TXT_RETEST        "确定重测"
#define PQ_TXT_UNMEASURED_ERR "读数不可信"


// 番茄钟（M4）
#define PQ_TXT_FOCUS_STAGE   "专注"
#define PQ_TXT_BREAK_STAGE   "休息"
#define PQ_TXT_REMAINING     "剩余"
#define PQ_TXT_MINUTES       "分"
#define PQ_TXT_START         "开始"
#define PQ_TXT_CONTINUE      "继续"
#define PQ_TXT_GIVE_UP       "放弃"
#define PQ_TXT_FOCUS_DONE    "专注完成"
#define PQ_TXT_BREAK_DONE    "休息结束"
#define PQ_TXT_COMPLETED     "已完成"
#define PQ_TXT_PAUSED        "已暂停"
#define PQ_TXT_FLASH_WARNING "整片烧录会重置报告"

#define PQ_TXT_HINT_START    "确定 开始   长按返回"
#define PQ_TXT_HINT_PAUSE    "确定 继续   长按上键 放弃"
#define PQ_TXT_HINT_BREAK    "确定 开始休息   长按返回"
#define PQ_TXT_HINT_DONE     "确定 完成   长按返回"
#define PQ_TXT_NEXT_STAGE   "下一步"
#define PQ_TXT_HINT_IDLE    "上下 调整  长按上键 切换  确定 开始"
#define PQ_TXT_SHORT_MODE    "1 分钟短档"
#define PQ_TXT_LONG_MODE     "25/5 档"

static const char *const PQ_UI_STRINGS[] = {
    PQ_TXT_HOME_TITLE, PQ_TXT_BATTERY, PQ_TXT_WAKE_COUNT, PQ_TXT_LAST_SLEEP,
    PQ_TXT_SELF_CHECK, PQ_TXT_FOCUS, PQ_TXT_REPORT, PQ_TXT_LEGACY,
    PQ_TXT_FONT_PROBE, PQ_TXT_HOME_HINT, PQ_TXT_COMING_SOON, PQ_TXT_TIMER, PQ_TXT_KEY,
    PQ_TXT_DEEP_SLEEP, PQ_TXT_DURATION, PQ_TXT_SECONDS, PQ_TXT_SLEEPING,
    PQ_TXT_RELEASE_KEY, PQ_TXT_ERROR, PQ_TXT_WAKE, PQ_TXT_SLEPT,
    PQ_TXT_NO_PREVIOUS, PQ_TXT_UNMEASURED, PQ_TXT_SLEEP_FAILED, PQ_TXT_QUEST_HINT,
    PQ_TXT_COVERAGE, PQ_TXT_MISSING, PQ_TXT_FONT_SAMPLE, PQ_TXT_BACK_HINT, PQ_TXT_NO_MISSING,
    PQ_TXT_LEVEL_DISPLAY, PQ_TXT_LEVEL_BACKLIGHT, PQ_TXT_LEVEL_I2C, PQ_TXT_LEVEL_BATTERY,
    PQ_TXT_LEVEL_BUTTON, PQ_TXT_PENDING, PQ_TXT_PASS, PQ_TXT_FAIL, PQ_TXT_MANUAL,
    PQ_TXT_NEXT, PQ_TXT_BRIGHTNESS, PQ_TXT_VOLTAGE, PQ_TXT_DEVICES, PQ_TXT_ALL_DONE,
    PQ_TXT_CLICK_UP, PQ_TXT_CLICK_DOWN, PQ_TXT_CLICK_OK, PQ_TXT_LONG_OK, PQ_TXT_RELEASE,
    PQ_TXT_WHITE, PQ_TXT_BLACK, PQ_TXT_RED, PQ_TXT_GREEN, PQ_TXT_BLUE, PQ_TXT_GRAYSCALE,
    PQ_TXT_FOCUS_STAGE, PQ_TXT_BREAK_STAGE, PQ_TXT_REMAINING, PQ_TXT_MINUTES,
    PQ_TXT_START, PQ_TXT_CONTINUE, PQ_TXT_GIVE_UP, PQ_TXT_FOCUS_DONE,
    PQ_TXT_BREAK_DONE, PQ_TXT_COMPLETED, PQ_TXT_PAUSED, PQ_TXT_FLASH_WARNING,
    PQ_TXT_HINT_START, PQ_TXT_HINT_PAUSE, PQ_TXT_HINT_BREAK, PQ_TXT_HINT_DONE, PQ_TXT_HINT_IDLE, PQ_TXT_NEXT_STAGE,
    PQ_TXT_SHORT_MODE, PQ_TXT_LONG_MODE,
    PQ_TXT_QUEST_REPORT, PQ_TXT_RETEST, PQ_TXT_UNMEASURED_ERR,
};

#define PQ_UI_STRING_COUNT (sizeof(PQ_UI_STRINGS) / sizeof(PQ_UI_STRINGS[0]))
