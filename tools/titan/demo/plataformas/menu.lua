-- Menú principal: jugar, borrar el progreso y mejor marca guardada.
local Menu = {}

local function reloj(t) return string.format("%d:%02d", math.floor(t / 60), math.floor(t % 60)) end

function Menu:onStart()
    self.jugar = Entity.find("Jugar")
    self.borrar = Entity.find("Borrar progreso")
    self.marca = Entity.find("Mejor marca")
    self.decorado = Entity.find("Moneda del menú")
    self.camara = Entity.find("Cámara")
    self.t = 0
    self:mostrarMarca()
end

function Menu:mostrarMarca()
    local mejor = Save.get("mejor", nil)
    if mejor then
        self.marca:setText(string.format("Mejor marca: %d / %d monedas en %s   ·   Partidas: %d",
            mejor.monedas, mejor.total, reloj(mejor.tiempo), Save.get("partidas", 0)))
    else
        self.marca:setText("Todavía no has completado el nivel")
    end
    self.borrar:setActive(mejor ~= nil or Save.has("partidas"))
end

function Menu:onUpdate(dt)
    self.t = self.t + dt
    self.decorado:rotate(0, 90 * dt, 0)
    self.camara:setPosition(math.sin(self.t * 0.2) * 1.5, 2.2, 6)
    if self.jugar:wasClicked() or Input.pressed("Saltar") then
        Audio.play("moneda", 1, "Efectos")
        Scene.load("nivel1")
    end
    if self.borrar:isActive() and self.borrar:wasClicked() then
        Save.clear()
        Save.write()
        Audio.play("golpe", 0.5, "Efectos")
        self:mostrarMarca()
    end
end

return Menu
