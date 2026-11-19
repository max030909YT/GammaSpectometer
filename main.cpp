// ============================================================================
//  main.cpp — интерфейс (чистый Win32 API, без внешних зависимостей)
//  Сборка: x86_64-w64-mingw32-g++ -O2 -std=c++17 -static -mwindows -municode
//          main.cpp app.o -o GammaSpectrometer.exe -lgdi32
// ============================================================================
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <cwchar>
#include <chrono>
#include <string>
#include <vector>
#include "core.hpp"

using namespace gs;

// ------------------------------------------------------------------ служебное
static HWND g_wnd;
static int g_dpi = 96;
static HFONT g_font, g_mono, g_fontV;
static int S(int v) { return MulDiv(v, g_dpi, 96); }

static std::wstring W(const char* s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    std::wstring w(n > 0 ? n - 1 : 0, L'\0');
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s, -1, &w[0], n);
    return w;
}
static std::wstring F(const wchar_t* f, ...) {
    wchar_t b[1024];
    va_list ap; va_start(ap, f);
    _vsnwprintf(b, 1023, f, ap);
    va_end(ap);
    b[1023] = 0;
    return b;
}

enum {
    ID_DET = 100, ID_PRESET, ID_X, ID_N0, ID_BG, ID_MC, ID_SEED, ID_LOG,
    ID_BSIM, ID_BINV, ID_BAUTO, ID_OUT, ID_NOTE,
    ID_CHK = 200,   // +i, i = 0..11
    ID_ED = 230     // +i
};

struct Preset { const wchar_t* name; std::vector<std::pair<const char*, double>> comp; };
static const std::vector<Preset> PRESETS = {
    {L"Бронза (Cu 90 / Sn 10)",      {{"Cu", 90}, {"Sn", 10}}},
    {L"Припой ПОС-61 (Sn 61 / Pb 39)", {{"Sn", 61}, {"Pb", 39}}},
    {L"Латунь (Cu 70 / Zn 30)",      {{"Cu", 70}, {"Zn", 30}}},
    {L"Дюралюминий (Al 95 / Cu 5)",  {{"Al", 95}, {"Cu", 5}}},
    {L"Кварц SiO₂",                  {{"Si", 46.74}, {"O", 53.26}}},
    {L"Вода H₂O",                    {{"H", 11.19}, {"O", 88.81}}},
    {L"Известняк CaCO₃",             {{"Ca", 40.04}, {"C", 12.00}, {"O", 47.96}}},
    {L"Свой состав (ввести вручную)", {}},
};

struct State {
    bool have = false, haveInv = false;
    Setup S;
    SpectraPair sp;
    Inverse inv;
} g;

static double getnum(int id, double def) {
    wchar_t b[64] = {0};
    GetWindowTextW(GetDlgItem(g_wnd, id), b, 63);
    for (wchar_t* p = b; *p; ++p) if (*p == L',') *p = L'.';
    wchar_t* e = nullptr;
    double v = wcstod(b, &e);
    return (e == b) ? def : v;
}
static void setnum(int id, const wchar_t* t) { SetWindowTextW(GetDlgItem(g_wnd, id), t); }
static void setout(const std::wstring& t) { SetWindowTextW(GetDlgItem(g_wnd, ID_OUT), t.c_str()); }

static RECT ChartRect() { return RECT{S(350), S(10), S(990), S(332)}; }
static void RedrawChart() { RECT r = ChartRect(); InvalidateRect(g_wnd, &r, FALSE); }

