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

| `onCollisionEnter(otro)` / `onCollisionExit(otro)` | Empieza / termina un choque físico |
| `onTriggerEnter(otro)` / `onTriggerExit(otro)` | Algo entra / sale de un Collider marcado como trigger |
| `onClick()` | Se ha pulsado el UIButton de esta entidad |
| `onAnimationEvent(nombre)` | El Animator de esta entidad pasó por un evento de su máquina de estados |

Variables editables en Detalles: `local Clase = { props = { velocidad = 5, activo = true, nombre = "x" } }`.
Cada entidad puede cambiar esos valores en el editor; en el script se leen como `self.velocidad`.

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
| `getColor()`, `setColor(r,g,b[,a])` | De su SpriteRenderer, UIImage, UIText o MeshRenderer (el primero que tenga) |
| `setSize(w,h)` | Del `SpriteRenderer` |
| `hasComponent(n)`, `addComponent(n)`, `removeComponent(n)` | Por nombre, cualquier componente: `MeshRenderer`, `Rigidbody`, `UIText`... |
| `getScript()` | La instancia del script de esa entidad, para que los scripts se comuniquen |
| `addForce`, `addImpulse`, `addTorque`, `setVelocity`, `getVelocity`, `teleport` (x,y,z) | Física (necesita Collider + Rigidbody) |
| `playAnimation(clip [, repetir])`, `stopAnimation()`, `getAnimation()` → clip, tiempo, reproduciendo, `setAnimationSpeed(v)` | Animación del modelo (glTF con esqueleto); al cambiar de clip hay transición suave |
| `playSound([sonido, volumen])`, `stopSound()` | Su AudioSource, u otro sonido que sigue a la entidad en 3D |
| `setText(t)`, `getText()` | Texto de su UIText (acepta números) |
| `setFill(0..1)`, `getFill()` | Relleno de su UIImage (barras de vida, de carga...) |
| `isPressed()`, `wasClicked()`, `setInteractable(b)` | Su UIButton: se mantiene pulsado / se pulsó y soltó encima en este fotograma |
| `getJoystick()` → x, y | Su UIJoystick, de −1 a 1 (Y positiva = arriba) |
| `getUIPosition()`, `setUIPosition(x, y)`, `setUISize(w, h)` | Su UIRect, en píxeles de referencia |
| `move(x, z)`, `jump([velocidad=5])`, `isGrounded()` | Su CharacterController: velocidad horizontal en m/s (se mantiene hasta el siguiente `move`), salto, si pisa suelo |
| `setAnimParam(nombre, valor)`, `setAnimTrigger(nombre)`, `getAnimParam(nombre)`, `getAnimState()` | Máquina de estados de su Animator (valor numérico o booleano) |
| `playParticles()`, `stopParticles([quitarLasVivas])`, `emitParticles(n)`, `particleCount()` | Su ParticleSystem |
| `terrainHeight(x, z)` → y | Altura de su Terrain en ese punto del mundo (para colocar cosas o pegar al suelo) |

Las funciones que devuelven vectores devuelven varios valores: `local x, y, z = e:getPosition()`.

### Tablas globales

| | |
|---|---|
| `Entity.find(nombre)`, `Entity.get(id)`, `Entity.create(nombre [, padre])` | Buscar y crear entidades |
| `Scene.load(nombre o ruta)`, `Scene.name()` | Cambiar de escena (se aplica al final del fotograma) |
| `Scene.instantiate(prefab, x, y, z)` → entidad | Crear una copia de un prefab con sus scripts y su física |
| `Physics.raycast(ox,oy,oz, dx,dy,dz [, distancia, ignorar, máscara])` → entidad, x, y, z, distancia, nx, ny, nz | Rayo contra los colliders (y la normal de la superficie) |
| `Physics.sphereCast(ox,oy,oz, radio, dx,dy,dz [, distancia, ignorar, máscara])` → igual que raycast | Lanza una esfera |
| `Physics.overlapSphere(x,y,z, radio [, máscara])`, `Physics.overlapBox(x,y,z, mx,my,mz [, máscara])` → lista de entidades | Qué hay dentro de una zona |
| `Physics.setLayerCollision(a, b, choca)`, `Physics.layersCollide(a, b)` | Capas de colisión 0..15 (máscara: bit i = capa i; por defecto todas) |
| `Physics.setGravity(x,y,z)`, `Physics.getGravity()` | Gravedad del mundo |
| `Input.action(nombre)`, `Input.pressed(nombre)`, `Input.released(nombre)`, `Input.axis(nombre)` → x, y | Acciones: el mismo código vale para dedo, teclado y mando (ver más abajo) |
| `Input.define(nombre, "boton"/"eje", {enlaces})` | Crear o cambiar una acción |
| `Input.key(nombre)`, `Input.keyPressed(nombre)`, `Input.gamepad()` | Teclado ("Space", "W", "LeftShift"...) y si hay un mando conectado |
| `Quality.set("muy bajo"/"bajo"/"medio"/"alto"/"muy alto")`, `Quality.get()`, `Quality.recommended()`, `Quality.device()` | Calidad gráfica y datos medidos del dispositivo (para un menú de ajustes) |
| `Save.set(clave, valor)`, `Save.get(clave [, defecto])`, `Save.has`, `Save.delete`, `Save.clear`, `Save.write()` | Partida guardada: números, textos, booleanos y tablas; máx. 1 MB; se escribe sola al pausar o cerrar |
| `Audio.play(sonido [, volumen])`, `Audio.playAt(sonido, x, y, z [, volumen])`, `Audio.setVolume(v)` | Sonidos sueltos (por ruta o por nombre) |
| `Time.delta()`, `Time.fixedDelta()`, `Time.time()`, `Time.frame()` | Tiempo |
| `Input.touchCount()`, `Input.touch(i)` → `{id, x, y, phase}`, `Input.touchWorld(i)` → x,y,z (plano z = 0 en 2D; suelo y = 0 con cámara en perspectiva), `Input.tapped()` | Táctil; `i` empieza en 1; `phase` es "began" o "held". Los dedos que están usando un botón o joystick de la interfaz no aparecen |
| `Screen.width()`, `Screen.height()` | Píxeles |
| `Screen.text(x, y, tamaño, texto [, r, g, b, a])` | Texto en pantalla durante este fotograma (admite acentos y eñe) |
| `Debug.log(...)`, `Debug.warn(...)`, `Debug.error(...)`, `print(...)` | Al registro del motor (logcat en Android) |

