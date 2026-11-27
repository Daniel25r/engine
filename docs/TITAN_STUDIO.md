# Titán Studio — herramientas de creación 3D dentro del motor

Objetivo: que en Titán se pueda **modelar, esculpir, texturizar, animar y renderizar**, como en Blender, y que todo
llegue directamente al juego (modelas → pulsas Jugar). Referencia de capacidades: Blender 4.x. **No se copia código
de Blender**: es GPL y haría GPL a Titán y a todos los juegos exportados. Todo se escribe para Titán (o con
bibliotecas de licencia permisiva, registradas en `TITAN_ENGINE_DEPENDENCIES.md`).

Regla: cada herramienta se marca como hecha solo cuando funciona de verdad, guarda su resultado y tiene pruebas.

## 1. Inventario de Blender y plan para Titán

| Área de Blender | Qué tiene Blender | Plan en Titán | Fase |
|---|---|---|---|
| **Modelado (Edit Mode)** | Vértices/aristas/caras, mover/rotar/escalar, extruir, inset, bevel, loop cut, knife, bridge, fill, merge, subdividir, separar/unir, simetría, snapping, proporcional | Malla editable propia (`EditMesh`): primitivas, extruir, inset, subdividir (lineal y Catmull-Clark), borrar, fusionar, voltear, simetría; luego modo edición en el visor, loop cut, bevel, knife | 1 |
| **Modificadores** | Mirror, Subdivision, Array, Bevel, Solidify, Boolean, Decimate, Remesh, Shrinkwrap... (~50) | Pila de modificadores no destructivos sobre `EditMesh` (mirror, subdivisión, array, solidify primero) | 3 |
| **Escultura** | Pinceles (draw, clay, inflate, grab, smooth, crease, pinch, flatten...), dyntopo, multires, remesh voxel, máscaras | Reutilizar los pinceles del terreno sobre mallas (subir, bajar, suavizar, aplanar) + agarrar, inflar, pellizcar; luego remallado | 2 |
| **UV y texturas** | Desplegado (unwrap, smart project), editor UV, pintura de texturas, bake | Proyección por caja/cilindro/esfera (hecho: caja automática), desplegado por costuras, pintura sobre el modelo, horneado de AO | 4 |
| **Materiales/shaders** | Nodos (Principled BSDF...) | PBR actual + editor por nodos (ROADMAP etapa 3) | 4 |
| **Rigging** | Armaduras, pesos automáticos, IK, restricciones | Esqueletos en el editor, pesos por calor/distancia, IK de dos huesos | 5 |
| **Animación** | Línea de tiempo, fotogramas clave, Graph Editor, Dope Sheet, NLA, drivers, shape keys | Pistas de transformación con claves, curvas, hoja de claves, reproducción en el juego; luego shape keys | 3 |
| **Render** | Cycles (trazado de rayos), Eevee (tiempo real), Workbench | El render en tiempo real del motor + "Render a imagen/vídeo" desde una cámara; trazado de rayos por CPU como fase posterior | 6 |
| **Simulación** | Partículas, pelo, tela, fluidos, humo, cuerpos rígidos/blandos | Partículas y cuerpos rígidos ya existen (motor); tela y cuerpos blandos vía Jolt | 6 |
| **Geometry Nodes** | Modelado procedural por nodos | Generación procedural (ROADMAP etapa 5) | 7 |
| **Grease Pencil** | Dibujo 2D en 3D | Fuera de alcance por ahora | — |
| **Importar/exportar** | glTF, FBX, OBJ, STL, PLY, USD, Alembic, Collada... | Ver sección 2 | 1–3 |

## 2. Formatos

Todo modelo importado se **convierte a GLB** dentro del proyecto: el render, la física, la animación y la exportación
ya trabajan con glTF, así que cada formato nuevo funciona en todo el motor sin rutas especiales.

