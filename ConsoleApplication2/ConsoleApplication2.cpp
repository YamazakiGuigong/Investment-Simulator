#include <windows.h>
#include <string>
#include <random>
#include <cstdlib>
#include <sstream>
#include <iomanip>
#include <vector>
#include <algorithm>
#include <cmath>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <omp.h>
#pragma warning(disable:28251)

// 控件 ID
#define IDC_EDIT_CAPITAL      101
#define IDC_EDIT_PROB         102
#define IDC_EDIT_PROFIT       103
#define IDC_EDIT_STOPLOSS     104
#define IDC_EDIT_TAKEPROFIT   105
#define IDC_EDIT_INITBET      106
#define IDC_EDIT_MULTIPLIER   107
#define IDC_EDIT_CUSTOM_TIMES 108

#define IDC_BTN_ONCE          201
#define IDC_BTN_5             202
#define IDC_BTN_10            203
#define IDC_BTN_50            204
#define IDC_BTN_100           205
#define IDC_BTN_AUTO          206
#define IDC_BTN_RESET         207
#define IDC_BTN_CLEARLOG      208
#define IDC_BTN_CUSTOM_RUN    209
#define IDC_BTN_STOP_CALC     210
#define IDC_BTN_START_CALC    211

#define IDC_STATIC_STATUS     301
#define IDC_LOG               302
#define IDC_STATIC_THEORY     303

// 自定义消息
#define WM_APP_THEORY_READY   (WM_APP + 1)
#define WM_APP_THEORY_UPDATE  (WM_APP + 2)

struct TheoryUpdateData {
    int epoch;
    int iteration;
    double prob;
};

// 全局模拟状态
double g_capital = 200.0;
double g_probWin = 0.5;
double g_profitRatio = 0.995;
double g_stopLoss = 1.0;
double g_takeProfit = 250.0;
double g_initialBet = 1.0;
double g_multiplier = 2.0;
double g_currentBet = 1.0;
long long g_totalBets = 0;
bool g_running = false;
int g_currentLossStreak = 0;
int g_maxLossStreak = 0;
double g_currentLossAmount = 0.0;
double g_maxLossAmount = 0.0;
double g_maxBet = 0.0;
bool g_stopLogged = false;

// 理论胜率计算状态
double g_theoreticalProb = -1.0;          // 最终结果，-1 表示未完成
double g_lastDisplayProb = -1.0;          // 最近一次迭代的近似概率，用于暂停时显示
bool g_hasCalculated = false;             // 是否已完成计算
std::atomic<bool> g_calcInProgress{ false };
bool g_calcEnabled = true;                // 是否允许自动计算
std::atomic<bool> g_pauseCalc{ false };   // 暂停标志
std::mutex g_pauseMutex;
std::condition_variable g_pauseCV;

// 上次计算参数（文本快照，用于精确比较）
std::wstring g_lastEditTexts[7];

// 上次计算参数（浮点值，仅用于内部逻辑，不作为比较依据）
double g_lastCalcCapital = -1.0;
double g_lastCalcProb = -1.0;
double g_lastCalcProfit = -1.0;
double g_lastCalcStop = -1.0;
double g_lastCalcTake = -1.0;
double g_lastCalcInitBet = -1.0;
double g_lastCalcMult = -1.0;

// 计算世代
std::atomic<int> g_calcEpoch{ 0 };

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
void CheckAndLogStop(HWND hWnd);
double CalculateTheoreticalProbability(HWND hWnd, double capital, double probWin, double profitRatio,
    double stopLoss, double takeProfit,
    double initialBet, double multiplier, int epoch);
void StartNewCalculation(HWND hWnd);
void SaveEditTexts(HWND hWnd, std::wstring* outTexts);

std::wstring FormatDouble(double value) {
    std::wstringstream wss;
    wss << std::fixed << std::setprecision(3) << value;
    return wss.str();
}

double GetEditFloat(HWND hWnd, int id) {
    wchar_t buf[256];
    GetDlgItemTextW(hWnd, id, buf, 255);
    return _wtof(buf);
}

void SetEditFloat(HWND hWnd, int id, double value) {
    std::wstring s = FormatDouble(value);
    SetDlgItemTextW(hWnd, id, s.c_str());
}