// ------------------------------------------------------------ чтение параметров
static bool ReadSetup(Setup& st, std::wstring& err) {
    const auto& el = elements();
    st.w.assign(el.size(), 0.0);
    double sum = 0;
    for (size_t i = 0; i < el.size(); ++i) {
        double v = getnum(ID_ED + (int)i, 0.0);
        if (v < 0) { err = L"Массовая доля не может быть отрицательной."; return false; }
        st.w[i] = v; sum += v;
    }
    if (sum <= 0) { err = L"Задайте состав образца (масс.%) хотя бы для одного элемента."; return false; }
    for (auto& v : st.w) v /= sum;
    st.det = (int)SendMessageW(GetDlgItem(g_wnd, ID_DET), CB_GETCURSEL, 0, 0);
    if (st.det < 0) st.det = 0;
    st.x = getnum(ID_X, 1.0);
    st.N0 = getnum(ID_N0, 1e8);
    st.bg = getnum(ID_BG, 200);
    if (st.x <= 0)  { err = L"Массовая толщина x должна быть положительной."; return false; }
    if (st.N0 < 1e3) { err = L"Счёт N0 слишком мал (минимум 1000)."; return false; }
    if (st.bg < 0) st.bg = 0;
    return true;
}

static std::mt19937_64 MakeRng() {
    unsigned long long seed = (unsigned long long)getnum(ID_SEED, 0);
    if (seed == 0)
        seed = (unsigned long long)std::chrono::high_resolution_clock::now().time_since_epoch().count() ^
               ((unsigned long long)GetTickCount64() << 20);
    return std::mt19937_64(seed);
}

static std::wstring CompString(const Setup& s) {
    std::wstring r;
    const auto& el = elements();
    for (size_t i = 0; i < el.size(); ++i) if (s.w[i] > 0) {
        if (!r.empty()) r += L", ";
        r += F(L"%ls %.2f%%", W(el[i].sym).c_str(), 100 * s.w[i]);
    }
    return r;
}

// --------------------------------------------------------------- действия
static bool DoSimulate(bool verbose) {
    std::wstring err;
    Setup st;
    if (!ReadSetup(st, err)) { MessageBoxW(g_wnd, err.c_str(), L"Ошибка ввода", MB_ICONWARNING); return false; }
    HCURSOR old = SetCursor(LoadCursor(nullptr, IDC_WAIT));
    auto rng = MakeRng();
    g.S = st; g.sp = simulate(st, rng);
    g.have = true; g.haveInv = false;
    SetCursor(old);
    RedrawChart();
    if (verbose) {
        const Detector& D = detectors()[st.det];
        std::wstring t = L"ПРЯМАЯ ЗАДАЧА: спектры смоделированы\r\n";
        t += F(L"Образец: %ls;  x = %.3g г/см²\r\n", CompString(st).c_str(), st.x);
        t += F(L"Спектрометр: %ls;  N0 = %.3g;  фон = %.3g имп/кан\r\n\r\n", W(D.name).c_str(), st.N0, st.bg);
        t += L" E, кэВ   Нуклид   FWHM, кэВ   μ/ρ смеси, см²/г   I/I0 (теория)\r\n";
        for (const auto& l : lines()) {
            double mu = mu_mix(st.w, l.E);
            t += F(L" %7.2f  %-7ls  %8.2f    %12.5f      %10.5f\r\n", l.E, W(l.nuc).c_str(), D.fwhm(l.E), mu, std::exp(-mu * st.x));
        }
        t += L"\r\nСиняя кривая — спектр без образца, красная — с образцом.\r\n";
        t += L"Нажмите «2. Определить состав», чтобы решить обратную задачу.\r\n";
        setout(t);
    }
    return true;
}

