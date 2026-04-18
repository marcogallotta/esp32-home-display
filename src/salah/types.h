#pragma once

namespace salah {

enum class Phase {
    Fajr,
    SunriseMakruh,
    Duha,
    DahwaEKubra,
    Zuhr,
    Asr,
    AsrMakruh,
    Maghrib,
    Isha,
};

struct Schedule {
    int fajr;
    int sunrise;
    int duha;
    int dahwaEKubra;
    int zuhr;
    int asr;
    int asrMakruh;
    int maghrib;
    int isha;
};

} // namespace salah
