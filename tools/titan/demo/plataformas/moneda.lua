-- Moneda: gira y flota; al tocarla el jugador (Collider trigger) suma una moneda y desaparece.
local Moneda = { props = { giro = 180 } }

function Moneda:onStart()
    self.juego = Entity.find("Juego"):getScript()
    self.juego:registrarMoneda()
    local x, y, z = self.entity:getPosition()
    self.y0, self.fase, self.t = y, x * 0.7 + z * 0.3, 0
end

function Moneda:onUpdate(dt)
    self.t = self.t + dt
    self.entity:rotate(0, self.giro * dt, 0)
    local x, _, z = self.entity:getPosition()
    self.entity:setPosition(x, self.y0 + math.sin(self.t * 3 + self.fase) * 0.12, z)
end

function Moneda:onTriggerEnter(otro)
    if self.recogida or otro:name() ~= "Jugador" then return end
    self.recogida = true
    local x, y, z = self.entity:getWorldPosition()
    self.juego:monedaRecogida(x, y, z)
    self.entity:destroy()
end

return Moneda
