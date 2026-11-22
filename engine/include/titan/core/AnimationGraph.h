#pragma once
// Máquina de estados de animación: decide qué clip suena según parámetros (velocidad, en el suelo, disparar...)
// y prepara el Animator (clip, tiempo, transición, mezcla 1D) para que el render calcule la pose. Sin GPU.
#include <functional>
#include <string>

#include "titan/core/Components.h"

namespace titan {

// Duración en segundos de un clip por nombre (0 si no existe). La da el modelo del Renderer.
using ClipDuration = std::function<float(const std::string& clip)>;

// Avanza un Animator: con máquina de estados la evalúa; sin ella, igual que advanceAnimator.
void updateAnimator(Animator& a, float dt, const ClipDuration& duration);

// Parámetros (los triggers se consumen al usarse en una transición).
bool setAnimParam(Animator& a, const std::string& name, float value);
bool setAnimTrigger(Animator& a, const std::string& name);
float animParam(const Animator& a, const std::string& name);

} // namespace titan
