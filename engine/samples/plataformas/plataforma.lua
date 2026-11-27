-- Plataforma móvil (Rigidbody cinemático): va y vuelve entre su posición inicial y (dx, dy, dz) con un
-- movimiento suave. El personaje que está encima se mueve con ella.
local Plataforma = { props = { dx = 0, dy = 0, dz = 0, periodo = 4, giro = 0, retraso = 0 } }

function Plataforma:onStart()
    self.x0, self.y0, self.z0 = self.entity:getPosition()
    self.t = -self.retraso
end

function Plataforma:onUpdate(dt)
    self.t = self.t + dt
    local k = (1 - math.cos(math.max(0, self.t) / self.periodo * 2 * math.pi)) * 0.5
    self.entity:setPosition(self.x0 + self.dx * k, self.y0 + self.dy * k, self.z0 + self.dz * k)
    if self.giro ~= 0 then self.entity:rotate(0, self.giro * dt, 0) end
end

return Plataforma