void SaveEditTexts(HWND hWnd, std::wstring* outTexts) {
    wchar_t buf[256];
    GetDlgItemTextW(hWnd, IDC_EDIT_CAPITAL, buf, 255);    outTexts[0] = buf;
    GetDlgItemTextW(hWnd, IDC_EDIT_PROB, buf, 255);       outTexts[1] = buf;
    GetDlgItemTextW(hWnd, IDC_EDIT_PROFIT, buf, 255);     outTexts[2] = buf;
    GetDlgItemTextW(hWnd, IDC_EDIT_STOPLOSS, buf, 255);   outTexts[3] = buf;
    GetDlgItemTextW(hWnd, IDC_EDIT_TAKEPROFIT, buf, 255); outTexts[4] = buf;
    GetDlgItemTextW(hWnd, IDC_EDIT_INITBET, buf, 255);    outTexts[5] = buf;
    GetDlgItemTextW(hWnd, IDC_EDIT_MULTIPLIER, buf, 255); outTexts[6] = buf;
}

void UpdateDisplay(HWND hWnd) {
    std::wstring status = L"资金: " + FormatDouble(g_capital) +
        L"  当前投注: " + FormatDouble(g_currentBet) +
        L"  当前连败: " + std::to_wstring(g_currentLossStreak) +
        L" (" + FormatDouble(g_currentLossAmount) + L")" +
        L"  最高连败: " + std::to_wstring(g_maxLossStreak) +
        L" (" + FormatDouble(g_maxLossAmount) + L")" +
        L"  最高单注: " + FormatDouble(g_maxBet) +
        L"  总投注: " + std::to_wstring(g_totalBets);
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

    // 理论胜率显示
    std::wstring theoryText;
    if (g_theoreticalProb >= 0.0) {
        theoryText = L"达到止盈点胜率: " + FormatDouble(g_theoreticalProb * 100.0) + L"%";
        if (g_pauseCalc.load()) {
            theoryText += L"（停止中）";
        }
        else if (g_calcInProgress.load()) {
            theoryText += L"（计算中...）";
        }
        else {
            theoryText += L"（已计算完成）";
        }
    }
    else {
        if (g_lastDisplayProb >= 0.0) {
            theoryText = L"达到止盈点胜率: " + FormatDouble(g_lastDisplayProb * 100.0) + L"%";
            if (g_pauseCalc.load()) {
                theoryText += L"（停止中）";
            }
            else if (g_calcInProgress.load()) {
                theoryText += L"（计算中...）";
            }
        }
        else {
            if (g_calcInProgress.load()) {
                if (g_pauseCalc.load())
                    theoryText = L"达到止盈点胜率: 已暂停";
                else
                    theoryText = L"达到止盈点胜率: 计算中...";
            }
            else if (!g_calcEnabled) {
                theoryText = L"达到止盈点胜率: 未计算（停止）";
            }
            else {
                theoryText = L"达到止盈点胜率: 未计算";
            }
        }
    }
    SetDlgItemTextW(hWnd, IDC_STATIC_THEORY, theoryText.c_str());
}

void LogMessage(HWND hWnd, const std::wstring& msg) {
    HWND hLog = GetDlgItem(hWnd, IDC_LOG);
    int len = GetWindowTextLengthW(hLog);
    if (len > 100000) {
        SetWindowTextW(hLog, L"");
        len = 0;
    }

    wchar_t* buf = new wchar_t[len + 1];
    GetWindowTextW(hLog, buf, len + 1);
    std::wstring old = buf;
    delete[] buf;

    old += msg + L"\r\n";
    SetWindowTextW(hLog, old.c_str());

    SendMessageW(hLog, EM_SETSEL, (WPARAM)old.size(), (LPARAM)old.size());
    SendMessageW(hLog, EM_SCROLLCARET, 0, 0);
}

void CheckAndLogStop(HWND hWnd) {
    if (!g_running && !g_stopLogged) {
        std::wstring msg = L"本轮结束，最终资金: " + FormatDouble(g_capital);
        if (g_capital >= g_takeProfit)
            msg += L"（止盈）";
        else if (g_capital < g_stopLoss)
            msg += L"（止损）";
        else
            msg += L"（资金不足无法继续）";
        LogMessage(hWnd, msg);
        g_stopLogged = true;
    }
}

