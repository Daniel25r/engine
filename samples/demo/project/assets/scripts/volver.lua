-- Muestra el nivel 2 durante 2 segundos y vuelve a la escena principal.
local Volver = {}

function Volver:onStart()
    self.inicio = Time.time()
    Debug.log("Nivel 2 cargado")
end

function Volver:onUpdate(dt)
    local escala = 1 + 0.2 * math.sin((Time.time() - self.inicio) * 6)
    self.entity:setScale(escala)
    if Time.time() - self.inicio > 2 then Scene.load("principal") end
end

return Volver