Bibliotecas de Lua disponibles: base (sin `load`, `loadfile` ni `dofile`), `string` (sin `dump`), `table`, `math`,
`utf8` y `coroutine`. No hay `io`, `os`, `package`, `require` ni `debug`.

Límites por defecto: 0,25 s por llamada y 256 MB de memoria para todo el intérprete.

### Interfaz de juego

Las entidades con `UIRect` se dibujan encima del juego y no usan su Transform. Las medidas están en píxeles de una
pantalla de referencia de **720 px de alto** y se escalan con la pantalla real. Origen arriba a la izquierda.

| Componente | Campos |
|---|---|
| `UIRect` | `anchorMin`, `anchorMax` (0..1 del padre o de la pantalla; distintos = estirar), `pivot`, `position`, `size` |
| `UIImage` | `texture` (vacía = color liso), `color`, `cornerRadius`, `fill` (0..1, de izquierda a derecha) |
| `UIText` | `text`, `font` (.ttf/.otf; vacía = Outfit), `bold`, `size`, `color`, `align`, `verticalAlign`, `shadow` |
| `UIButton` | `interactable`, `pressedTint` |
| `UIJoystick` | `baseColor`, `handleColor`, `handleSize`, `deadZone` |

Un UIRect hijo de otro se coloca dentro del rectángulo del padre; se dibujan en el orden del Outliner (los de abajo,
encima). Ejemplo:

```lua
local Jugador = {}
function Jugador:onStart()
    self.joy = Entity.find("Joystick")
    self.vida = Entity.find("Relleno")
    self.salud = 100
end
function Jugador:onUpdate(dt)
    local x, y = self.joy:getJoystick()
    self.entity:setVelocity(x * 5, select(2, self.entity:getVelocity()), -y * 5)
    if Entity.find("Saltar"):wasClicked() then self.entity:addImpulse(0, 6, 0) end
    self.vida:setFill(self.salud / 100)
end
return Jugador
```

### Acciones de entrada (Input Actions)

Acciones que existen desde el principio:

| Acción | Tipo | Enlaces por defecto |
|---|---|---|
| `Mover` | eje | WASD, flechas, stick izquierdo, cualquier UIJoystick |
| `Mirar` | eje | stick derecho |
| `Saltar` | botón | Espacio, A del mando, UIButton llamado "Saltar" |
| `Atacar` | botón | J, X, gatillo derecho, UIButton "Atacar" |
| `Interactuar` | botón | E, Y, UIButton "Interactuar" |
| `Correr` | botón | Shift izquierdo, LB, UIButton "Correr" |
| `Pausa` | botón | Escape, Start, UIButton "Pausa" |

Enlaces posibles: `key:Space`, `pad:A` (A, B, X, Y, LB, RB, Back, Start, DpadUp...), `pad:LeftTrigger`, `ui:NombreDelBotón`,
`touch` (cualquier toque del juego), y para ejes `keys:WASD`, `keys:Arrows`, `pad:LeftStick`, `pad:RightStick`,
`uijoy:NombreDelJoystick` (o `uijoy:*`). Los sticks tienen zona muerta (0,15) y el teclado en diagonal no va más rápido.

```lua
function Jugador:onUpdate(dt)
    local x, y = Input.axis("Mover")
    self.entity:move(x * 5, -y * 5)                  -- CharacterController
    if Input.pressed("Saltar") then self.entity:jump(6) end
end
```

### Personaje, partículas, animación y guardado

```lua
-- Máquina de estados del Animator (se crea en Detalles › Animator › Estados)
self.entity:setAnimParam("velocidad", velocidad)     -- mezcla 1D Walk/Run
if Input.pressed("Atacar") then self.entity:setAnimTrigger("atacar") end
function Jugador:onAnimationEvent(nombre) if nombre == "paso" then Audio.play("paso") end end

-- Partículas
Entity.find("Chispas"):emitParticles(30)           -- ráfaga en un impacto

-- Partida guardada
Save.set("nivel", 3)
Save.set("inventario", {"espada", "poción"})
local nivel = Save.get("nivel", 1)
```

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
