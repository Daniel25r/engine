-- Nave: orbita alrededor del centro. Cada toque en la pantalla
-- cambia su color; al tercer toque se carga la escena "nivel2".
local Nave = {}

function Nave:onStart()
    self.radio = 2.5
    self.angulo = 0
    self.toques = 0
    Debug.log("La nave arranca en", self.entity:getPosition())
end

function Nave:onUpdate(dt)
    self.angulo = self.angulo + dt * 0.8
    local x = math.cos(self.angulo) * self.radio
    local y = math.sin(self.angulo) * self.radio
    self.entity:setPosition(x, y, 0)
    -- La punta de la flecha mira en la dirección del movimiento.
    self.entity:setRotation(0, 0, math.deg(self.angulo))

    if Input.tapped() then
        self.toques = self.toques + 1
        local colores = { {0.37, 0.82, 0.78}, {0.91, 0.40, 0.42}, {0.95, 0.71, 0.29} }
        local c = colores[(self.toques - 1) % #colores + 1]
        self.entity:setColor(c[1], c[2], c[3])
        Debug.log("Toque número", self.toques)
        if self.toques >= 3 then Scene.load("nivel2") end
    end
end

return Nave
