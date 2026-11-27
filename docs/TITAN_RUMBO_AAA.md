# Titán: rumbo al nivel AAA (plan real)

Unreal Engine 5 es el resultado de décadas de trabajo de cientos de ingenieros. La forma real de acercarse no es
copiarlo: es una arquitectura propia (la de Titán) construida sobre piezas profesionales probadas, como hacen los
estudios. Titán ya lo hace con Jolt Physics. Este documento ordena lo que falta por impacto y dice con qué se haría.
Solo se apuntan bibliotecas con licencia compatible (MIT, BSD, Apache 2.0, zlib); nada GPL.

## Hecho (2026-11)

- Render: PBR, IBL del cielo (split-sum), SSAO, SSR, barniz, sombras en 4 cascadas, bloom, FXAA, gradación.
- Mundo de bloques infinito con streaming en varios hilos; terreno esculpible; Jolt; Lua; audio con mezclador.
- Editor con modelado, escultura, rigging y animación por claves.

## Siguiente, por impacto

| # | Bloque | Cómo | Licencia |
|---|---|---|---|
| 1 | ~~Editor de materiales por nodos~~ ✅ hecho (2026-11-27). Siguiente: salida de normal, grafos en .mat compartidos | — | propio |
| 2 | ~~Importar FBX~~ ✅ hecho (2026-11-27). Animaciones de otros archivos por nombre de hueso ✅. Siguiente: mapa de huesos entre esqueletos distintos | — | MIT |
| 3 | ~~TAA~~ ✅ hecho (2026-11-27). HDR y exposición automática también ✅. Falta: vectores de movimiento por objeto | — | propio |
| 4 | ~~Sonda de reflexión~~ ✅ hecho (2026-11-27). Falta: varias sondas con mezcla, corrección de paralaje, prefiltrado GGX | — | propio |
| 5 | ~~LOD automático~~ ✅ hecho (2026-11-27) con meshoptimizer. Falta: LOD para las primitivas instanciadas y el mundo de bloques | — | MIT |
| 6 | Navegación con navmesh real | Recast/Detour (sustituye la rejilla) | zlib |
| 7 | Backend Vulkan / render moderno | Filament (render PBR de Google) o un backend propio sobre Vulkan | Apache 2.0 |
| 8 | ~~Red multijugador~~ ✅ base hecha (2026-11-27) con ENet y `Net.*`. Falta: replicación automática, predicción, servidor dedicado | — | MIT |
| 9 | GI en tiempo real | DDGI (sondas con trazado) cuando haya Vulkan | propio |

Cada bloque se entrega igual que hasta ahora: integrado, probado (pruebas automáticas + capturas), documentado y
sin romper proyectos anteriores.
