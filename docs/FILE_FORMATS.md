# Formatos de archivo

Todos son JSON en UTF-8 con dos campos obligatorios: `format` (qué es) y `version` (entero).
Reglas comunes:

- Un archivo de una versión **mayor** que la que entiende el motor se rechaza y no se modifica.
- Uno de versión menor pasa por las migraciones registradas, en memoria. El original no se toca hasta que se guarda,
  y al guardar queda la versión anterior en `.bak`.
- Todo se guarda de forma atómica (`.tmp` → `rename`), y escenas y proyecto conservan además una copia `.bak`.
- Los números decimales se escriben con el mínimo de cifras que reproduce el `float` exacto.

## Proyecto — `project.titan`

```json
{
  "format": "titan.project",
  "version": 1,
  "engineVersion": "0.2.0",
  "name": "Demostración técnica",
  "dimension": "2d",
  "startScene": "9eee7aa9ec4e0d68"
}
```

`startScene` es el id del recurso de la escena inicial (ver `.meta`). `dimension`: `"2d"` o `"3d"`.

## Escena — `*.scene` (versión 1)

```json
{
  "format": "titan.scene",
  "version": 1,
  "engineVersion": "0.2.0",
  "name": "principal",
  "entities": [
    {
      "id": "3f2a9c0d11e4b7a5",
      "name": "Nave",
      "active": true,
      "parent": "a01b2c3d4e5f6071",
      "components": {
        "Transform": { "position": [2.5, 0, 0], "rotation": [0, 0, 0, 1], "scale": [1, 1, 1] },
        "SpriteRenderer": { "texture": "1a4170d0427f1462", "color": [0.37, 0.82, 0.78, 1], "size": [1, 1],
                            "pivot": [0.5, 0.5], "uv": [0, 0, 1, 1], "sortingOrder": 2, "visible": true },
        "Script": { "script": "553108b0186ef7a9", "enabled": true }
      }
    }
  ]
}
```

- `entities` va en orden de recorrido en profundidad: los padres antes que los hijos, y los hermanos en su orden.
- `id`: 16 caracteres hexadecimales, único en la escena. Si falta o se repite, se genera otro y se avisa.
- `parent`: id de otra entidad de la misma escena, o `null`. Si no existe, la entidad queda como raíz y se avisa.
- `rotation` es un cuaternión `[x, y, z, w]`.
- Referencias a recursos: id del `.meta`, o `null`.
- Los componentes de tipo desconocido se conservan tal cual al volver a guardar.
- Un campo que falta toma su valor por defecto. Un campo con un tipo incorrecto genera un aviso y toma su valor por defecto.

### Componentes de la versión 1

| Componente | Campos |
|---|---|
| `Transform` | `position` [3], `rotation` [4], `scale` [3] |
| `Camera` | `projection` ("orthographic"/"perspective"), `orthographicSize`, `fieldOfView`, `near`, `far`, `priority`, `clearColor` [4] |
| `SpriteRenderer` | `texture`, `color` [4], `size` [2], `pivot` [2], `uv` [4], `sortingOrder`, `visible` |
| `Script` | `script` (recurso .lua), `enabled`, `props` (objeto) |
| `MeshRenderer` | `mesh`, `model`, `material` (recurso .mat), `castShadows`, `visible` y los campos de material: `texture`, `color`, `metallic`, `roughness`, `emission`, `prototypeGrid`, `unlit`, `normalMap`, `normalStrength`, `metallicRoughnessMap`, `emissionMap`, `occlusionMap`, `tiling` [2], `offset` [2], `alphaClip`, `alphaCutoff`, `doubleSided` |
| `Light` | `type` ("directional"/"point"/"spot"), `color`, `intensity`, `range`, `spotAngle`, `spotSoftness` |
| `Environment` | cielo, niebla, sombras y posprocesado: `bloom`, `bloomIntensity`, `bloomThreshold`, `vignette`, `contrast`, `saturation`, `temperature` |
| `Animator` | `clip`, `playing`, `loop`, `speed`, `fadeDuration`, `machine` (opcional: `params`, `states` con `blend` y `events`, `transitions` con `conditions`) |
| `AudioSource` | `clip`, `volume`, `pitch`, `loop`, `playOnStart`, `spatial`, `minDistance`, `maxDistance` |
| `Rigidbody`, `Collider` | ver ENGINE_API; `Collider.layer` 0..15 |
| `CharacterController` | `height`, `radius`, `maxSlope`, `stepHeight`, `gravityScale`, `layer` |
| `Joint` | `type` ("fixed"/"hinge"/"distance"), `connected` (id de entidad o null), `anchor`, `axis`, `limitAngle`, `minAngle`, `maxAngle`, `minDistance`, `maxDistance`, `springFrequency`, `springDamping` |
| `ParticleSystem` | emisión, vida, velocidad, forma, aceleración, tamaño y color inicial/final, textura con hoja de cuadros, `additive`, `worldSpace` |
| `Terrain` | `size`, `maxHeight`, `resolution`, `layerTiling`, `layers` [4 × {texture, color}], `heights` (base64 de uint16 little endian, resolution²), `splat` (base64, resolution² × 4 pesos) |
| `UIRect`, `UIImage`, `UIText`, `UIButton`, `UIJoystick` | ver ENGINE_API |

