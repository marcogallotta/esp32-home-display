#include "state.h"

namespace salah {

State computeState(const Schedule& today,
                   const Schedule& tomorrow,
                   int nowMinutes) {
    if (nowMinutes < today.fajr) {
        return State{
            Phase::Isha,
            Phase::Fajr,
            today.fajr - nowMinutes
        };
    }

    if (nowMinutes < today.sunrise) {
        return State{
            Phase::Fajr,
            Phase::SunriseMakruh,
            today.sunrise - nowMinutes
        };
    }

    if (nowMinutes < today.duha) {
        return State{
            Phase::SunriseMakruh,
            Phase::Duha,
            today.duha - nowMinutes
        };
    }

    if (nowMinutes < today.dahwaEKubra) {
        return State{
            Phase::Duha,
            Phase::DahwaEKubra,
            today.dahwaEKubra - nowMinutes
        };
    }

    if (nowMinutes < today.zuhr) {
        return State{
            Phase::DahwaEKubra,
            Phase::Zuhr,
            today.zuhr - nowMinutes
        };
    }

    if (nowMinutes < today.asr) {
        return State{
            Phase::Zuhr,
            Phase::Asr,
            today.asr - nowMinutes
        };
    }

    if (nowMinutes < today.asrMakruh) {
        return State{
            Phase::Asr,
            Phase::AsrMakruh,
            // Note: speciall case for Asr, where the "deadline" is actually Maghrib, not AsrMakruh
            today.maghrib - nowMinutes
        };
    }

    if (nowMinutes < today.maghrib) {
        return State{
            Phase::AsrMakruh,
            Phase::Maghrib,
            today.maghrib - nowMinutes
        };
    }

    if (nowMinutes < today.isha) {
        return State{
            Phase::Maghrib,
            Phase::Isha,
            today.isha - nowMinutes
        };
    }

    return State{
        Phase::Isha,
        Phase::Fajr,
        (24 * 60 - nowMinutes) + tomorrow.fajr
    };
}

} // namespace salah
