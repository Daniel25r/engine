-- Jugador: se mueve respecto a la cámara, doble salto, salto variable, salto de pared, daño, caídas, puntos de
-- control y una animación hecha con código (piernas, brazos y estirar/aplastar al saltar y aterrizar).
local Jugador = { props = { velocidad = 6, carrera = 9, salto = 8.5, saltoPared = 7, caida = -8 } }

function Jugador:onStart()
    self.juego = Entity.find("Juego"):getScript()
    self.modelo = Entity.find("Modelo del jugador")
    self.cuerpo = Entity.find("Cuerpo del jugador")
    self.piernaI, self.piernaD = Entity.find("Cadera izquierda"), Entity.find("Cadera derecha")
    self.brazoI, self.brazoD = Entity.find("Hombro izquierdo"), Entity.find("Hombro derecho")
    self.polvo = Entity.find("Polvo")
    self.camara = Entity.find("Cámara")
    local x, y, z = self.entity:getPosition()
    self.control = { x, y + 0.2, z }
    self.fase, self.paso, self.invulnerable, self.escala = 0, 0, 0, 1
    self.enSueloAntes = true
    self.vy = 0
end

-- Punto de control: al caer o perder una vida se vuelve aquí.
function Jugador:guardarControl(x, y, z)
    self.control = { x, y + 0.2, z }
end

function Jugador:volverAlControl()
    self.entity:teleport(self.control[1], self.control[2], self.control[3])
end

-- Daño desde un punto (enemigo, pinchos, barra): retroceso, temblor de cámara y una vida menos.
function Jugador:herir(desdeX, desdeZ)
    if self.invulnerable > 0 or self.juego.terminado then return end
    self.invulnerable = 1.6
    Audio.play("golpe", 1, "Efectos")
    self.camara:shakeCamera(0.35, 0.3)
    local x, _, z = self.entity:getPosition()
    local dx, dz = x - desdeX, z - desdeZ
    local d = math.sqrt(dx * dx + dz * dz)
    if d < 0.01 then dx, dz, d = 0, 1, 1 end
    self.entity:pushCharacter(dx / d * 8, dz / d * 8)
    self.entity:setVerticalVelocity(6)
    self.juego:perderVida()
end

-- Rebote al pisar a un enemigo.
function Jugador:rebotar()
    self.entity:setVerticalVelocity(9)
    self.escala = 1.35
end

function Jugador:onUpdate(dt)
    local e = self.entity
    local terminado = self.juego.terminado
    -- Movimiento: el joystick o WASD empujan hacia donde mira la cámara.
    local mx, mz = Input.cameraAxis("Mover")
    if terminado then mx, mz = 0, 0 end
    local rapida = Input.action("Correr") and self.carrera or self.velocidad
    e:move(mx * rapida, mz * rapida)

    local enSuelo = e:isGrounded()
    local _, vy = e:getVelocity()
    if not terminado and Input.pressed("Saltar") then
        local pared, nx, nz = e:isTouchingWall()
        if not enSuelo and pared and e:jumpsUsed() >= 2 then
            -- Salto de pared: impulso hacia fuera de la pared y hacia arriba.
            e:pushCharacter(nx * self.saltoPared, nz * self.saltoPared)
            e:setVerticalVelocity(self.saltoPared + 1)
            Audio.play("salto", 0.7, "Efectos")
            self.escala = 1.25
        elseif enSuelo or e:jumpsUsed() < 2 then
            e:jump(self.salto)
            Audio.play("salto", enSuelo and 0.7 or 0.5, "Efectos")
            self.escala = 1.25
        end
    end
    if Input.released("Saltar") then e:endJump() end -- salto variable: soltar antes = saltar menos

    -- Aterrizaje: polvo, sonido y aplastar un momento.
    if enSuelo and not self.enSueloAntes and self.vy < -4 then
        local x, y, z = e:getPosition()
        self.polvo:setPosition(x, y + 0.05, z)
        self.polvo:emitParticles(12)
        Audio.play("paso", 0.6, "Efectos")
        self.escala = 0.75
    end
    self.enSueloAntes, self.vy = enSuelo, vy

    -- Caída al vacío.
    local _, y = e:getPosition()
    if y < self.caida then
        Audio.play("caida", 1, "Efectos")
        if self.juego:perderVida() then self:volverAlControl() end
        self.invulnerable = 1
        e:setVerticalVelocity(0)
    end

    self:animar(dt, mx, mz, rapida, enSuelo)
end

function Jugador:animar(dt, mx, mz, rapida, enSuelo)
    local intensidad = math.min(1, math.sqrt(mx * mx + mz * mz))
    if enSuelo and intensidad > 0.1 then
        self.fase = self.fase + dt * rapida * 1.8
        self.paso = self.paso - dt * rapida
        if self.paso <= 0 then self.paso = 2.2; Audio.play("paso", 0.25, "Efectos") end
    end
    local balanceo = math.sin(self.fase) * 45 * intensidad
    if enSuelo then
        self.piernaI:setRotation(balanceo, 0, 0)
        self.piernaD:setRotation(-balanceo, 0, 0)
        self.brazoI:setRotation(-balanceo * 0.8, 0, -8)
        self.brazoD:setRotation(balanceo * 0.8, 0, 8)
    else -- en el aire: piernas recogidas y brazos arriba
        self.piernaI:setRotation(-35, 0, 0)
        self.piernaD:setRotation(20, 0, 0)
        self.brazoI:setRotation(0, 0, -150)
        self.brazoD:setRotation(0, 0, 150)
    end
    -- Estirar y aplastar: vuelve poco a poco a 1.
    self.escala = self.escala + (1 - self.escala) * math.min(1, dt * 10)
    local s = self.escala
    self.modelo:setScale(1 / math.sqrt(s), s, 1 / math.sqrt(s))
    local bote = enSuelo and math.abs(math.sin(self.fase)) * 0.06 * intensidad or 0
    self.modelo:setPosition(0, bote, 0)
    -- Parpadeo mientras es invulnerable.
    if self.invulnerable > 0 then
        self.invulnerable = self.invulnerable - dt
        self.modelo:setActive(self.invulnerable <= 0 or math.floor(self.invulnerable * 12) % 2 == 0)
    end
end

return Jugador
