# API del motor

## Scripts en Lua

Un script es un archivo `.lua` del proyecto que devuelve una tabla. Cada entidad con un componente `Script`
recibe su propia instancia `self`, y `self.entity` es su entidad.

```lua
local Nave = {}

function Nave:onStart()
    self.velocidad = 3
end

function Nave:onUpdate(dt)
    self.entity:translate(self.velocidad * dt, 0, 0)
    if Input.tapped() then self.entity:setColor(1, 0, 0) end
end

return Nave
```

### Ciclo de vida

| Método | Cuándo |
|---|---|
| `onCreate()` | Al cargar la escena, cuando ya existen todas las instancias |
| `onStart()` | Después de todos los `onCreate`, o antes del primer update si la entidad estaba inactiva |
| `onFixedUpdate(dt)` | 0 a 5 veces por fotograma, con `dt` = 1/60 |
| `onUpdate(dt)` | Una vez por fotograma |
| `onLateUpdate(dt)` | Después de todos los `onUpdate` |
| `onEnable()` / `onDisable()` | Al llamar `setActive` sobre la entidad del script |
| `onDestroy()` | Antes de destruir la entidad o al descargar la escena |

Pendientes (llegarán con la física): `onCollisionEnter`, `onCollisionExit`, `onTriggerEnter`, `onTriggerExit`.

Solo reciben updates las instancias cuyo `Script.enabled` es true y cuya entidad está activa en la jerarquía.
Si un método lanza un error, el error se registra y esa instancia deja de ejecutarse.

### Entidad (`self.entity` u otra entidad)

| Método | Descripción |
|---|---|
| `name()`, `setName(s)`, `id()` | Nombre e id persistente |
| `isValid()` | false si ya fue destruida (cualquier otro método da error en ese caso) |
| `isActive()`, `setActive(b)` | Activa en la jerarquía / activar o desactivar |
| `destroy()` | Destruye la entidad y sus hijos al final de la fase actual |
| `parent()`, `setParent(e o nil, conservarMundo=true)`, `children()` | Jerarquía |
| `getPosition()`, `setPosition(x,y,z)`, `translate(x,y,z)` | Posición local |
| `getWorldPosition()` | Posición en el mundo |
| `getRotation()`, `setRotation(x,y,z)`, `rotate(x,y,z)` | Rotación local en grados (Euler) |
| `getScale()`, `setScale(x,y,z)` o `setScale(s)` | Escala local |
| `forward()`, `right()`, `up()` | Direcciones en el mundo (delante = −Z) |
| `getColor()`, `setColor(r,g,b[,a])`, `setSize(w,h)` | Del `SpriteRenderer` |
| `hasComponent(n)`, `addComponent(n)`, `removeComponent(n)` | Por nombre: `Camera`, `SpriteRenderer`, `MeshRenderer`, `Light`, `Script` |
| `getScript()` | La instancia del script de esa entidad, para que los scripts se comuniquen |

Las funciones que devuelven vectores devuelven varios valores: `local x, y, z = e:getPosition()`.

### Tablas globales

| | |
|---|---|
| `Entity.find(nombre)`, `Entity.get(id)`, `Entity.create(nombre [, padre])` | Buscar y crear entidades |
| `Scene.load(nombre o ruta)`, `Scene.name()` | Cambiar de escena (se aplica al final del fotograma) |
| `Time.delta()`, `Time.fixedDelta()`, `Time.time()`, `Time.frame()` | Tiempo |
| `Input.touchCount()`, `Input.touch(i)` → `{id, x, y, phase}`, `Input.touchWorld(i)` → x,y,z (plano z = 0 en 2D; suelo y = 0 con cámara en perspectiva), `Input.tapped()` | Táctil; `i` empieza en 1; `phase` es "began" o "held" |
| `Screen.width()`, `Screen.height()` | Píxeles |
| `Screen.text(x, y, tamaño, texto [, r, g, b, a])` | Texto en pantalla durante este fotograma (de momento solo ASCII) |
| `Debug.log(...)`, `Debug.warn(...)`, `Debug.error(...)`, `print(...)` | Al registro del motor (logcat en Android) |

Bibliotecas de Lua disponibles: base (sin `load`, `loadfile` ni `dofile`), `string` (sin `dump`), `table`, `math`,
`utf8` y `coroutine`. No hay `io`, `os`, `package`, `require` ni `debug`.

Límites por defecto: 0,25 s por llamada y 256 MB de memoria para todo el intérprete.

## C++ (para el motor, las herramientas y el futuro editor)

```cpp
#include "titan/core/Project.h"
using namespace titan;

auto project = Project::create("MiJuego", "Mi juego", ProjectDimension::TwoD);
ComponentRegistry registry = ComponentRegistry::createDefault();

Scene scene("principal");
Entity cam = scene.createEntity("Cámara");
scene.transform(cam).setPosition({0, 0, 10});
scene.world().add<Camera>(cam);

Entity hero = scene.createEntity("Héroe");
scene.world().add<SpriteRenderer>(hero, {});
Entity sword = scene.createEntity("Espada", hero);   // hijo
scene.transform(sword).setPosition({0.5f, 0, 0});

project->setStartScene(project->saveScene(scene, registry, "scenes/principal.scene"));
project->save();
```

Piezas principales:

- `World`: `create`, `destroy`, `alive`, `add<T>`, `get<T>`, `has<T>`, `remove<T>`, `each<A, B...>(fn)`.
- `Scene`: `createEntity`, `destroyEntity` (diferido), `flushDestroyed`, `setParent`, `children`, `roots`, `traverse`,
  `findById`, `findByName`, `info`, `transform`, `computeWorldMatrix`, `isActiveInHierarchy`.
- `TransformSystem::update(scene)`.
- `EventBus`: `subscribe<E>(fn)` devuelve una `Subscription`; `emit(e)`.
- `ComponentRegistry`: `add(ComponentType)`, `addSimple<T>(nombre, guardar, cargar)` para registrar componentes propios.
- `serializeScene`, `deserializeScene`, `saveSceneFile`, `loadSceneFile` (devuelven un `LoadReport` con errores, avisos y dependencias).
- `Project`: `create`, `open`, `save`, `saveScene`, `loadScene`, `package`, `assets()`.
- `Runtime`: `start(FileSource&)`, `frame(w, h)`, `pause`, `resume`, `shutdown`, `input()`.

## Herramienta `titan`

```
titan new <carpeta> <nombre> [2d|3d]
titan demo <carpeta>
titan tree <carpeta> [--scene ruta]
titan inspect <carpeta> <entidad>
titan set <carpeta> <entidad> <Componente.campo> <valores...>   (también Entity.name / Entity.active)
titan add-entity <carpeta> <nombre> [padre]
titan add-component <carpeta> <entidad> <Componente>
titan assets <carpeta>
titan check <carpeta>
titan package <carpeta> <salida>
```

`set` valida el valor con el mismo cargador que las escenas y no guarda nada si el valor no es válido.
En campos de recurso acepta la ruta (`textures/nave.png`) y la convierte en id.