// ========== 并行化理论胜率计算（支持暂停/继续） ==========
double CalculateTheoreticalProbability(HWND hWnd, double capital, double probWin, double profitRatio,
    double stopLoss, double takeProfit,
    double initialBet, double multiplier, int epoch) {
    const double delta = 0.001;
    double minC = stopLoss - delta;
    double maxC = takeProfit + delta;
    int nC = static_cast<int>((maxC - minC) / delta + 0.5) + 1;

    std::vector<double> betList;
    double b = initialBet;
    while (b <= takeProfit + initialBet) {
        betList.push_back(b);
        if (multiplier <= 1.0) break;
        b *= multiplier;
        if (b < 1e-9) break;
    }
    if (betList.empty()) betList.push_back(initialBet);
    int nB = static_cast<int>(betList.size());

    std::vector<std::vector<double>> P_old(nC, std::vector<double>(nB, 0.5));
    std::vector<std::vector<double>> P_new(nC, std::vector<double>(nB, 0.5));

    for (int i = 0; i < nC; ++i) {
        double C = minC + i * delta;
        if (C >= takeProfit) {
            for (int k = 0; k < nB; ++k) {
                P_old[i][k] = 1.0;
                P_new[i][k] = 1.0;
            }
        }
        else if (C < stopLoss) {
            for (int k = 0; k < nB; ++k) {
                P_old[i][k] = 0.0;
                P_new[i][k] = 0.0;
            }
        }
    }

    int idxCap = static_cast<int>((capital - minC) / delta + 0.5);
    idxCap = max(0, min(idxCap, nC - 1));

    const int maxIter = 10000;
    const double tol = 1e-12;
    const int updateInterval = 100;

    for (int iter = 0; iter < maxIter; ++iter) {
        {
            std::unique_lock<std::mutex> lock(g_pauseMutex);
            g_pauseCV.wait(lock, [&]() {
                return !g_pauseCalc.load() || epoch != g_calcEpoch.load();
                });
            if (epoch != g_calcEpoch.load()) {
                double currentProb = P_old[idxCap][0];
                TheoryUpdateData* data = new TheoryUpdateData{ epoch, iter, currentProb };
                PostMessageW(hWnd, WM_APP_THEORY_UPDATE, 0, (LPARAM)data);
                return -1.0;
            }
        }

        double maxDiff = 0.0;

#pragma omp parallel
        {
            double localMaxDiff = 0.0;

#pragma omp for
            for (int i = 0; i < nC; ++i) {
                double C = minC + i * delta;
                if (C >= takeProfit || C < stopLoss) continue;

                for (int k = 0; k < nB; ++k) {
                    double b = betList[k];
                    double newP;

                    if (b > C) {
                        if (initialBet > C) {
                            newP = 0.0;
                        }
                        else {
                            int idxC = static_cast<int>((C - minC) / delta + 0.5);
                            idxC = max(0, min(idxC, nC - 1));
                            newP = P_old[idxC][0];
                        }
                    }
                    else {
                        double Cwin = C + profitRatio * b;
                        double Pwin;
                        if (Cwin >= takeProfit) {
                            Pwin = 1.0;
                        }
                        else {
                            int idxCwin = static_cast<int>((Cwin - minC) / delta + 0.5);
                            idxCwin = max(0, min(idxCwin, nC - 1));
                            Pwin = P_old[idxCwin][0];
                        }

                        double Close = C - b;
                        double Plose;
                        if (Close < stopLoss) {
                            Plose = 0.0;
                        }
                        else {
                            double bNext = b * multiplier;
                            int idxClose = static_cast<int>((Close - minC) / delta + 0.5);
                            idxClose = max(0, min(idxClose, nC - 1));
                            if (bNext > Close) {
                                if (initialBet > Close) {
                                    Plose = 0.0;
                                }
                                else {
                                    Plose = P_old[idxClose][0];
                                }
                            }
                            else {
                                int idxB = 0;
                                double minDiff = 1e30;
                                for (int j = 0; j < nB; ++j) {
                                    double diff = fabs(betList[j] - bNext);
                                    if (diff < minDiff) {
                                        minDiff = diff;
                                        idxB = j;
                                    }
                                }
                                Plose = P_old[idxClose][idxB];
                            }
                        }

                        newP = probWin * Pwin + (1.0 - probWin) * Plose;
                    }

                    double diff = fabs(newP - P_old[i][k]);
                    if (diff > localMaxDiff) localMaxDiff = diff;
                    P_new[i][k] = newP;
                }
            }

#pragma omp critical
            {
                if (localMaxDiff > maxDiff) maxDiff = localMaxDiff;
            }
        }

        std::swap(P_old, P_new);

        if (epoch != g_calcEpoch.load()) {
            double currentProb = P_old[idxCap][0];
            TheoryUpdateData* data = new TheoryUpdateData{ epoch, iter, currentProb };
            PostMessageW(hWnd, WM_APP_THEORY_UPDATE, 0, (LPARAM)data);
            return -1.0;
        }

        if (iter % updateInterval == 0 || maxDiff < tol) {
            TheoryUpdateData* data = new TheoryUpdateData{ epoch, iter, P_old[idxCap][0] };
            PostMessageW(hWnd, WM_APP_THEORY_UPDATE, 0, (LPARAM)data);
        }

        if (maxDiff < tol) break;
    }

    return P_old[idxCap][0];
}