static void DoInvert() {
    if (!g.have && !DoSimulate(false)) return;
    const auto& el = elements();
    std::vector<int> cand;
    for (size_t i = 0; i < el.size(); ++i)
        if (IsDlgButtonChecked(g_wnd, ID_CHK + (int)i) == BST_CHECKED) cand.push_back((int)i);
    if (cand.empty()) { MessageBoxW(g_wnd, L"Отметьте хотя бы один элемент-кандидат.", L"Обратная задача", MB_ICONWARNING); return; }

    HCURSOR old = SetCursor(LoadCursor(nullptr, IDC_WAIT));
    const Detector& D = detectors()[g.S.det];
    g.inv = invert(D, g.sp.open, g.sp.samp, g.S.x, cand, g.S.w);
    g.haveInv = true;

    int K = (int)getnum(ID_MC, 30);
    K = std::max(1, std::min(K, 300));
    std::vector<std::vector<double>> WS;
    if (g.inv.ok) WS.push_back(g.inv.w);
    auto rng = MakeRng();
    for (int r = 1; r < K; ++r) {
        SpectraPair sp = simulate(g.S, rng);
        Inverse iv = invert(D, sp.open, sp.samp, g.S.x, cand, g.S.w);
        if (iv.ok) WS.push_back(iv.w);
    }
    SetCursor(old);
    RedrawChart();

    std::wstring t = L"ОБРАТНАЯ ЗАДАЧА: определение состава\r\n";
    t += F(L"Образец (задан): %ls;  x = %.3g г/см²\r\n\r\n", CompString(g.S).c_str(), g.S.x);
    t += L"Линии:  μ/ρ_изм = ln(I0/I)/x   (I0, I — чистые площади пиков)\r\n";
    t += L" E, кэВ   Нуклид   N без обр.  N с обр.    μ_изм     ±σ        μ_теор    статус\r\n";
    for (const auto& l : g.inv.lines) {
        if (l.used)
            t += F(L" %7.2f  %-7ls  %9.3g  %9.3g  %8.5f  %7.5f  %8.5f   ok\r\n", l.E, W(l.nuc).c_str(), l.N_open, l.N_samp, l.mu_meas, l.s_mu, l.mu_true);
        else
            t += F(L" %7.2f  %-7ls  %9ls  %9ls  %8ls  %7ls  %8.5f   %ls\r\n", l.E, W(l.nuc).c_str(), L"—", L"—", L"—", L"—", l.mu_true, W(l.why).c_str());
    }
    t += L"\r\n";
    if (!g.inv.ok) {
        t += L"Недостаточно пригодных линий для решения (нужно ≥ 2). Увеличьте N0, уменьшите x\r\nили выберите детектор с лучшим разрешением.\r\n";
        setout(t); return;
    }
    t += L"СОСТАВ, масс.%\r\n";
    t += F(L" Эл.    Задано    Найдено   ⟨МК, %d повт.⟩ ±σ\r\n", (int)WS.size());
    for (size_t i = 0; i < el.size(); ++i) {
        bool isCand = std::find(cand.begin(), cand.end(), (int)i) != cand.end();
        if (!isCand && g.S.w[i] <= 0) continue;
        if (!isCand) {
            t += F(L" %-3ls  %8.2f  %9ls   — не отмечен как кандидат (обратная задача его не видит)\r\n", W(el[i].sym).c_str(), 100 * g.S.w[i], L"—");
            continue;
        }
        double m = 0, s = 0;
        for (auto& w : WS) m += w[i];
        m /= std::max<size_t>(1, WS.size());
        for (auto& w : WS) s += (w[i] - m) * (w[i] - m);
        s = std::sqrt(s / std::max<size_t>(1, WS.size()));
        t += F(L" %-3ls  %8.2f  %9.2f   %8.2f ± %.2f\r\n", W(el[i].sym).c_str(), 100 * g.S.w[i], 100 * g.inv.w[i], 100 * m, 100 * s);
    }
    t += F(L"\r\nχ²/ндф = %.1f / %d;  использовано линий: %d из %d\r\n", g.inv.chi2, g.inv.ndf, g.inv.nused, (int)g.inv.lines.size());
    if (!g.inv.weak.empty()) {
        t += L"\r\nПлохо различимые пары кандидатов (мин. заметный перенос доли, 3σ):\r\n";
        int shown = 0;
        for (const auto& wk : g.inv.weak) {
            if (++shown > 8) { t += L"  …\r\n"; break; }
            t += F(L"  %ls – %ls:  %.0f%%\r\n", W(el[wk.i].sym).c_str(), W(el[wk.j].sym).c_str(), 100 * std::min(wk.f3, 9.99));
        }
        t += L"  Это физический предел метода: у соседних по Z элементов близкие μ/ρ.\r\n";
    }
    setout(t);
}