Los campos nuevos de 0.13 tienen valores por defecto que reproducen el aspecto anterior: una escena de 0.12 se carga
igual, sin migración.

Añadir componentes nuevos no cambia la versión del formato: un motor antiguo los conserva como desconocidos.

## Material — `*.mat`

```json
{ "format": "titan.material", "version": 1, "engineVersion": "0.13.0",
  "texture": "a1b2...", "color": [1, 1, 1, 1], "metallic": 0, "roughness": 0.6, "normalMap": null, "tiling": [1, 1], ... }
```

Los mismos campos de material que `MeshRenderer`. Un MeshRenderer con `material` usa los del archivo. El paquete comprueba
que se puede leer y que sus texturas existen.

## Mezclador — `assets/audio_buses.json` (versión 1, opcional)

Sin este archivo solo existe Master, sin efectos (los proyectos anteriores suenan igual). Lo escribe el editor.

```json
{
  "version": 1,
  "buses": [
    { "name": "Master", "send": "Master", "volume_db": 0, "mute": false, "bypass_effects": false,
      "effects": [ { "type": "limiter", "enabled": true, "params": {} } ] },
    { "name": "Efectos", "send": "Master", "volume_db": -3, "mute": false, "bypass_effects": false,
      "effects": [ { "type": "reverb", "enabled": true, "params": { "wet": 0.18, "room_size": 0.35 } } ] }
  ]
}
```

Al cargar se normaliza: Master va primero (se crea si falta), los nombres repetidos se renombran ("A 2"), los
destinos que no existen o forman un ciclo pasan a Master, y los efectos de tipo desconocido se ignoran. En `params`
solo hace falta lo que cambia respecto al valor por defecto. `AudioSource` guarda `"bus"` solo si no es Master.

## Partida guardada — `partida.json`

`{"format": "titan.save", "version": 1, "data": { ... }}`, escrito de forma atómica con copia `.bak`. Lo crea `Save.*`
en el almacenamiento privado de la app (Android), junto al paquete (reproductor de escritorio) o en `.titan/` del
proyecto (Jugar en el editor; no se empaqueta).

## Mundo de bloques — `mundos/<nombre>/`

Junto a la partida guardada (en el editor, `<proyecto>/.titan/mundos/<nombre>/`).

- `world.json`: `{"format": "titan-voxel-world", "version": 1, "seed", "seaLevel", "terrainScale", "caves", "trees"}`.
  Si existe, manda sobre los ajustes del componente (el mundo sigue siendo el mismo).
- `chunks/<cx>_<cz>.chunk`: solo los chunks que el jugador cambió. Binario: `"TVC1"` y los
  16 384 bloques (orden y, z, x) en series `(longitud 1-255, id)`. Un archivo dañado o incompleto se rechaza y el
  chunk se vuelve a generar. Se escribe de forma atómica.

## Metadatos de recurso — `<archivo>.meta`

```json
{ "format": "titan.meta", "version": 1, "id": "1a4170d0427f1462", "type": "texture",
  "importSettings": { "filter": "linear", "mipmaps": true } }
```

Tipos: `scene`, `texture` (.png .jpg .jpeg), `script` (.lua), `audio` (.wav .ogg .mp3), `model` (.gltf .glb .obj),
`font` (.ttf .otf), `data` (.json), `unknown`. Que un tipo se reconozca **no significa** que el motor ya lo use:
hoy solo se usan escenas, texturas PNG/JPEG y scripts.

Un `.meta` dañado se aparta como `.meta.bak` y se crea uno nuevo. Nunca se borra.

## Paquete de juego (lo que ejecuta el runtime)

```
paquete/
  project.titan
  asset_index.json      {"format":"titan.assetindex","version":1,"assets":[{"id","path","type","importSettings"}]}
  assets/...            los recursos, sin .meta
```

En el APK, el paquete va en la raíz de `assets/`, junto a los recursos del motor (`engine/fonts/`).