// 启动新计算（内部函数）
void StartNewCalculation(HWND hWnd) {
    g_lastCalcCapital = g_capital;
    g_lastCalcProb = g_probWin;
    g_lastCalcProfit = g_profitRatio;
    g_lastCalcStop = g_stopLoss;
    g_lastCalcTake = g_takeProfit;
    g_lastCalcInitBet = g_initialBet;
    g_lastCalcMult = g_multiplier;

    // 保存编辑框文本快照
    SaveEditTexts(hWnd, g_lastEditTexts);

    g_theoreticalProb = -1.0;
    g_lastDisplayProb = -1.0;
    g_hasCalculated = false;
    g_calcInProgress = true;
    g_pauseCalc = false;
    g_pauseCV.notify_all();

    int epoch = ++g_calcEpoch;

    double capital = g_capital;
    double prob = g_probWin;
    double profit = g_profitRatio;
    double stop = g_stopLoss;
    double take = g_takeProfit;
    double initBet = g_initialBet;
    double mult = g_multiplier;

    std::thread calcThread([hWnd, capital, prob, profit, stop, take, initBet, mult, epoch]() {
        double result = CalculateTheoreticalProbability(hWnd, capital, prob, profit,
            stop, take, initBet, mult, epoch);
        if (epoch == g_calcEpoch.load() && result >= 0.0) {
            double* pResult = new double(result);
            PostMessageW(hWnd, WM_APP_THEORY_READY, 0, (LPARAM)pResult);
        }
        });
    calcThread.detach();
}

// 读取参数并重置模拟
void ReadParamsAndReset(HWND hWnd) {
    double newCapital = GetEditFloat(hWnd, IDC_EDIT_CAPITAL);
    double newProb = GetEditFloat(hWnd, IDC_EDIT_PROB);
    double newProfit = GetEditFloat(hWnd, IDC_EDIT_PROFIT);
    double newStop = GetEditFloat(hWnd, IDC_EDIT_STOPLOSS);
    double newTake = GetEditFloat(hWnd, IDC_EDIT_TAKEPROFIT);
    double newInitBet = GetEditFloat(hWnd, IDC_EDIT_INITBET);
    double newMult = GetEditFloat(hWnd, IDC_EDIT_MULTIPLIER);

    // 使用文本比较判断参数是否变化
    std::wstring curTexts[7];
    SaveEditTexts(hWnd, curTexts);
    bool paramsChanged = false;
    for (int i = 0; i < 7; ++i) {
        if (curTexts[i] != g_lastEditTexts[i]) {
            paramsChanged = true;
            break;
        }
    }

    g_capital = newCapital;
    g_probWin = newProb;
    g_profitRatio = newProfit;
    g_stopLoss = newStop;
    g_takeProfit = newTake;
    g_initialBet = newInitBet;
    g_multiplier = newMult;

    g_currentBet = g_initialBet;
    g_totalBets = 0;
    g_running = true;
    g_currentLossStreak = 0;
    g_maxLossStreak = 0;
    g_currentLossAmount = 0.0;
    g_maxLossAmount = 0.0;
    g_maxBet = 0.0;
    g_stopLogged = false;

    if (g_capital >= g_takeProfit || g_capital < g_stopLoss)
        g_running = false;

    bool needCalc = false;
    if (g_calcEnabled && !g_pauseCalc.load()) {
        if (paramsChanged) {
            needCalc = true;
        }
        else if (!g_hasCalculated && !g_calcInProgress.load()) {
            needCalc = true;
        }
    }

    if (needCalc) {
        StartNewCalculation(hWnd);
    }

    UpdateDisplay(hWnd);
    if (needCalc)
        LogMessage(hWnd, L"已重置，参数已应用，正在后台计算理论胜率...");
}