static void ApplyPreset(int idx) {
    if (idx < 0 || idx >= (int)PRESETS.size()) return;
    const Preset& p = PRESETS[idx];
    if (p.comp.empty()) return;
    const auto& el = elements();
    std::vector<double> w(el.size(), 0.0);
    double sum = 0;
    for (size_t i = 0; i < el.size(); ++i) {
        double v = 0;
        for (auto& c : p.comp) if (std::string(c.first) == el[i].sym) v = c.second;
        w[i] = v; sum += v;
        setnum(ID_ED + (int)i, v > 0 ? F(L"%g", v).c_str() : L"0");
        CheckDlgButton(g_wnd, ID_CHK + (int)i, v > 0 ? BST_CHECKED : BST_UNCHECKED);
    }
    for (auto& v : w) v /= sum;
    double x = 1.5 / mu_mix(w, 59.54);
    setnum(ID_X, F(L"%.2g", x).c_str());
    g.have = g.haveInv = false;
    RedrawChart();
}

static void AutoThickness() {
    Setup st; std::wstring err;
    if (!ReadSetup(st, err)) { MessageBoxW(g_wnd, err.c_str(), L"Ошибка ввода", MB_ICONWARNING); return; }
    // ln(I0/I) ≈ 1,5 на линии 59,5 кэВ — компромисс между контрастом и статистикой
    setnum(ID_X, F(L"%.2g", 1.5 / mu_mix(st.w, 59.54)).c_str());
}

