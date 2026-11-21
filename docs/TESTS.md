# Pruebas

## Cómo ejecutarlas

```bash
./build/desktop/titan_tests        # todas
./build/desktop/titan_tests escena # solo las que contienen "escena"
./build/desktop/titan_tests -v     # mostrando también el registro del motor
```

Arnés propio y mínimo (`tests/Test.h`), sin dependencias. Las pruebas que tocan disco usan carpetas temporales nuevas.

## Resultado actual (2026-11-21, Linux x86_64, GCC, Release)

```
[ OK ] math: Euler <-> cuaternión ida y vuelta
[ OK ] math: rotar 90 grados en Y lleva -Z a -X
[ OK ] math: TRS por su inversa es la identidad
[ OK ] math: descomponer recupera T, R y S
[ OK ] math: proyección ortográfica lleva los bordes a -1..1
[ OK ] ecs: crear, añadir, leer y quitar componentes
[ OK ] ecs: una entidad destruida invalida sus copias aunque el índice se reutilice
[ OK ] ecs: each solo visita las entidades con todos los componentes
[ OK ] ecs: destruir la entidad visitada dentro de each es seguro
[ OK ] ecs: 100 000 entidades se crean y destruyen sin fugas de índices
[ OK ] escena: crear entidades con id, nombre y transformación
[ OK ] escena: no se aceptan ids repetidos
[ OK ] jerarquía: la matriz de mundo combina las de los padres
[ OK ] jerarquía: solo se recalculan las ramas modificadas
[ OK ] jerarquía: cambiar de padre conserva la posición en el mundo
[ OK ] jerarquía: se rechazan los ciclos
[ OK ] jerarquía: el orden de los hijos se conserva
[ OK ] escena: destruir es diferido y arrastra a los hijos
[ OK ] escena: activa en la jerarquía depende de los antepasados
[ OK ] eventos: crear y destruir entidades emite eventos
[ OK ] eventos: la suscripción se da de baja al destruirse
[ OK ] eventos: darse de baja durante la emisión es seguro
[ OK ] eventos: una suscripción que sobrevive al bus no falla
[ OK ] serialización: guardar y cargar conserva entidades, jerarquía y componentes
[ OK ] serialización: los componentes desconocidos se conservan
[ OK ] serialización: datos incorrectos dan avisos, no se cuelga ni inventa
[ OK ] serialización: se rechazan archivos ajenos, rotos y de versiones futuras
[ OK ] archivos: guardado atómico con copia de seguridad y recuperación
[ OK ] proyecto: crear, guardar escena, cerrar y volver a abrir (demostración técnica)
[ OK ] recursos: los ids sobreviven a mover un archivo con su .meta
[ OK ] recursos: un archivo copiado con su .meta recibe un id nuevo
[ OK ] paquete: detecta referencias rotas y genera el índice si todo es correcto
[ OK ] proyecto: se rechaza un proyecto de una versión futura sin tocarlo
[ OK ] scripts: ciclo de vida onCreate, onStart, onUpdate, onFixedUpdate, onLateUpdate, onDestroy
[ OK ] scripts: mueven entidades reales de la escena
[ OK ] scripts: API de entidades, componentes y jerarquía
[ OK ] scripts: los errores indican archivo, línea y función, y no detienen a los demás
[ OK ] scripts: errores de sintaxis y scripts que no devuelven tabla
[ OK ] scripts: un bucle infinito se corta por tiempo y el sistema sigue funcionando
[ OK ] scripts: límite de memoria
[ OK ] scripts: sin acceso al sistema (io, os, load, require)
[ OK ] scripts: usar una entidad destruida da un error claro
[ OK ] scripts: setActive llama onDisable/onEnable y detiene onUpdate

43 pruebas, 0 fallidas (178.4 ms)
```

## Pruebas de integración ejecutadas a mano en esta sesión

| Prueba | Cómo | Resultado |
|---|---|---|
| Demostración técnica: crear → modificar → cerrar → reabrir | `titan demo`, `titan set`, `add-entity`, `add-component`, y luego `tree` e `inspect` en procesos nuevos | Correcto: las entidades, la jerarquía, los componentes y las referencias se recuperan |
| Valor no válido en `titan set` | `titan set ... Transform.scale grande` | Rechazado con código 1; el archivo no cambia |
| Paquete y runtime | `titan package` + `titan_player` con Xvfb y captura | La escena se dibuja; los scripts mueven la nave; el hijo sigue al padre; 0 errores de script |
| Cambio de escena desde scripts | `TITAN_TAPS` con 3 toques simulados | `principal` → `nivel2` → `principal` a los 2 s, según el registro |
| APK | `gradlew assembleRelease`, `apksigner verify`, `aapt2 dump badging`, `llvm-nm`, `llvm-readelf` | Firmado; 3 ABI; paquete incluido; `ANativeActivity_onCreate` exportado; solo depende de bibliotecas del sistema |

## Mediciones

| Medida | Valor | Condiciones |
|---|---|---|
| Crear y destruir 100 000 entidades con un componente, y volver a crearlas | incluido en ~30 ms del total de la prueba | Linux x86_64, Release |
| Fotograma de la demo (30 entidades, 146 sprites incluido el texto) | ~1,6 ms CPU, ~290 FPS | **Render por software (llvmpipe) en un contenedor sin GPU: no representa a un teléfono** |

No se publicarán cifras de rendimiento en Android hasta medirlas en dispositivos reales.

## Qué NO está probado

- Nada se ha ejecutado aún en un dispositivo o emulador Android.
- La cámara en perspectiva solo se ha compilado.
- La recuperación tras perder el contexto GL no se puede provocar en escritorio.
- El módulo `Log` no tiene pruebas propias (se usa en todas las demás).
