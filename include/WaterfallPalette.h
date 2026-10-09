#pragma once
#include <QColor>
#include <algorithm>
#include <array>
#include <cmath>

inline QRgb waterfallSpectrumColor(float level) {
    struct Stop { float position; int r,g,b; };
    constexpr std::array<Stop,6> stops{{{0,0,8,48},{.25f,0,55,220},{.45f,0,220,255},{.65f,20,235,65},{.82f,255,230,0},{1,255,30,0}}};
    level=std::isfinite(level) ? std::clamp(level,0.0f,1.0f) : 0;
    for(size_t i=1;i<stops.size();++i) if(level<=stops[i].position) {
        const auto& a=stops[i-1]; const auto& b=stops[i];
        const float t=(level-a.position)/(b.position-a.position);
        return qRgb(static_cast<int>(a.r+(b.r-a.r)*t),static_cast<int>(a.g+(b.g-a.g)*t),static_cast<int>(a.b+(b.b-a.b)*t));
    }
    return qRgb(255,30,0);
}
