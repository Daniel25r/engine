#include "titan/core/AnimationGraph.h"

#include <algorithm>
#include <cmath>

namespace titan {

namespace {

bool conditionHolds(AnimStateMachine& m, const AnimCondition& c) {
    const AnimParam* p = m.param(c.param);
    if (!p) return false;
    switch (c.op) {
    case AnimCondition::Op::Greater: return p->value > c.value;
    case AnimCondition::Op::Less: return p->value < c.value;
    case AnimCondition::Op::Equal: return std::fabs(p->value - c.value) < 1e-4f;
    case AnimCondition::Op::NotEqual: return std::fabs(p->value - c.value) >= 1e-4f;
    case AnimCondition::Op::IsTrue: return p->value != 0;
    case AnimCondition::Op::IsFalse: return p->value == 0;
    }
    return false;
}

// Clips del estado y su peso: en mezcla 1D, los dos puntos que rodean al valor del parámetro.
struct Sample {
    std::string a, b;  // b vacío = un solo clip
    float weightB = 0; // peso de b (0..1)
};
Sample sampleOf(AnimStateMachine& m, const AnimState& s) {
    if (s.blend.empty()) return {s.clip, "", 0};
    std::vector<AnimBlendPoint> pts = s.blend;
    std::sort(pts.begin(), pts.end(), [](const AnimBlendPoint& x, const AnimBlendPoint& y) { return x.threshold < y.threshold; });
    const AnimParam* p = m.param(s.blendParam);
    const float v = p ? p->value : 0;
    if (v <= pts.front().threshold || pts.size() == 1) return {pts.front().clip, "", 0};
    if (v >= pts.back().threshold) return {pts.back().clip, "", 0};
    for (size_t i = 0; i + 1 < pts.size(); ++i)
        if (v >= pts[i].threshold && v <= pts[i + 1].threshold) {
            const float span = std::max(1e-6f, pts[i + 1].threshold - pts[i].threshold);
            return {pts[i].clip, pts[i + 1].clip, (v - pts[i].threshold) / span};
        }
    return {pts.back().clip, "", 0};
}

} // namespace

bool setAnimParam(Animator& a, const std::string& name, float value) {
    AnimParam* p = a.machine.param(name);
    if (!p) return false;
    p->value = p->type == AnimParam::Type::Bool || p->type == AnimParam::Type::Trigger ? (value != 0 ? 1.0f : 0.0f)
             : p->type == AnimParam::Type::Int                                       ? std::round(value)
                                                                                      : value;
    return true;
}

bool setAnimTrigger(Animator& a, const std::string& name) { return setAnimParam(a, name, 1); }

float animParam(const Animator& a, const std::string& name) {
    for (const AnimParam& p : a.machine.params)
        if (p.name == name) return p.value;
    return 0;
}

void updateAnimator(Animator& a, float dt, const ClipDuration& duration) {
    AnimStateMachine& m = a.machine;
    if (m.empty()) {
        advanceAnimator(a, dt);
        return;
    }
    m.firedEvents.clear();
    if (!m.state(m.current)) { // primera vez (o el estado ya no existe): estado de entrada
        m.current = m.states.front().name;
        m.normalizedTime = 0;
    }

    // 1. Transiciones: la primera que se cumpla (las de "*" no vuelven al estado en el que ya está).
    const AnimState* cur = m.state(m.current);
    const float frac = m.normalizedTime - std::floor(m.normalizedTime);
    for (const AnimTransition& t : m.transitions) {
        if (t.from != m.current && !(t.from == "*" && t.to != m.current)) continue;
        if (!m.state(t.to)) continue;
        if (t.hasExitTime) {
            // Con bucle: sale cada vez que el ciclo pasa de exitTime (>= 1 = al terminar el primer ciclo).
            const bool exitOk = cur->loop ? (t.exitTime >= 1 ? m.normalizedTime >= t.exitTime : frac >= t.exitTime)
                                          : std::min(m.normalizedTime, 1.0f) >= t.exitTime;
            if (!exitOk) continue;
        }
        bool ok = true;
        for (const AnimCondition& c : t.conditions) ok = ok && conditionHolds(m, c);
        if (!ok) continue;
        for (const AnimCondition& c : t.conditions) // los triggers usados se apagan
            if (AnimParam* p = m.param(c.param); p && p->type == AnimParam::Type::Trigger) p->value = 0;
        // Mezcla desde el clip dominante del estado actual.
        const Sample prev = sampleOf(m, *cur);
        a.fromClip = prev.weightB > 0.5f ? prev.b : prev.a;
        a.fromTime = a.fromClip == a.blendClip && !a.blendClip.empty() ? a.blendTime : a.time;
        a.fromWeight = t.duration > 0 ? 1.0f : 0.0f;
        a.fadeDuration = t.duration;
        m.current = t.to;
        m.normalizedTime = 0;
        cur = m.state(m.current);
        break;
    }

    // 2. Avanzar el tiempo normalizado (en mezcla 1D, con la duración ponderada: los pasos coinciden).
    const Sample s = sampleOf(m, *cur);
    const float da = std::max(0.001f, duration ? duration(s.a) : 1.0f);
    const float db = s.b.empty() ? da : std::max(0.001f, duration ? duration(s.b) : 1.0f);
    const float len = da * (1 - s.weightB) + db * s.weightB;
    const float before = m.normalizedTime;
    if (a.playing) m.normalizedTime += dt * a.speed * cur->speed / len;
    if (!cur->loop) m.normalizedTime = std::min(m.normalizedTime, 1.0f);
    // Eventos que se han cruzado en este paso (también al dar la vuelta en bucle).
    for (const AnimEvent& ev : cur->events)
        for (float k = std::floor(before); k <= std::floor(m.normalizedTime); k += 1)
            if (before < k + ev.time && k + ev.time <= m.normalizedTime) m.firedEvents.push_back(ev.name);
    if (before == 0 && m.normalizedTime > 0) // el evento en 0 al entrar al estado
        for (const AnimEvent& ev : cur->events)
            if (ev.time <= 0) m.firedEvents.push_back(ev.name);

    // 3. Lo que necesita el render: clip principal (b si pesa más), el otro como mezcla, y la transición.
    const float f = cur->loop ? m.normalizedTime - std::floor(m.normalizedTime) : std::min(m.normalizedTime, 1.0f);
    a.loop = cur->loop;
    if (s.b.empty()) {
        a.clip = s.a;
        a.time = f * da;
        a.blendClip.clear();
        a.blendWeight = 0;
    } else {
        a.clip = s.b;
        a.time = f * db;
        a.blendClip = s.a;
        a.blendTime = f * da;
        a.blendWeight = 1 - s.weightB;
    }
    if (a.fromWeight > 0) {
        a.fromTime += dt * a.speed;
        a.fromWeight -= a.fadeDuration > 0 ? dt / a.fadeDuration : 1.0f;
        if (a.fromWeight < 0) a.fromWeight = 0;
    }
}

} // namespace titan