| Formato | De dónde viene | Cómo | Estado |
|---|---|---|---|
| glTF / GLB | Blender, Maya, casi todos | Nativo (cgltf) | ✅ (mallas, materiales, esqueletos, animaciones) |
| OBJ + MTL | Prisma 3D, Blender, ZBrush, casi todos | Lector propio → GLB (materiales, color, brillo, texturas PNG/JPG incrustadas, cuadriláteros y n-gonos, índices negativos) | ✅ probado |
| STL | Impresión 3D, Prisma 3D, CAD | Lector propio (ASCII y binario) → GLB, de Z arriba a Y arriba | ✅ probado |
| PLY | Escáneres 3D, fotogrametría | Lector propio (ASCII y binario, little y big endian) → GLB. Colores por vértice: todavía no | ✅ probado (sin colores) |
| .blend | Blender | Se usa el Blender instalado (`blender --background`) para exportar a GLB, como hace Godot. Blender se ejecuta como programa aparte: no hay problema de licencia | 🔨 escrito, **sin probar** (aquí no hay Blender instalado) |
| FBX | Maya, 3ds Max, Prisma 3D, Mixamo | Biblioteca ufbx (MIT) → GLB con esqueletos y animaciones | Fase 2 |
| SMD / DMX | Source Filmmaker, Source Engine | Lector propio de SMD (texto: malla, esqueleto, animación) → GLB; DMX después | Fase 3 |
| DAE (Collada) | Herramientas antiguas | Pendiente de valorar | — |

## 3. Estado

| Pieza | Estado |
|---|---|
| Escritor GLB propio (`GlbWriter`): posiciones, normales, UV, índices de 32 bits, materiales PBR, texturas incrustadas | ✅ (el GLB se valida con cgltf en las pruebas) |
| Malla editable `EditMesh`: cubo, plano, cilindro, cono, esfera UV; extruir región, inset, subdivisión lineal y Catmull-Clark, borrar caras/vértices, fusionar por distancia, voltear, simetría; guardar `.tmesh` | ✅ núcleo con 5 pruebas geométricas (malla cerrada, Euler, normales hacia fuera) |
| Importación en el editor (Contenido › Importar) y `titan import`: OBJ/STL/PLY/.blend → GLB | ✅ OBJ y STL probados de principio a fin (importar, colocar, ver en el reproductor) |
| **Modo Modelar** (Detalles › "Modelar / Esculpir" o Tab): vértices (1) y caras (3) con el ratón, Mayús para añadir, arrastrar para mover, E extruir, I inset, X borrar, A todo, subdividir, Catmull-Clark, simetría X/Y/Z, voltear, fusionar, color/metal/rugosidad, sombreado suave, deshacer propio (Ctrl+Z), exportar GLB y OBJ | ✅ probado en el editor con clics simulados (cara seleccionada y extruida dos veces, guardada en .tmesh) |
| **Modo Esculpir**: pinceles Dibujar, Inflar, Suavizar, Aplanar, Pellizcar y Agarrar; radio y fuerza; Ctrl hunde; círculo del pincel sobre la superficie | ✅ probado en el editor con trazos simulados sobre una esfera de 6144 caras |
| Crear › Modelo editable: cubo, esfera, cilindro, cono, plano y "Cabeza (esfera densa)" para esculpir | ✅ |
| Convertir un modelo importado en malla editable (se crea una copia en models/; el original no se toca) | ✅ forma; ⚠️ todavía pierde texturas, UV y huesos |
| **Modo Esqueleto** (rigging): esqueleto humano automático (19 huesos ajustados a la caja de la malla) o un hueso raíz; añadir hijo, renombrar, mover cabeza/cola, borrar hueso (sus hijos pasan al padre), pesos automáticos por distancia a los huesos (4 por vértice) | ✅ probado en el editor (19 huesos, todos los vértices con peso) y con prueba de pesos y deformación |
| **Modo Animar**: clips con nombre, duración y bucle; línea de tiempo con marcas de claves; rotación (y traslación del hueso raíz) con clave automática; insertar/borrar clave; reproducir; vista previa deformada en el visor | ✅ probado en el editor (clip nuevo, hueso elegido con clic) |
| Exportación del rig: el GLB lleva JOINTS_0/WEIGHTS_0, nodos de huesos, skin con matrices inversas y animaciones; el motor lo reproduce con `Animator` | ✅ prueba de ida y vuelta con cgltf |
| FBX (Mixamo, Prisma 3D), SMD/DMX (Source Filmmaker), retopología, IK | ▶ siguientes bloques, en este orden: FBX (ufbx, MIT), SMD, retopología |
