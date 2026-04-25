#ifndef ML_FEATURES_H
#define ML_FEATURES_H

// 17-feature extractor for CSI motion detection MLP (17 -> 18 -> 9 -> 1).
// Ported from espectre (Francesco Pace, GPLv3) components/espectre/ml_features.h.
// Trained with DSER/PLCR features; test F1 = 0.852.
//
// Features (must match training order in train_ml_model.py):
//   0 turb_mean       1 turb_std        2 turb_max       3 turb_min
//   4 turb_zcr        5 turb_skewness   6 turb_kurtosis  7 turb_entropy
//   8 turb_autocorr   9 turb_mad       10 turb_slope    11 waveform_length
//  12 phase_turbulence  13 ratio_turbulence  14 breathing_score
//  15 dser              16 plcr

#include <cstdint>
#include <cmath>
#include <algorithm>

namespace csi_ml {

constexpr uint8_t  ML_NUM_FEATURES  = 17;
constexpr uint8_t  ML_ENTROPY_BINS  = 10;
constexpr uint16_t ML_MAX_SORT_SIZE = 200;

inline float median_sort(float* arr, uint16_t size) {
    if (size == 0 || !arr) return 0.0f;
    std::sort(arr, arr + size);
    if (size % 2 == 0) return (arr[size / 2 - 1] + arr[size / 2]) / 2.0f;
    return arr[size / 2];
}

inline float calc_skewness(const float* v, uint16_t n, float mean, float std_dev) {
    if (n < 3 || std_dev < 1e-10f) return 0.0f;
    float m3 = 0.0f;
    for (uint16_t i = 0; i < n; i++) {
        float d = v[i] - mean;
        m3 += d * d * d;
    }
    m3 /= n;
    return m3 / (std_dev * std_dev * std_dev);
}

inline float calc_kurtosis(const float* v, uint16_t n, float mean, float std_dev) {
    if (n < 4 || std_dev < 1e-10f) return 0.0f;
    float m4 = 0.0f;
    for (uint16_t i = 0; i < n; i++) {
        float d = v[i] - mean;
        float d2 = d * d;
        m4 += d2 * d2;
    }
    m4 /= n;
    float s4 = std_dev * std_dev * std_dev * std_dev;
    return (m4 / s4) - 3.0f;
}

inline float calc_entropy(const float* v, uint16_t n) {
    if (n < 2) return 0.0f;
    float mn = v[0], mx = v[0];
    for (uint16_t i = 1; i < n; i++) {
        if (v[i] < mn) mn = v[i];
        if (v[i] > mx) mx = v[i];
    }
    float range = mx - mn;
    if (range < 1e-10f) return 0.0f;
    uint16_t bins[ML_ENTROPY_BINS] = {0};
    float bw = range / ML_ENTROPY_BINS;
    for (uint16_t i = 0; i < n; i++) {
        int idx = static_cast<int>((v[i] - mn) / bw);
        if (idx >= ML_ENTROPY_BINS) idx = ML_ENTROPY_BINS - 1;
        bins[idx]++;
    }
    float h = 0.0f;
    float log2 = std::log(2.0f);
    for (uint8_t i = 0; i < ML_ENTROPY_BINS; i++) {
        if (bins[i] > 0) {
            float p = static_cast<float>(bins[i]) / n;
            h -= p * std::log(p) / log2;
        }
    }
    return h;
}

inline float calc_zcr(const float* v, uint16_t n, float mean) {
    if (n < 2) return 0.0f;
    uint16_t crossings = 0;
    bool prev_above = v[0] >= mean;
    for (uint16_t i = 1; i < n; i++) {
        bool curr_above = v[i] >= mean;
        if (curr_above != prev_above) crossings++;
        prev_above = curr_above;
    }
    return static_cast<float>(crossings) / (n - 1);
}

inline float calc_autocorr(const float* v, uint16_t n, float mean, float var, uint16_t lag = 1) {
    if (n < lag + 2 || var < 1e-10f) return 0.0f;
    float acov = 0.0f;
    for (uint16_t i = 0; i < n - lag; i++) {
        acov += (v[i] - mean) * (v[i + lag] - mean);
    }
    acov /= (n - lag);
    return acov / var;
}

inline float calc_mad(const float* v, uint16_t n) {
    if (n < 2 || n > ML_MAX_SORT_SIZE) return 0.0f;
    float sorted[ML_MAX_SORT_SIZE];
    for (uint16_t i = 0; i < n; i++) sorted[i] = v[i];
    float median = median_sort(sorted, n);
    float devs[ML_MAX_SORT_SIZE];
    for (uint16_t i = 0; i < n; i++) devs[i] = std::fabs(v[i] - median);
    return median_sort(devs, n);
}

inline float calc_waveform_length(const float* v, uint16_t n) {
    if (n < 2 || v == nullptr) return 0.0f;
    float total = 0.0f;
    float prev = v[0];
    for (uint16_t i = 1; i < n; i++) {
        total += std::fabs(v[i] - prev);
        prev = v[i];
    }
    return total;
}

inline void extract_ml_features(const float* turb, uint16_t turb_count,
                                float phase_turb, float ratio_turb, float breath,
                                float dser, float plcr,
                                float* out) {
    for (uint8_t i = 0; i < ML_NUM_FEATURES; i++) out[i] = 0.0f;
    if (turb_count < 2) return;

    float sum = 0.0f, mn = turb[0], mx = turb[0];
    for (uint16_t i = 0; i < turb_count; i++) {
        float x = turb[i];
        sum += x;
        if (x < mn) mn = x;
        if (x > mx) mx = x;
    }
    float mean = sum / turb_count;

    float var_sum = 0.0f;
    for (uint16_t i = 0; i < turb_count; i++) {
        float d = turb[i] - mean;
        var_sum += d * d;
    }
    float var = var_sum / turb_count;
    float std_dev = std::sqrt(var);

    float zcr      = calc_zcr(turb, turb_count, mean);
    float skew     = calc_skewness(turb, turb_count, mean, std_dev);
    float kurt     = calc_kurtosis(turb, turb_count, mean, std_dev);
    float entropy  = calc_entropy(turb, turb_count);
    float autocorr = calc_autocorr(turb, turb_count, mean, var, 1);
    float mad      = calc_mad(turb, turb_count);

    float mean_i = (turb_count - 1) / 2.0f;
    float num = 0.0f, den = 0.0f;
    for (uint16_t i = 0; i < turb_count; i++) {
        float di = i - mean_i;
        float dx = turb[i] - mean;
        num += di * dx;
        den += di * di;
    }
    float slope = (den > 0.0f) ? (num / den) : 0.0f;
    float wfl = calc_waveform_length(turb, turb_count);

    out[0]  = mean;
    out[1]  = std_dev;
    out[2]  = mx;
    out[3]  = mn;
    out[4]  = zcr;
    out[5]  = skew;
    out[6]  = kurt;
    out[7]  = entropy;
    out[8]  = autocorr;
    out[9]  = mad;
    out[10] = slope;
    out[11] = wfl;
    out[12] = phase_turb;
    out[13] = ratio_turb;
    out[14] = breath;
    out[15] = dser;
    out[16] = plcr;
}

}  // namespace csi_ml

#endif  // ML_FEATURES_H
