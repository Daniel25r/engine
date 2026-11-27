-- Peligro: daña al jugador que se acerca. Con "largo" > 0 es una barra que gira (daña a lo largo de su eje X);
-- si no, una zona redonda (pinchos).
local Peligro = { props = { radio = 0.7, largo = 0, giro = 0, alto = 1.2 } }

function Peligro:onStart()
    self.jugador = Entity.find("Jugador")
    self.js = self.jugador:getScript()
end

function Peligro:onUpdate(dt)
    if self.giro ~= 0 then self.entity:rotate(0, self.giro * dt, 0) end
    local px, py, pz = self.jugador:getWorldPosition()
    local x, y, z = self.entity:getWorldPosition()
    if py + 1.6 < y - self.alto * 0.5 or py > y + self.alto * 0.5 then return end -- por encima o por debajo
    local cx, cz = x, z
    if self.largo > 0 then -- punto de la barra más cercano al jugador
        local rx, _, rz = self.entity:right()
        local t = math.max(-self.largo, math.min(self.largo, (px - x) * rx + (pz - z) * rz))
        cx, cz = x + rx * t, z + rz * t
    end
    local dx, dz = px - cx, pz - cz
    if dx * dx + dz * dz < (self.radio + 0.35) ^ 2 then self.js:herir(cx, cz) end
end

return Peligro
