// ============================================================================
//  core.hpp — ядро программы «Гамма-спектрометр: определение состава образца»
//
//  Физика:   закон Бугера–Ламберта  I = I0 * exp(-(mu/rho) * x),
//            правило аддитивности   mu/rho = sum_i w_i * (mu/rho)_i
//  Данные:   NIST SRD 126 (Hubbell & Seltzer), Table 3, mu/rho, см^2/г
//            (https://physics.nist.gov/PhysRefData/XrayMassCoef/tab3.html)
// ============================================================================
#pragma once
#include <vector>
#include <cmath>
#include <random>
#include <algorithm>
#include <cstdint>

namespace gs {

constexpr int    NCH = 4096;     // число каналов анализатора
constexpr double KPC = 0.5;      // кэВ на канал (диапазон 0...2048 кэВ)
constexpr double MEC2 = 511.0;   // энергия покоя электрона, кэВ

// ---------------------------------------------------------------- данные NIST
struct Element {
    const char* sym;
    const char* name;
    std::vector<double> E;   // МэВ (в точке K-края энергия повторяется дважды)
    std::vector<double> mu;  // mu/rho, см^2/г
};

inline std::vector<Element> build_elements() {
    const std::vector<double> G = {0.05, 0.06, 0.08, 0.10, 0.15, 0.20, 0.30, 0.40,
                                   0.50, 0.60, 0.80, 1.00, 1.25, 1.50, 2.00};
    std::vector<Element> v;
    auto add = [&](const char* s, const char* n, std::vector<double> mu) {
        v.push_back({s, n, G, std::move(mu)});
    };
    add("H",  "Водород",  {0.3355, 0.3260, 0.3091, 0.2944, 0.2651, 0.2429, 0.2112, 0.1893, 0.1729, 0.1599, 0.1405, 0.1263, 0.1129, 0.1027, 0.08769});
    add("C",  "Углерод",  {0.1871, 0.1753, 0.1610, 0.1514, 0.1347, 0.1229, 0.1066, 0.09546, 0.08715, 0.08058, 0.07076, 0.06361, 0.05690, 0.05179, 0.04442});
    add("N",  "Азот",     {0.1980, 0.1817, 0.1639, 0.1529, 0.1353, 0.1233, 0.1068, 0.09557, 0.08719, 0.08063, 0.07081, 0.06364, 0.05693, 0.05180, 0.04450});
    add("O",  "Кислород", {0.2132, 0.1907, 0.1678, 0.1551, 0.1361, 0.1237, 0.1070, 0.09566, 0.08729, 0.08070, 0.07087, 0.06372, 0.05697, 0.05185, 0.04459});
    add("Al", "Алюминий", {0.3681, 0.2778, 0.2018, 0.1704, 0.1378, 0.1223, 0.1042, 0.09276, 0.08445, 0.07802, 0.06841, 0.06146, 0.05496, 0.05006, 0.04324});
    add("Si", "Кремний",  {0.4385, 0.3207, 0.2228, 0.1835, 0.1448, 0.1275, 0.1082, 0.09614, 0.08748, 0.08077, 0.07082, 0.06361, 0.05688, 0.05183, 0.04480});
    add("Ca", "Кальций",  {1.019, 0.6578, 0.3656, 0.2571, 0.1674, 0.1376, 0.1116, 0.09783, 0.08851, 0.08148, 0.07122, 0.06388, 0.05709, 0.05207, 0.04524});
    add("Fe", "Железо",   {1.958, 1.205, 0.5952, 0.3717, 0.1964, 0.1460, 0.1099, 0.09400, 0.08414, 0.07704, 0.06699, 0.05995, 0.05350, 0.04883, 0.04265});
    add("Cu", "Медь",     {2.613, 1.593, 0.7630, 0.4584, 0.2217, 0.1559, 0.1119, 0.09413, 0.08362, 0.07625, 0.06605, 0.05901, 0.05261, 0.04803, 0.04205});
    add("Zn", "Цинк",     {2.892, 1.760, 0.8364, 0.4973, 0.2341, 0.1617, 0.1141, 0.09539, 0.08450, 0.07695, 0.06656, 0.05941, 0.05296, 0.04834, 0.04235});
    add("Sn", "Олово",    {10.70, 6.564, 3.029, 1.676, 0.6091, 0.3260, 0.1639, 0.1156, 0.09374, 0.08113, 0.06662, 0.05800, 0.05095, 0.04638, 0.04112});
    // Свинец: K-край 88,0045 кэВ (значения до и после края)
    v.push_back({"Pb", "Свинец",
        {0.05, 0.06, 0.08, 0.0880045, 0.0880045, 0.10, 0.15, 0.20, 0.30, 0.40, 0.50, 0.60, 0.80, 1.00, 1.25, 1.50, 2.00},
        {8.041, 5.021, 2.419, 1.910, 7.683, 5.549, 2.014, 0.9985, 0.4031, 0.2323, 0.1614, 0.1248, 0.0887, 0.07102, 0.05876, 0.05222, 0.04606}});
    return v;
}

inline const std::vector<Element>& elements() {
    static const std::vector<Element> v = build_elements();
    return v;
}

// mu/rho элемента при энергии E (МэВ): лог-лог интерполяция таблицы NIST
inline double mu_el(const Element& e, double E_MeV) {
    const size_t n = e.E.size();
    if (E_MeV <= e.E[0])     return e.mu[0];
    if (E_MeV >= e.E[n - 1]) return e.mu[n - 1];
    for (size_t i = 0; i + 1 < n; ++i) {
        if (e.E[i] <= E_MeV && E_MeV < e.E[i + 1]) {   // нулевой интервал (край) пропускается
            double t = (std::log(E_MeV) - std::log(e.E[i])) / (std::log(e.E[i + 1]) - std::log(e.E[i]));
            return std::exp(std::log(e.mu[i]) + t * (std::log(e.mu[i + 1]) - std::log(e.mu[i])));
        }
    }
    return e.mu[n - 1];
}

// mu/rho смеси при энергии E (кэВ); w — массовые доли (в сумме 1)
inline double mu_mix(const std::vector<double>& w, double E_keV) {
    const auto& el = elements();
    double s = 0;
    for (size_t i = 0; i < el.size() && i < w.size(); ++i)
        if (w[i] != 0) s += w[i] * mu_el(el[i], E_keV / 1000.0);
    return s;
}

// ------------------------------------------------------------ источники и детектор
struct Line { const char* nuc; double E; double I; };   // кэВ, квантов на распад

inline const std::vector<Line>& lines() {
    static const std::vector<Line> v = {
        {"Am-241", 59.54, 0.359},   {"Ba-133", 80.998, 0.329}, {"Ba-133", 302.85, 0.1833},
        {"Ba-133", 356.01, 0.6205}, {"Na-22", 511.0, 1.798},   {"Cs-137", 661.657, 0.851},
        {"Co-60", 1173.23, 0.9985}, {"Na-22", 1274.54, 0.9994}, {"Co-60", 1332.49, 0.9998}};
    return v;
}

// Разрешение: FWHM(E)^2 = F0^2 + b*E   (кэВ)
struct Detector {
    const char* name;
    double F0, b;
    double fwhm(double E_keV) const { return std::sqrt(F0 * F0 + b * E_keV); }
};

inline const std::vector<Detector>& detectors() {
    static const std::vector<Detector> v = {
        {"HPGe, 1,9 кэВ @1332 кэВ",       0.80, 0.00223},
        {"HPGe (хуже), 3,0 кэВ @1332 кэВ", 1.20, 0.005676},
        {"CdZnTe, ~10 кэВ @662 кэВ",       3.00, 0.1375}};
    return v;
}

// Пиковая эффективность (модель): степенной спад с энергией
inline double eff_peak(double E_keV) { return std::min(0.9, 0.05 * std::pow(E_keV / 1000.0, -0.95)); }

// Доля полного поглощения P/T (остальное — комптоновский континуум)
inline double peak_to_total(double E_keV) { return 1.0 / (1.0 + 0.35 * std::pow(E_keV / 200.0, 1.2)); }

// ------------------------------------------------------------------ прямая задача
struct Setup {
    int det = 0;
    std::vector<double> w;    // массовые доли элементов (в порядке elements()), сумма = 1
    double x = 1.0;           // массовая толщина, г/см^2
    double N0 = 2.0e6;        // счёт в сильнейшем пике открытого пучка
    double bg = 200.0;        // уровень фона, имп/канал при 0 кэВ
};

// Площади пиков в «открытом» пучке: пропорциональны I*eff, нормировка по сильнейшему
inline std::vector<double> open_areas(double N0) {
    const auto& L = lines();
    std::vector<double> a(L.size());
    double mx = 0;
    for (size_t k = 0; k < L.size(); ++k) { a[k] = L[k].I * eff_peak(L[k].E); mx = std::max(mx, a[k]); }
    for (auto& v : a) v *= N0 / mx;
    return a;
}

// Площади пиков после образца: A_k * exp(-x * mu_mix(E_k))
inline std::vector<double> attenuated_areas(const std::vector<double>& A0, const std::vector<double>& w, double x) {
    const auto& L = lines();
    std::vector<double> a(A0.size());
    for (size_t k = 0; k < A0.size(); ++k) a[k] = A0[k] * std::exp(-x * mu_mix(w, L[k].E));
    return a;
}

// Ожидаемый спектр: гауссовы пики + комптоновские плато + фон
inline std::vector<double> expected_spectrum(const Detector& D, const std::vector<double>& A, double bg) {
    const auto& L = lines();
    std::vector<double> s(NCH, 0.0);
    for (size_t k = 0; k < L.size(); ++k) {
        const double E = L[k].E, sig = D.fwhm(E) / 2.3548200450309493;
        const double inv = 1.0 / (sig * std::sqrt(2.0));
        int c0 = std::max(0, (int)std::floor((E - 6 * sig) / KPC));
        int c1 = std::min(NCH - 1, (int)std::ceil((E + 6 * sig) / KPC));
        for (int c = c0; c <= c1; ++c) {
            double lo = c * KPC, hi = lo + KPC;
            s[c] += A[k] * 0.5 * (std::erf((hi - E) * inv) - std::erf((lo - E) * inv));
        }
        // комптоновский континуум: плато до комптоновского края
        const double PT = peak_to_total(E);
        const double cont = A[k] * (1.0 / PT - 1.0);
        const double edge = 2 * E * E / (MEC2 + 2 * E);
        const double dens = cont / edge;      // отсчётов на кэВ
        int cmax = std::min(NCH - 1, (int)std::ceil((edge + 6 * sig) / KPC));
        for (int c = 0; c <= cmax; ++c) {
            double e = (c + 0.5) * KPC;
            s[c] += dens * KPC * 0.5 * std::erfc((e - edge) * inv);
        }
    }
    for (int c = 0; c < NCH; ++c) {
        double e = (c + 0.5) * KPC;
        s[c] += bg * (std::exp(-e / 300.0) + 0.1);
    }
    return s;
}

// Пуассоновский розыгрыш
inline std::vector<double> poisson_sample(const std::vector<double>& lam, std::mt19937_64& rng) {
    std::vector<double> r(lam.size());
    for (size_t i = 0; i < lam.size(); ++i) {
        if (lam[i] <= 0) { r[i] = 0; continue; }
        std::poisson_distribution<long long> d(lam[i]);
        r[i] = (double)d(rng);
    }
    return r;
}

struct SpectraPair { std::vector<double> open, samp; };

inline SpectraPair simulate(const Setup& S, std::mt19937_64& rng) {
    const Detector& D = detectors()[S.det];
    auto A0 = open_areas(S.N0);
    auto A1 = attenuated_areas(A0, S.w, S.x);
    SpectraPair p;
    p.open = poisson_sample(expected_spectrum(D, A0, S.bg), rng);
    p.samp = poisson_sample(expected_spectrum(D, A1, S.bg), rng);
    return p;
}

// ---------------------------------------------------------------- обратная задача
struct PeakRes { double gross, net, sig; };

// Чистая площадь пика: окно ±1,5·FWHM, линейный фон по боковым окнам
inline PeakRes peak_area(const std::vector<double>& s, double E, double F) {
    auto ch = [](double e) { return (int)std::floor(e / KPC); };
    int p0 = ch(E - 1.5 * F), p1 = ch(E + 1.5 * F);
    int l1 = p0 - 2, l0 = std::min(ch(E - 3.5 * F), l1 - 3);
    int r0 = p1 + 2, r1 = std::max(ch(E + 3.5 * F), r0 + 3);
    l0 = std::max(l0, 0); r1 = std::min(r1, NCH - 1);
    double G = 0, SL = 0, SR = 0;
    for (int c = p0; c <= p1; ++c) G += s[c];
    for (int c = l0; c <= l1; ++c) SL += s[c];
    for (int c = r0; c <= r1; ++c) SR += s[c];
    double nP = p1 - p0 + 1, nL = l1 - l0 + 1, nR = r1 - r0 + 1;
    double mL = SL / nL, mR = SR / nR;
    double cp = 0.5 * (p0 + p1), cl = 0.5 * (l0 + l1), cr = 0.5 * (r0 + r1);
    double t = (cp - cl) / (cr - cl);
    double B = nP * (mL + t * (mR - mL));
    double varB = nP * nP * ((1 - t) * (1 - t) * SL / (nL * nL) + t * t * SR / (nR * nR));
    PeakRes r;
    r.gross = G; r.net = G - B; r.sig = std::sqrt(std::max(G + varB, 1.0));
    return r;
}

struct LineResult {
    const char* nuc = ""; double E = 0;
    bool used = false; const char* why = "";
    double N_open = 0, N_samp = 0;
    double A = 0, sA = 0;               // A = ln(I0/I)
    double mu_meas = 0, s_mu = 0;       // измеренные mu/rho смеси, см^2/г
    double mu_true = 0;                 // «теоретическое» по заданному составу
};

struct Inverse {
    bool ok = false;
    std::vector<LineResult> lines;
    std::vector<double> w;              // найденные массовые доли (по всем 12 элементам)
    double chi2 = 0; int ndf = 0; int nused = 0;
    struct Weak { int i, j; double f3; };   // f3 — минимальный различимый (3σ) перенос доли
    std::vector<Weak> weak;             // пары кандидатов, плохо различимых по затуханию (f3 > 3%)
};

// Решение min||Ax-b|| при x>=0 (Лоусон–Хэнсон)
inline bool ls_subset(const std::vector<std::vector<double>>& A, const std::vector<double>& b,
                      const std::vector<int>& P, std::vector<double>& z) {
    const int p = (int)P.size(), m = (int)A.size();
    std::vector<std::vector<double>> M(p, std::vector<double>(p + 1, 0.0));
    for (int i = 0; i < p; ++i) {
        for (int j = 0; j < p; ++j) { double s = 0; for (int r = 0; r < m; ++r) s += A[r][P[i]] * A[r][P[j]]; M[i][j] = s; }
        double s = 0; for (int r = 0; r < m; ++r) s += A[r][P[i]] * b[r]; M[i][p] = s;
    }
    double tr = 0; for (int i = 0; i < p; ++i) tr += M[i][i];
    for (int i = 0; i < p; ++i) M[i][i] += 1e-12 * tr / std::max(p, 1);
    for (int c = 0; c < p; ++c) {
        int piv = c; for (int r = c + 1; r < p; ++r) if (std::fabs(M[r][c]) > std::fabs(M[piv][c])) piv = r;
        if (std::fabs(M[piv][c]) < 1e-300) return false;
        std::swap(M[c], M[piv]);
        for (int r = c + 1; r < p; ++r) { double f = M[r][c] / M[c][c]; for (int k = c; k <= p; ++k) M[r][k] -= f * M[c][k]; }
    }
    z.assign(p, 0.0);
    for (int i = p - 1; i >= 0; --i) { double s = M[i][p]; for (int k = i + 1; k < p; ++k) s -= M[i][k] * z[k]; z[i] = s / M[i][i]; }
    return true;
}

inline std::vector<double> nnls(std::vector<std::vector<double>> A, const std::vector<double>& b) {
    const int m = (int)A.size(), n = (int)A[0].size();
    std::vector<double> cn(n, 1.0);
    for (int j = 0; j < n; ++j) {          // нормировка столбцов
        double s = 0; for (int i = 0; i < m; ++i) s += A[i][j] * A[i][j];
        cn[j] = std::sqrt(s) > 0 ? std::sqrt(s) : 1.0;
        for (int i = 0; i < m; ++i) A[i][j] /= cn[j];
    }
    std::vector<double> x(n, 0.0); std::vector<char> inP(n, 0);
    for (int it = 0; it < 10 * n + 20; ++it) {
        std::vector<double> r(m);
        for (int i = 0; i < m; ++i) { double s = 0; for (int j = 0; j < n; ++j) s += A[i][j] * x[j]; r[i] = b[i] - s; }
        int jb = -1; double wb = 1e-10;
        for (int j = 0; j < n; ++j) if (!inP[j]) {
            double w = 0; for (int i = 0; i < m; ++i) w += A[i][j] * r[i];
            if (w > wb) { wb = w; jb = j; }
        }
        if (jb < 0) break;
        inP[jb] = 1;
        for (int inner = 0; inner < 10 * n + 20; ++inner) {
            std::vector<int> P; for (int j = 0; j < n; ++j) if (inP[j]) P.push_back(j);
            std::vector<double> z; if (!ls_subset(A, b, P, z)) { inP[jb] = 0; break; }
            bool allpos = true; for (double v : z) if (v <= 0) { allpos = false; break; }
            if (allpos) { std::fill(x.begin(), x.end(), 0.0); for (size_t i = 0; i < P.size(); ++i) x[P[i]] = z[i]; break; }
            double alpha = 1e300;
            for (size_t i = 0; i < P.size(); ++i) if (z[i] <= 0) { double d = x[P[i]] - z[i]; if (d > 0) alpha = std::min(alpha, x[P[i]] / d); }
            for (size_t i = 0; i < P.size(); ++i) x[P[i]] += alpha * (z[i] - x[P[i]]);
            for (int j = 0; j < n; ++j) if (inP[j] && x[j] <= 1e-12) { inP[j] = 0; x[j] = 0; }
        }
    }
    for (int j = 0; j < n; ++j) x[j] /= cn[j];
    return x;
}

// Восстановление состава по двум спектрам (без образца / с образцом).
// cand — индексы элементов-кандидатов; wtrue нужен лишь для колонки «теория».
inline Inverse invert(const Detector& D, const std::vector<double>& open, const std::vector<double>& samp,
                      double x, const std::vector<int>& cand, const std::vector<double>& wtrue) {
    const auto& L = lines(); const auto& el = elements();
    Inverse R; R.lines.resize(L.size()); R.w.assign(el.size(), 0.0);
    for (size_t k = 0; k < L.size(); ++k) {
        LineResult& lr = R.lines[k];
        lr.nuc = L[k].nuc; lr.E = L[k].E;
        if (!wtrue.empty()) lr.mu_true = mu_mix(wtrue, L[k].E);
        const double F = D.fwhm(lr.E);
        bool resolved = true;
        for (size_t j = 0; j < L.size(); ++j) if (j != k) {
            double Fj = D.fwhm(L[j].E);
            if (std::fabs(L[j].E - L[k].E) < 5.0 * std::max(F, Fj)) resolved = false;
        }
        if (!resolved) { lr.why = "не разрешена"; continue; }
        PeakRes po = peak_area(open, lr.E, F), ps = peak_area(samp, lr.E, F);
        lr.N_open = po.net; lr.N_samp = ps.net;
        if (po.net < 100 || ps.net < 30) { lr.why = "мало отсчётов"; continue; }
        double ratio = ps.net / po.net;
        lr.A = -std::log(ratio);
        lr.sA = std::sqrt((ps.sig / ps.net) * (ps.sig / ps.net) + (po.sig / po.net) * (po.sig / po.net));
        lr.mu_meas = lr.A / x; lr.s_mu = lr.sA / x;
        lr.used = true; lr.why = "ok";
        ++R.nused;
    }
    const int nc = (int)cand.size();
    if (R.nused < 2 || nc < 1) return R;
    std::vector<std::vector<double>> A; std::vector<double> b;
    double mx = 0;
    for (size_t k = 0; k < L.size(); ++k) if (R.lines[k].used) {
        std::vector<double> row(nc);
        for (int j = 0; j < nc; ++j) { row[j] = mu_el(el[cand[j]], L[k].E / 1000.0) / R.lines[k].s_mu; mx = std::max(mx, std::fabs(row[j])); }
        A.push_back(row); b.push_back(R.lines[k].mu_meas / R.lines[k].s_mu);
    }
    A.push_back(std::vector<double>(nc, 50.0 * mx)); b.push_back(50.0 * mx);   // условие sum w = 1
    // диагностика различимости: перенос доли f массы с элемента a на c меняет chi^2 на f^2 * D2,
    // D2 = sum_k ((mu_a - mu_c)/sigma_k)^2 ; минимальный заметный (3 sigma) перенос f = 3/sqrt(D2)
    for (int a = 0; a < nc; ++a) for (int c = a + 1; c < nc; ++c) {
        double d2 = 0;
        for (size_t r = 0; r < A.size(); ++r) { double d = A[r][a] - A[r][c]; d2 += d * d; }
        double f = d2 > 0 ? 3.0 / std::sqrt(d2) : 1e9;
        if (f > 0.03) R.weak.push_back({cand[a], cand[c], f});
    }
    std::vector<double> sol = nnls(A, b);
    double sum = 0; for (double v : sol) sum += v;
    if (sum <= 0) return R;
    for (int j = 0; j < nc; ++j) R.w[cand[j]] = sol[j] / sum;
    int nnz = 0; for (int j = 0; j < nc; ++j) if (R.w[cand[j]] > 1e-9) ++nnz;
    for (size_t k = 0; k < L.size(); ++k) if (R.lines[k].used) {
        double mm = 0; for (int j = 0; j < nc; ++j) mm += R.w[cand[j]] * mu_el(el[cand[j]], L[k].E / 1000.0);
        double d = (R.lines[k].mu_meas - mm) / R.lines[k].s_mu; R.chi2 += d * d;
    }
    R.ndf = std::max(1, R.nused - std::max(nnz - 1, 0));   // число линий минус число свободных параметров
    R.ok = true;
    return R;
}

}  // namespace gs
