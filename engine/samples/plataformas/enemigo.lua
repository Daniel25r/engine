-- Enemigo: patrulla entre dos puntos; si ve al jugador (AI.canSee) lo persigue con la navegación. Pisarlo desde
-- arriba lo derrota; tocarlo de lado hace daño.
local Enemigo = { props = { patrulla = 4, vista = 9, lento = 2, rapido = 4.2 } }

function Enemigo:onStart()
    self.jugador = Entity.find("Jugador")
    self.js = self.jugador:getScript()
    self.cuerpo = self.entity:children()[1] -- la malla (la raíz tiene el CharacterController, con origen en los pies)
    local x, y, z = self.entity:getPosition()
    self.a, self.b = { x - self.patrulla, y, z }, { x + self.patrulla, y, z }
    self.haciaB = true
    self.perdido = 0
    self.estado = "patrulla"
    self.t = math.random() * 3
    self:patrullar()
end

function Enemigo:patrullar()
    local p = self.haciaB and self.b or self.a
    self.entity:setNavSpeed(self.lento)
    self.entity:setDestination(p[1], p[2], p[3])
end

function Enemigo:onUpdate(dt)
    local e = self.entity
    self.t = self.t + dt
    if self.estado == "derrotado" then
        self.morir = self.morir - dt
        if self.morir <= 0 then e:destroy() end
        return
    end

    -- Ver y perseguir.
    local x, y, z = e:getWorldPosition()
    local px, py, pz = self.jugador:getWorldPosition()
    local ve = AI.canSee(e, self.jugador, self.vista, 140) and math.abs(py - y) < 2.5
    if ve and self.estado ~= "persigue" then
        self.estado = "persigue"
        e:setNavSpeed(self.rapido)
        e:setDestination(self.jugador)
        self.cuerpo:setColor(1, 0.25, 0.15)
    elseif self.estado == "persigue" then
        self.perdido = ve and 0 or self.perdido + dt
        if self.perdido > 2 then
            self.estado = "patrulla"
            self.cuerpo:setColor(0.75, 0.2, 0.25)
            self:patrullar()
        end
    elseif e:hasReachedDestination() or not e:hasPath() then
        self.haciaB = not self.haciaB
        self:patrullar()
    end

    -- Contacto con el jugador.
    local dx, dy, dz = px - x, py - y, pz - z
    if dx * dx + dz * dz < 0.95 * 0.95 and dy > -1.5 and dy < 1.7 then
        local _, vy = self.jugador:getVelocity()
        if dy > 0.7 and vy < 0.5 then
            self.estado = "derrotado"
            self.morir = 0.35
            e:stopNavigation()
            e:setScale(1.3, 0.3, 1.3)
            e:emitParticles(20)
            Audio.play("pisoton", 1, "Efectos")
            self.js:rebotar()
        else
            self.js:herir(x, z)
        end
    end
    -- Andar dando saltitos.
    local s = 1 + math.abs(math.sin(self.t * (self.estado == "persigue" and 14 or 8))) * 0.08
    e:setScale(1 / math.sqrt(s), s, 1 / math.sqrt(s))
end

return Enemigo
