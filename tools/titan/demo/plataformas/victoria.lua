-- Pantalla de victoria: resultado de la partida, si es récord, y volver a jugar o al menú.
local Victoria = {}

local function reloj(t) return string.format("%d:%02d", math.floor(t / 60), math.floor(t % 60)) end

function Victoria:onStart()
    local u = Save.get("ultima", { monedas = 0, total = 0, tiempo = 0 })
    Entity.find("Resultado"):setText(string.format("Monedas %d / %d   ·   Tiempo %s", u.monedas, u.total, reloj(u.tiempo)))
    local mejor = Save.get("mejor", u)
    Entity.find("Récord"):setText(Save.get("record", false) and "¡Nueva mejor marca!"
        or string.format("Mejor marca: %d / %d en %s", mejor.monedas, mejor.total, reloj(mejor.tiempo)))
    self.otraVez = Entity.find("Jugar otra vez")
    self.menu = Entity.find("Volver al menú")
    self.fuegos = Entity.find("Fuegos")
    self.estrella = Entity.find("Estrella de la victoria")
    self.t, self.siguiente = 0, 0.3
    Audio.play("victoria", 1, "Efectos")
end

function Victoria:onUpdate(dt)
    self.t = self.t + dt
    self.estrella:rotate(0, 120 * dt, 0)
    self.siguiente = self.siguiente - dt
    if self.siguiente <= 0 then -- fuegos artificiales en sitios distintos
        self.siguiente = 0.7
        self.fuegos:setPosition(math.sin(self.t * 1.7) * 4, 2.5 + math.cos(self.t * 2.3), -3)
        self.fuegos:emitParticles(60)
        Audio.play("paso", 0.3, "Efectos")
    end
    if self.otraVez:wasClicked() or Input.pressed("Saltar") then Scene.load("nivel1") end
    if self.menu:wasClicked() or Input.pressed("Pausa") then Scene.load("menu") end
end

return Victoria
