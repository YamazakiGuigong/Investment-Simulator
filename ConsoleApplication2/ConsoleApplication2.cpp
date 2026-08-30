// 倍投投资模拟器 - Win32 API 版本
// 编译环境：Visual Studio 2022 / 2026，新建“Windows 桌面应用程序”项目
// 将本文件替换主源文件后编译即可

#include <windows.h>
#include <string>
#include <random>
#include <cstdlib>

// 控件 ID
#define IDC_EDIT_CAPITAL      101
#define IDC_EDIT_PROB         102
#define IDC_EDIT_PROFIT       103
#define IDC_EDIT_STOPLOSS     104
#define IDC_EDIT_TAKEPROFIT   105
#define IDC_EDIT_INITBET      106
#define IDC_EDIT_MULTIPLIER   107

#define IDC_BTN_ONCE          201
#define IDC_BTN_5             202
#define IDC_BTN_10            203
#define IDC_BTN_50            204
#define IDC_BTN_100           205
#define IDC_BTN_AUTO          206
#define IDC_BTN_RESET         207

#define IDC_STATIC_STATUS     301
#define IDC_LOG               302

// 全局模拟状态
double g_capital = 200.0;        // 当前资金
double g_probWin = 0.5;          // 胜率
double g_profitRatio = 0.995;    // 赢时收益比例
double g_stopLoss = 1.0;         // 止损点
double g_takeProfit = 250.0;     // 止盈点
double g_initialBet = 1.0;       // 初始投注额
double g_multiplier = 2.0;       // 倍投因子
double g_currentBet = 1.0;       // 当前投注额
long long g_totalBets = 0;       // 总投注次数
bool g_running = false;          // 是否运行中

// 随机数生成器
std::mt19937 g_rng{ std::random_device{}() };
std::uniform_real_distribution<double> g_dist(0.0, 1.0);

// 函数声明
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
void CreateControls(HWND hWnd);
void UpdateDisplay(HWND hWnd);
void ReadParamsAndReset(HWND hWnd);
void RunOnce();
void RunMultiple(int times);
void RunAuto(HWND hWnd);
void LogMessage(HWND hWnd, const std::wstring& msg);

// 从编辑框读取浮点数
double GetEditFloat(HWND hWnd, int id) {
    wchar_t buf[256];
    GetDlgItemTextW(hWnd, id, buf, 255);
    return _wtof(buf);
}

// 设置编辑框浮点数
void SetEditFloat(HWND hWnd, int id, double value) {
    std::wstring s = std::to_wstring(value);
    SetDlgItemTextW(hWnd, id, s.c_str());
}

// 更新状态栏
void UpdateDisplay(HWND hWnd) {
    std::wstring status = L"资金: " + std::to_wstring(g_capital) +
        L"  当前投注: " + std::to_wstring(g_currentBet) +
        L"  总投注次数: " + std::to_wstring(g_totalBets);
    if (!g_running) {
        if (g_capital >= g_takeProfit)
            status += L"  状态: 止盈停止";
        else if (g_capital < g_stopLoss)
            status += L"  状态: 止损停止";
        else
            status += L"  状态: 已停止";
    }
    else {
        status += L"  状态: 运行中";
    }
    SetDlgItemTextW(hWnd, IDC_STATIC_STATUS, status.c_str());
}

// 向日志框追加一行
void LogMessage(HWND hWnd, const std::wstring& msg) {
    HWND hLog = GetDlgItem(hWnd, IDC_LOG);
    int len = GetWindowTextLengthW(hLog);
    if (len > 100000) {  // 日志过长则清空
        SetWindowTextW(hLog, L"");
        len = 0;
    }

    wchar_t* buf = new wchar_t[len + 1];
    GetWindowTextW(hLog, buf, len + 1);   // 获取现有文本
    std::wstring old = buf;
    delete[] buf;

    old += msg + L"\r\n";
    SetWindowTextW(hLog, old.c_str());    // 设置新文本

    // 滚动到底部
    SendMessageW(hLog, EM_SETSEL, (WPARAM)old.size(), (LPARAM)old.size());
    SendMessageW(hLog, EM_SCROLLCARET, 0, 0);
}

