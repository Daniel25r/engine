-- Gira la entidad sobre su eje Z.
local Girar = {}

function Girar:onUpdate(dt)
    self.entity:rotate(0, 0, 120 * dt)
end

return Girar
