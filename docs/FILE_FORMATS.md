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
| `Camera` | `projection` ("orthographic"/"perspective"), `orthographicSize` (media altura), `fieldOfView` (grados), `near`, `far`, `priority`, `clearColor` [4] |
| `SpriteRenderer` | `texture` (recurso), `color` [4], `size` [2], `pivot` [2], `uv` [4], `sortingOrder`, `visible` |
| `Script` | `script` (recurso .lua), `enabled` |
| `MeshRenderer` | `mesh` ("cube", "sphere", "plane", "cylinder", "cone", "capsule"), `texture`, `color` [4], `shininess`, `unlit`, `visible` |
| `Light` | `type` ("directional"/"point"), `color` [4], `intensity`, `range` |

Añadir componentes nuevos no cambia la versión del formato: un motor antiguo los conserva como desconocidos.

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

En el APK, el paquete va en la raíz de `assets/`, junto a `engine/font.png`.