// 读取参数并重置模拟
void ReadParamsAndReset(HWND hWnd) {
    g_capital = GetEditFloat(hWnd, IDC_EDIT_CAPITAL);
    g_probWin = GetEditFloat(hWnd, IDC_EDIT_PROB);
    g_profitRatio = GetEditFloat(hWnd, IDC_EDIT_PROFIT);
    g_stopLoss = GetEditFloat(hWnd, IDC_EDIT_STOPLOSS);
    g_takeProfit = GetEditFloat(hWnd, IDC_EDIT_TAKEPROFIT);
    g_initialBet = GetEditFloat(hWnd, IDC_EDIT_INITBET);
    g_multiplier = GetEditFloat(hWnd, IDC_EDIT_MULTIPLIER);

    g_currentBet = g_initialBet;
    g_totalBets = 0;
    g_running = true;

    // 初始检查停止条件
    if (g_capital >= g_takeProfit || g_capital < g_stopLoss)
        g_running = false;

    UpdateDisplay(hWnd);
    LogMessage(hWnd, L"已重置，参数已应用");
}

// 执行一次投注
void RunOnce() {
    if (!g_running) return;

    // 达到止盈/止损则停止
    if (g_capital >= g_takeProfit || g_capital < g_stopLoss) {
        g_running = false;
        return;
    }

    // 当前投注额超过资金，则重置为初始投注额
    if (g_currentBet > g_capital) {
        g_currentBet = g_initialBet;
    }

    // 初始投注额仍超过资金，无法继续
    if (g_currentBet > g_capital) {
        g_running = false;
        return;
    }

    double bet = g_currentBet;
    double rand = g_dist(g_rng);

    if (rand < g_probWin) {
        // 赢
        g_capital += bet * g_profitRatio;
        g_currentBet = g_initialBet;
    }
    else {
        // 输
        g_capital -= bet;
        g_currentBet *= g_multiplier;
    }

    g_totalBets++;

    // 再次检查停止条件
    if (g_capital >= g_takeProfit || g_capital < g_stopLoss) {
        g_running = false;
    }
}

// 执行多次投注
void RunMultiple(int times) {
    for (int i = 0; i < times && g_running; i++) {
        RunOnce();
    }
}

// 自动运行直到停止
void RunAuto(HWND hWnd) {
    const long long maxSteps = 1000000;  // 防止无限循环
    long long steps = 0;

    while (g_running && steps < maxSteps) {
        RunOnce();
        steps++;
    }

    if (g_running) {
        g_running = false;
        LogMessage(hWnd, L"自动运行超过最大步数，已强制停止");
    }

    UpdateDisplay(hWnd);
    LogMessage(hWnd, L"自动运行结束，最终资金: " + std::to_wstring(g_capital));
}

