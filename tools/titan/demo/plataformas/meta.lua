-- Estrella de la meta y banderas de control. tipo = "meta" o "control".
local Meta = { props = { tipo = "meta" } }

function Meta:onStart()
    self.juego = Entity.find("Juego"):getScript()
    self.t = 0
    self.y0 = select(2, self.entity:getPosition())
end

function Meta:onUpdate(dt)
    self.t = self.t + dt
    if self.tipo == "meta" then
        self.entity:rotate(0, 90 * dt, 0)
        local x, _, z = self.entity:getPosition()
        self.entity:setPosition(x, self.y0 + math.sin(self.t * 2) * 0.2, z)
    end
end

function Meta:onTriggerEnter(otro)
    if otro:name() ~= "Jugador" then return end
    if self.tipo == "meta" then
        self.juego:victoria()
        self.entity:emitParticles(80)
    elseif not self.activada then
        self.activada = true
        local x, y, z = self.entity:getWorldPosition()
        otro:getScript():guardarControl(x, y - 1, z)
        self.entity:setColor(0.3, 1, 0.4)
        self.entity:emitParticles(30)
        Audio.play("control", 1, "Efectos")
        self.juego:avisar("Punto de control", 1.5)
    end
end

return Meta