// ------------------------------------------------------------------ график
static void DrawChart(HDC dc, int Wd, int Ht) {
    RECT all = {0, 0, Wd, Ht};
    FillRect(dc, &all, (HBRUSH)GetStockObject(WHITE_BRUSH));
    SetBkMode(dc, TRANSPARENT);
    SelectObject(dc, g_font);
    RECT plot = {S(60), S(22), Wd - S(14), Ht - S(34)};
    const int pw = plot.right - plot.left, ph = plot.bottom - plot.top;
    FrameRect(dc, &plot, (HBRUSH)GetStockObject(BLACK_BRUSH));
    if (!g.have) {
        RECT r = plot;
        DrawTextW(dc, L"Нажмите «1. Смоделировать спектры»", -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        return;
    }
    const double XMAX = 1450.0;
    const int chMax = (int)(XMAX / KPC);
    double ymax = 1;
    for (int c = 0; c < chMax; ++c) ymax = std::max(ymax, std::max(g.sp.open[c], g.sp.samp[c]));
    const bool logy = IsDlgButtonChecked(g_wnd, ID_LOG) == BST_CHECKED;
    double ytop;
    if (logy) ytop = std::pow(10.0, std::ceil(std::log10(std::max(ymax, 10.0))));
    else {
        double mag = std::pow(10.0, std::floor(std::log10(ymax)));
        ytop = std::ceil(ymax / mag) * mag;
    }
    auto X = [&](double e) { return plot.left + (int)(e / XMAX * pw); };
    auto Y = [&](double v) {
        double f = logy ? std::log10(std::max(v, 1.0)) / std::log10(ytop) : v / ytop;
        f = std::min(1.0, std::max(0.0, f));
        return plot.bottom - (int)(f * ph);
    };
    // сетка и подписи
    HPEN grid = CreatePen(PS_SOLID, 1, RGB(225, 225, 225));
    HPEN old = (HPEN)SelectObject(dc, grid);
    SetTextColor(dc, RGB(60, 60, 60));
    for (int e = 0; e <= 1400; e += 200) {
        int px = X(e);
        MoveToEx(dc, px, plot.top, nullptr); LineTo(dc, px, plot.bottom);
        std::wstring s = F(L"%d", e);
        SIZE sz; GetTextExtentPoint32W(dc, s.c_str(), (int)s.size(), &sz);
        TextOutW(dc, px - sz.cx / 2, plot.bottom + S(3), s.c_str(), (int)s.size());
    }
    if (logy) {
        int nd = (int)std::round(std::log10(ytop));
        for (int d = 0; d <= nd; ++d) {
            int py = Y(std::pow(10.0, d));
            MoveToEx(dc, plot.left, py, nullptr); LineTo(dc, plot.right, py);
            std::wstring s = F(L"1e%d", d);
            SIZE sz; GetTextExtentPoint32W(dc, s.c_str(), (int)s.size(), &sz);
            TextOutW(dc, plot.left - sz.cx - S(4), py - sz.cy / 2, s.c_str(), (int)s.size());
        }
    } else {
        for (int d = 0; d <= 5; ++d) {
            double v = ytop * d / 5.0; int py = Y(v);
            MoveToEx(dc, plot.left, py, nullptr); LineTo(dc, plot.right, py);
            std::wstring s = F(L"%.2g", v);
            SIZE sz; GetTextExtentPoint32W(dc, s.c_str(), (int)s.size(), &sz);
            TextOutW(dc, plot.left - sz.cx - S(4), py - sz.cy / 2, s.c_str(), (int)s.size());
        }
    }
    SelectObject(dc, old); DeleteObject(grid);

    // маркеры линий
    const auto& L = lines();
    HPEN pUsed = CreatePen(PS_DOT, 1, RGB(40, 150, 60)), pBad = CreatePen(PS_DOT, 1, RGB(170, 170, 170));
    for (size_t k = 0; k < L.size(); ++k) {
        bool used = g.haveInv && k < g.inv.lines.size() && g.inv.lines[k].used;
        SelectObject(dc, (g.haveInv && !used) ? pBad : pUsed);
        int px = X(L[k].E);
        MoveToEx(dc, px, plot.top, nullptr); LineTo(dc, px, plot.bottom);
        SetTextColor(dc, (g.haveInv && !used) ? RGB(150, 150, 150) : RGB(30, 120, 50));
        std::wstring s = F(L"%.0f", L[k].E);
        SIZE sz; GetTextExtentPoint32W(dc, s.c_str(), (int)s.size(), &sz);
        TextOutW(dc, px - sz.cx / 2, plot.top + S(2) + (int)(k % 2) * (sz.cy - 1), s.c_str(), (int)s.size());
    }
    SelectObject(dc, old); DeleteObject(pUsed); DeleteObject(pBad);

    // спектры: для каждого пикселя — максимум по каналам (пики не пропадают)
    auto draw = [&](const std::vector<double>& sp, COLORREF col) {
        HPEN p = CreatePen(PS_SOLID, 1, col);
        HPEN o = (HPEN)SelectObject(dc, p);
        bool first = true;
        for (int px = 0; px < pw; ++px) {
            int c0 = (int)((double)px / pw * chMax), c1 = std::max(c0 + 1, (int)((double)(px + 1) / pw * chMax));
            double m = 0; for (int c = c0; c < c1 && c < NCH; ++c) m = std::max(m, sp[c]);
            int py = Y(m);
            if (first) { MoveToEx(dc, plot.left + px, py, nullptr); first = false; } else LineTo(dc, plot.left + px, py);
        }
        SelectObject(dc, o); DeleteObject(p);
    };
    draw(g.sp.open, RGB(30, 90, 200));
    draw(g.sp.samp, RGB(210, 50, 50));

    // легенда и подписи осей
    SetTextColor(dc, RGB(30, 90, 200));
    TextOutW(dc, plot.right - S(210), plot.top + S(24), L"— без образца", 13);
    SetTextColor(dc, RGB(210, 50, 50));
    TextOutW(dc, plot.right - S(210), plot.top + S(38), L"— с образцом", 12);
    SetTextColor(dc, RGB(0, 0, 0));
    const wchar_t* xl = L"E, кэВ";
    SIZE sz; GetTextExtentPoint32W(dc, xl, 6, &sz);
    TextOutW(dc, plot.left + pw / 2 - sz.cx / 2, Ht - sz.cy - S(2), xl, 6);
    SelectObject(dc, g_fontV);
    const wchar_t* yl = L"Отсчётов в канале";
    GetTextExtentPoint32W(dc, yl, (int)wcslen(yl), &sz);
    TextOutW(dc, S(2), plot.top + ph / 2 + sz.cx / 2, yl, (int)wcslen(yl));
    SelectObject(dc, g_font);
}

// ------------------------------------------------------------------- окно
static HWND mk(const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id, DWORD ex = 0) {
    return CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style, S(x), S(y), S(w), S(h), g_wnd,
                           (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
}

static void CreateControls() {
    const auto& el = elements();
    mk(L"STATIC", L"Спектрометр (разрешение детектора):", 0, 10, 8, 330, 18, -1);
    HWND cd = mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 10, 26, 330, 200, ID_DET);
    for (const auto& d : detectors()) SendMessageW(cd, CB_ADDSTRING, 0, (LPARAM)W(d.name).c_str());
    SendMessageW(cd, CB_SETCURSEL, 0, 0);

    mk(L"STATIC", L"Образец (пресет):", 0, 10, 56, 330, 18, -1);
    HWND cp = mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 10, 74, 330, 220, ID_PRESET);
    for (const auto& p : PRESETS) SendMessageW(cp, CB_ADDSTRING, 0, (LPARAM)p.name);
    SendMessageW(cp, CB_SETCURSEL, 0, 0);

    mk(L"STATIC", L"Состав, масс.%   (флажок — кандидат при поиске):", 0, 10, 104, 330, 18, -1);
    for (size_t i = 0; i < el.size(); ++i) {
        int col = (int)i / 6, row = (int)i % 6;
        int x = 10 + col * 168, y = 124 + row * 26;
        mk(L"BUTTON", W(el[i].sym).c_str(), BS_AUTOCHECKBOX | WS_TABSTOP, x, y + 2, 50, 20, ID_CHK + (int)i);
        mk(L"EDIT", L"0", ES_AUTOHSCROLL | ES_RIGHT | WS_TABSTOP, x + 52, y, 74, 22, ID_ED + (int)i, WS_EX_CLIENTEDGE);
    }
    struct Row { const wchar_t* label; const wchar_t* def; int id; };
    Row rows[] = {
        {L"Масс. толщина x, г/см²",      L"1",         ID_X},
        {L"Счёт в сильнейшем пике N0",   L"100000000", ID_N0},
        {L"Фон, имп/канал (при 0 кэВ)",  L"200",       ID_BG},
        {L"Повторов Монте-Карло",        L"30",        ID_MC},
        {L"Seed (0 — случайный)",        L"0",         ID_SEED},
    };
    for (int i = 0; i < 5; ++i) {
        int y = 286 + i * 26;
        mk(L"STATIC", rows[i].label, 0, 10, y + 3, 190, 18, -1);
        mk(L"EDIT", rows[i].def, ES_AUTOHSCROLL | ES_RIGHT | WS_TABSTOP, 205, y, 80, 22, rows[i].id, WS_EX_CLIENTEDGE);
    }
    mk(L"BUTTON", L"Авто", BS_PUSHBUTTON | WS_TABSTOP, 291, 285, 49, 24, ID_BAUTO);
    HWND lg = mk(L"BUTTON", L"Логарифмическая шкала Y", BS_AUTOCHECKBOX | WS_TABSTOP, 10, 418, 330, 20, ID_LOG);
    SendMessageW(lg, BM_SETCHECK, BST_CHECKED, 0);
    mk(L"BUTTON", L"1. Смоделировать спектры", BS_PUSHBUTTON | WS_TABSTOP, 10, 448, 330, 32, ID_BSIM);
    mk(L"BUTTON", L"2. Определить состав (обратная задача)", BS_PUSHBUTTON | WS_TABSTOP, 10, 486, 330, 32, ID_BINV);
    mk(L"STATIC",
       L"Источники: Am-241, Ba-133, Na-22, Cs-137, Co-60 (9 линий, 59–1332 кэВ).\n"
       L"Данные: NIST SRD 126, табл. 3, μ/ρ для 12 элементов, 50 кэВ – 2 МэВ.\n"
       L"Модель: узкий пучок, закон Бугера–Ламберта, аддитивность μ/ρ.",
       0, 10, 528, 330, 90, ID_NOTE);
    mk(L"EDIT", L"", ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | ES_AUTOHSCROLL | WS_VSCROLL | WS_HSCROLL,
       350, 340, 640, 290, ID_OUT, WS_EX_CLIENTEDGE);

    EnumChildWindows(g_wnd, [](HWND h, LPARAM) -> BOOL {
        SendMessageW(h, WM_SETFONT, (WPARAM)(GetDlgCtrlID(h) == ID_OUT ? g_mono : g_font), TRUE);
        return TRUE;
    }, 0);
    ApplyPreset(0);
    setout(L"Выберите образец и нажмите «1. Смоделировать спектры».\r\n\r\n"
           L"Прямая задача: по составу и толщине образца строятся два спектра\r\n"
           L"(без образца и с образцом) с разрешением детектора, комптоновским\r\n"
           L"континуумом, фоном и пуассоновским шумом.\r\n\r\n"
           L"Обратная задача: по площадям пиков находится ln(I0/I) → μ/ρ смеси на\r\n"
           L"каждой линии → массовые доли элементов (метод наименьших квадратов\r\n"
           L"с условием w ≥ 0, Σw = 1). Погрешности оцениваются методом Монте-Карло.\r\n");
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        g_wnd = hwnd;
        CreateControls();
        return 0;
    case WM_COMMAND: {
        int id = LOWORD(wp), code = HIWORD(wp);
        if (id == ID_PRESET && code == CBN_SELCHANGE) ApplyPreset((int)SendMessageW(GetDlgItem(hwnd, ID_PRESET), CB_GETCURSEL, 0, 0));
        else if (id == ID_DET && code == CBN_SELCHANGE) { g.have = g.haveInv = false; RedrawChart(); }
        else if (id == ID_BSIM && code == BN_CLICKED) DoSimulate(true);
        else if (id == ID_BINV && code == BN_CLICKED) DoInvert();
        else if (id == ID_BAUTO && code == BN_CLICKED) AutoThickness();
        else if (id == ID_LOG && code == BN_CLICKED) RedrawChart();
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc = ChartRect();
        int w = rc.right - rc.left, h = rc.bottom - rc.top;
        HDC mem = CreateCompatibleDC(hdc);
        HBITMAP bmp = CreateCompatibleBitmap(hdc, w, h);
        HGDIOBJ ob = SelectObject(mem, bmp);
        DrawChart(mem, w, h);
        BitBlt(hdc, rc.left, rc.top, w, h, mem, 0, 0, SRCCOPY);
        SelectObject(mem, ob); DeleteObject(bmp); DeleteDC(mem);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int nShow) {
    SetProcessDPIAware();
    HDC sdc = GetDC(nullptr);
    g_dpi = GetDeviceCaps(sdc, LOGPIXELSX);
    ReleaseDC(nullptr, sdc);
    g_font = CreateFontW(-MulDiv(9, g_dpi, 72), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    g_fontV = CreateFontW(-MulDiv(9, g_dpi, 72), 0, 900, 900, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    g_mono = CreateFontW(-MulDiv(9, g_dpi, 72), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, FIXED_PITCH, L"Consolas");

    WNDCLASSEXW wc = {sizeof(wc)};
    wc.lpfnWndProc = WndProc; wc.hInstance = hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"GammaSpecWnd";
    RegisterClassExW(&wc);

    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT r = {0, 0, S(1000), S(640)};
    AdjustWindowRect(&r, style, FALSE);
    int ww = r.right - r.left, wh = r.bottom - r.top;
    int sx = (GetSystemMetrics(SM_CXSCREEN) - ww) / 2, sy = (GetSystemMetrics(SM_CYSCREEN) - wh) / 2;
    HWND hw = CreateWindowExW(0, wc.lpszClassName, L"Гамма-спектрометр: определение состава образца (закон Бугера–Ламберта, данные NIST)",
                              style, std::max(0, sx), std::max(0, sy), ww, wh, nullptr, nullptr, hInst, nullptr);
    ShowWindow(hw, nShow);
    UpdateWindow(hw);
    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(hw, &m)) { TranslateMessage(&m); DispatchMessageW(&m); }
    }
    return (int)m.wParam;
}