// 创建所有子控件
void CreateControls(HWND hWnd) {
    // 第一行：本金、胜率、单次收益
    CreateWindowExW(0, L"STATIC", L"本金:", WS_CHILD | WS_VISIBLE, 20, 20, 80, 25, hWnd, nullptr, nullptr, nullptr);
    CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"200", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 120, 20, 100, 25, hWnd, (HMENU)IDC_EDIT_CAPITAL, nullptr, nullptr);

    CreateWindowExW(0, L"STATIC", L"胜率(0-1):", WS_CHILD | WS_VISIBLE, 240, 20, 80, 25, hWnd, nullptr, nullptr, nullptr);
    CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"0.5", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 330, 20, 100, 25, hWnd, (HMENU)IDC_EDIT_PROB, nullptr, nullptr);

    CreateWindowExW(0, L"STATIC", L"单次投资收益:", WS_CHILD | WS_VISIBLE, 450, 20, 80, 25, hWnd, nullptr, nullptr, nullptr);
    CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"0.995", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 540, 20, 100, 25, hWnd, (HMENU)IDC_EDIT_PROFIT, nullptr, nullptr);

    // 第二行：止损、止盈、初始投注
    CreateWindowExW(0, L"STATIC", L"止损点:", WS_CHILD | WS_VISIBLE, 20, 60, 80, 25, hWnd, nullptr, nullptr, nullptr);
    CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"1", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 120, 60, 100, 25, hWnd, (HMENU)IDC_EDIT_STOPLOSS, nullptr, nullptr);

    CreateWindowExW(0, L"STATIC", L"止盈点:", WS_CHILD | WS_VISIBLE, 240, 60, 80, 25, hWnd, nullptr, nullptr, nullptr);
    CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"250", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 330, 60, 100, 25, hWnd, (HMENU)IDC_EDIT_TAKEPROFIT, nullptr, nullptr);

    CreateWindowExW(0, L"STATIC", L"初始投注:", WS_CHILD | WS_VISIBLE, 450, 60, 80, 25, hWnd, nullptr, nullptr, nullptr);
    CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"1", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 540, 60, 100, 25, hWnd, (HMENU)IDC_EDIT_INITBET, nullptr, nullptr);

    // 第三行：倍投因子
    CreateWindowExW(0, L"STATIC", L"倍投因子:", WS_CHILD | WS_VISIBLE, 20, 100, 80, 25, hWnd, nullptr, nullptr, nullptr);
    CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"2", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 120, 100, 100, 25, hWnd, (HMENU)IDC_EDIT_MULTIPLIER, nullptr, nullptr);

    // 按钮行
    CreateWindowExW(0, L"BUTTON", L"投资一次", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 20, 150, 80, 30, hWnd, (HMENU)IDC_BTN_ONCE, nullptr, nullptr);
    CreateWindowExW(0, L"BUTTON", L"5次", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 110, 150, 60, 30, hWnd, (HMENU)IDC_BTN_5, nullptr, nullptr);
    CreateWindowExW(0, L"BUTTON", L"10次", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 180, 150, 60, 30, hWnd, (HMENU)IDC_BTN_10, nullptr, nullptr);
    CreateWindowExW(0, L"BUTTON", L"50次", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 250, 150, 60, 30, hWnd, (HMENU)IDC_BTN_50, nullptr, nullptr);
    CreateWindowExW(0, L"BUTTON", L"100次", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 320, 150, 60, 30, hWnd, (HMENU)IDC_BTN_100, nullptr, nullptr);
    CreateWindowExW(0, L"BUTTON", L"一直投", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 390, 150, 70, 30, hWnd, (HMENU)IDC_BTN_AUTO, nullptr, nullptr);
    CreateWindowExW(0, L"BUTTON", L"重置", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 470, 150, 70, 30, hWnd, (HMENU)IDC_BTN_RESET, nullptr, nullptr);

    // 状态显示
    CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT, 20, 195, 660, 25, hWnd, (HMENU)IDC_STATIC_STATUS, nullptr, nullptr);

    // 日志窗口
    CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL, 20, 220, 660, 340, hWnd, (HMENU)IDC_LOG, nullptr, nullptr);
}

// 主窗口过程
LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        CreateControls(hWnd);
        ReadParamsAndReset(hWnd);
        break;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_BTN_RESET:
            ReadParamsAndReset(hWnd);
            break;

        case IDC_BTN_ONCE:
            if (!g_running) ReadParamsAndReset(hWnd);
            RunOnce();
            UpdateDisplay(hWnd);
            break;

        case IDC_BTN_5:
            if (!g_running) ReadParamsAndReset(hWnd);
            RunMultiple(5);
            UpdateDisplay(hWnd);
            break;

        case IDC_BTN_10:
            if (!g_running) ReadParamsAndReset(hWnd);
            RunMultiple(10);
            UpdateDisplay(hWnd);
            break;

        case IDC_BTN_50:
            if (!g_running) ReadParamsAndReset(hWnd);
            RunMultiple(50);
            UpdateDisplay(hWnd);
            break;

        case IDC_BTN_100:
            if (!g_running) ReadParamsAndReset(hWnd);
            RunMultiple(100);
            UpdateDisplay(hWnd);
            break;

        case IDC_BTN_AUTO:
            if (!g_running) ReadParamsAndReset(hWnd);
            RunAuto(hWnd);
            break;
        }
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
    return 0;
}

// 程序入口
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"MartingaleSimulator";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassW(&wc);

    HWND hWnd = CreateWindowExW(
        0, wc.lpszClassName, L"倍投投资模拟器",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 720, 640,
        nullptr, nullptr, hInstance, nullptr
    );

    if (!hWnd) return 0;

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}