// 执行一次投注
void RunOnce() {
    if (!g_running) return;

    if (g_capital >= g_takeProfit || g_capital < g_stopLoss) {
        g_running = false;
        return;
    }

    if (g_currentBet > g_capital) {
        g_currentBet = g_initialBet;
    }

    if (g_currentBet > g_capital) {
        g_running = false;
        return;
    }

    double bet = g_currentBet;

    if (bet > g_maxBet) {
        g_maxBet = bet;
    }

    double rand = g_dist(g_rng);

    if (rand < g_probWin) {
        g_capital += bet * g_profitRatio;
        g_currentBet = g_initialBet;
        g_currentLossStreak = 0;
        g_currentLossAmount = 0.0;
    }
    else {
        g_capital -= bet;
        g_currentBet *= g_multiplier;
        g_currentLossStreak++;
        g_currentLossAmount += bet;

        if (g_currentLossStreak > g_maxLossStreak ||
            (g_currentLossStreak == g_maxLossStreak && g_currentLossAmount > g_maxLossAmount)) {
            g_maxLossStreak = g_currentLossStreak;
            g_maxLossAmount = g_currentLossAmount;
        }
    }

    g_totalBets++;

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
    const long long maxSteps = 10000000;
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
}

// 创建所有子控件
void CreateControls(HWND hWnd) {
    // 第一行
    CreateWindowExW(0, L"STATIC", L"本金:", WS_CHILD | WS_VISIBLE, 20, 20, 80, 25, hWnd, nullptr, nullptr, nullptr);
    CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"200.000", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 120, 20, 100, 25, hWnd, (HMENU)IDC_EDIT_CAPITAL, nullptr, nullptr);

    CreateWindowExW(0, L"STATIC", L"胜率(0-1):", WS_CHILD | WS_VISIBLE, 240, 20, 80, 25, hWnd, nullptr, nullptr, nullptr);
    CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"0.500", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 330, 20, 100, 25, hWnd, (HMENU)IDC_EDIT_PROB, nullptr, nullptr);

    CreateWindowExW(0, L"STATIC", L"单次投资收益:", WS_CHILD | WS_VISIBLE, 450, 20, 110, 25, hWnd, nullptr, nullptr, nullptr);
    CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"0.995", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 570, 20, 120, 25, hWnd, (HMENU)IDC_EDIT_PROFIT, nullptr, nullptr);

    // 第二行
    CreateWindowExW(0, L"STATIC", L"止损点:", WS_CHILD | WS_VISIBLE, 20, 60, 80, 25, hWnd, nullptr, nullptr, nullptr);
    CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"1.000", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 120, 60, 100, 25, hWnd, (HMENU)IDC_EDIT_STOPLOSS, nullptr, nullptr);

    CreateWindowExW(0, L"STATIC", L"止盈点:", WS_CHILD | WS_VISIBLE, 240, 60, 80, 25, hWnd, nullptr, nullptr, nullptr);
    CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"250.000", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 330, 60, 100, 25, hWnd, (HMENU)IDC_EDIT_TAKEPROFIT, nullptr, nullptr);

    CreateWindowExW(0, L"STATIC", L"初始投注:", WS_CHILD | WS_VISIBLE, 450, 60, 80, 25, hWnd, nullptr, nullptr, nullptr);
    CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"1.000", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 540, 60, 100, 25, hWnd, (HMENU)IDC_EDIT_INITBET, nullptr, nullptr);

    // 第三行
    CreateWindowExW(0, L"STATIC", L"倍投因子:", WS_CHILD | WS_VISIBLE, 20, 100, 80, 25, hWnd, nullptr, nullptr, nullptr);
    CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"2.000", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 120, 100, 100, 25, hWnd, (HMENU)IDC_EDIT_MULTIPLIER, nullptr, nullptr);

    // 按钮行
    CreateWindowExW(0, L"BUTTON", L"投资一次", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 20, 150, 80, 30, hWnd, (HMENU)IDC_BTN_ONCE, nullptr, nullptr);
    CreateWindowExW(0, L"BUTTON", L"5次", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 110, 150, 60, 30, hWnd, (HMENU)IDC_BTN_5, nullptr, nullptr);
    CreateWindowExW(0, L"BUTTON", L"10次", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 180, 150, 60, 30, hWnd, (HMENU)IDC_BTN_10, nullptr, nullptr);
    CreateWindowExW(0, L"BUTTON", L"50次", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 250, 150, 60, 30, hWnd, (HMENU)IDC_BTN_50, nullptr, nullptr);
    CreateWindowExW(0, L"BUTTON", L"100次", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 320, 150, 60, 30, hWnd, (HMENU)IDC_BTN_100, nullptr, nullptr);

    // 自定义次数输入框和按钮
    CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 390, 150, 60, 25, hWnd, (HMENU)IDC_EDIT_CUSTOM_TIMES, nullptr, nullptr);
    CreateWindowExW(0, L"BUTTON", L"自定义", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 460, 150, 70, 30, hWnd, (HMENU)IDC_BTN_CUSTOM_RUN, nullptr, nullptr);

    CreateWindowExW(0, L"BUTTON", L"一直投", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 540, 150, 70, 30, hWnd, (HMENU)IDC_BTN_AUTO, nullptr, nullptr);
    CreateWindowExW(0, L"BUTTON", L"重置", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 620, 150, 70, 30, hWnd, (HMENU)IDC_BTN_RESET, nullptr, nullptr);
    CreateWindowExW(0, L"BUTTON", L"清空日志", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 700, 150, 80, 30, hWnd, (HMENU)IDC_BTN_CLEARLOG, nullptr, nullptr);

    // 状态显示
    CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT, 20, 195, 940, 25, hWnd, (HMENU)IDC_STATIC_STATUS, nullptr, nullptr);

    // 达到止盈点胜率显示
    CreateWindowExW(0, L"STATIC", L"达到止盈点胜率: 计算中...", WS_CHILD | WS_VISIBLE | SS_LEFT, 20, 220, 300, 25, hWnd, (HMENU)IDC_STATIC_THEORY, nullptr, nullptr);

    // 停止计算按钮
    CreateWindowExW(0, L"BUTTON", L"停止计算", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 330, 220, 80, 25, hWnd, (HMENU)IDC_BTN_STOP_CALC, nullptr, nullptr);

    // 开始计算按钮
    CreateWindowExW(0, L"BUTTON", L"开始计算", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 420, 220, 80, 25, hWnd, (HMENU)IDC_BTN_START_CALC, nullptr, nullptr);

    // 日志窗口
    CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL, 20, 250, 940, 310, hWnd, (HMENU)IDC_LOG, nullptr, nullptr);
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

        case IDC_BTN_CLEARLOG:
            SetWindowTextW(GetDlgItem(hWnd, IDC_LOG), L"");
            break;

        case IDC_BTN_ONCE:
            if (!g_running) ReadParamsAndReset(hWnd);
            RunOnce();
            UpdateDisplay(hWnd);
            CheckAndLogStop(hWnd);
            break;

        case IDC_BTN_5:
            if (!g_running) ReadParamsAndReset(hWnd);
            RunMultiple(5);
            UpdateDisplay(hWnd);
            CheckAndLogStop(hWnd);
            break;

        case IDC_BTN_10:
            if (!g_running) ReadParamsAndReset(hWnd);
            RunMultiple(10);
            UpdateDisplay(hWnd);
            CheckAndLogStop(hWnd);
            break;

        case IDC_BTN_50:
            if (!g_running) ReadParamsAndReset(hWnd);
            RunMultiple(50);
            UpdateDisplay(hWnd);
            CheckAndLogStop(hWnd);
            break;

        case IDC_BTN_100:
            if (!g_running) ReadParamsAndReset(hWnd);
            RunMultiple(100);
            UpdateDisplay(hWnd);
            CheckAndLogStop(hWnd);
            break;

        case IDC_BTN_CUSTOM_RUN:
        {
            if (!g_running) ReadParamsAndReset(hWnd);
            wchar_t buf[256];
            GetDlgItemTextW(hWnd, IDC_EDIT_CUSTOM_TIMES, buf, 255);
            int customTimes = _wtoi(buf);
            if (customTimes > 0) {
                RunMultiple(customTimes);
                UpdateDisplay(hWnd);
                CheckAndLogStop(hWnd);
            }
            break;
        }

        case IDC_BTN_AUTO:
            if (!g_running) ReadParamsAndReset(hWnd);
            RunAuto(hWnd);
            CheckAndLogStop(hWnd);
            break;

        case IDC_BTN_STOP_CALC:
        {
            g_pauseCalc = true;
            g_calcEnabled = false;
            UpdateDisplay(hWnd);
            LogMessage(hWnd, L"已暂停理论胜率计算");
            break;
        }

        case IDC_BTN_START_CALC:
        {
            g_calcEnabled = true;
            if (g_pauseCalc.load()) {
                // 之前处于暂停状态，使用文本比较参数是否变化
                std::wstring curTexts[7];
                SaveEditTexts(hWnd, curTexts);
                bool paramsChanged = false;
                for (int i = 0; i < 7; ++i) {
                    if (curTexts[i] != g_lastEditTexts[i]) {
                        paramsChanged = true;
                        break;
                    }
                }
                if (paramsChanged) {
                    StartNewCalculation(hWnd);
                    LogMessage(hWnd, L"参数已变更，开始新的理论胜率计算");
                }
                else {
                    // 参数未变，继续计算
                    g_pauseCalc = false;
                    g_pauseCV.notify_all();
                    LogMessage(hWnd, L"继续理论胜率计算");
                }
                UpdateDisplay(hWnd);
            }
            else {
                LogMessage(hWnd, L"计算已在运行中");
            }
            break;
        }
        }
        break;

    case WM_APP_THEORY_UPDATE:
    {
        TheoryUpdateData* pData = (TheoryUpdateData*)lParam;
        if (pData) {
            if (pData->epoch == g_calcEpoch.load()) {
                g_lastDisplayProb = pData->prob;
                if (!g_pauseCalc.load()) {
                    std::wstring theoryText = L"达到止盈点胜率: " + FormatDouble(pData->prob * 100.0) +
                        L"% (迭代 " + std::to_wstring(pData->iteration) + L")";
                    SetDlgItemTextW(hWnd, IDC_STATIC_THEORY, theoryText.c_str());
                }
                else {
                    std::wstring theoryText = L"达到止盈点胜率: " + FormatDouble(pData->prob * 100.0) +
                        L"%（停止中）";
                    SetDlgItemTextW(hWnd, IDC_STATIC_THEORY, theoryText.c_str());
                }
            }
            delete pData;
        }
    }
    break;

    case WM_APP_THEORY_READY:
    {
        double* pResult = (double*)lParam;
        if (pResult) {
            double result = *pResult;
            delete pResult;
            if (g_calcEpoch.load() > 0) {
                g_theoreticalProb = result;
                g_hasCalculated = true;
                g_calcInProgress = false;
                g_pauseCalc = false;
                g_lastDisplayProb = result;
            }
            UpdateDisplay(hWnd);
        }
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
        CW_USEDEFAULT, CW_USEDEFAULT, 1080, 720,
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