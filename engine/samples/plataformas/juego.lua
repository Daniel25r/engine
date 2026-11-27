-- Estado de la partida: monedas, vidas, tiempo y textos de la interfaz.
-- Los demás scripts lo usan con Entity.find("Juego"):getScript().
local Juego = { props = { vidas = 3, siguiente = "victoria" } }

local function reloj(t)
    return string.format("%d:%02d", math.floor(t / 60), math.floor(t % 60))
end

function Juego:onCreate()
    -- En onCreate (antes de todos los onStart) para que las monedas puedan registrarse en su onStart.
    self.monedas, self.total, self.tiempo = 0, 0, 0
    self.vidasRestantes = self.vidas
    self.terminado = false
    self.mensajeTiempo = 0
end

function Juego:onStart()
    self.textoMonedas = Entity.find("Texto monedas")
    self.textoVidas = Entity.find("Texto vidas")
    self.textoTiempo = Entity.find("Texto tiempo")
    self.mensaje = Entity.find("Mensaje")
    self.chispas = Entity.find("Chispas")
    self.botonMenu = Entity.find("Menú")
    Save.set("partidas", Save.get("partidas", 0) + 1)
    self:actualizar()
    self:avisar("¡Recoge las monedas y llega a la estrella!", 3.5)
end

function Juego:avisar(texto, segundos)
    self.mensaje:setText(texto)
    self.mensajeTiempo = segundos or 2
end

function Juego:actualizar()
    self.textoMonedas:setText(string.format("Monedas %d / %d", self.monedas, self.total))
    self.textoVidas:setText("Vidas " .. math.max(0, self.vidasRestantes))
end

function Juego:registrarMoneda()
    self.total = self.total + 1
end

function Juego:monedaRecogida(x, y, z)
    self.monedas = self.monedas + 1
    Audio.play("moneda", 0.8, "Efectos")
    self.chispas:setPosition(x, y, z)
    self.chispas:emitParticles(24)
    self:actualizar()
    if self.monedas == self.total then self:avisar("¡Todas las monedas!", 2.5) end
end

-- Devuelve false si se acabaron las vidas (el nivel se reinicia solo).
function Juego:perderVida()
    if self.terminado then return true end
    self.vidasRestantes = self.vidasRestantes - 1
    self:actualizar()
    if self.vidasRestantes <= 0 then
        self.terminado = true
        Audio.play("derrota", 1, "Efectos")
        self:avisar("Sin vidas... ¡otra vez!", 3)
        self.reiniciar = 2.5
        return false
    end
    return true
end

function Juego:victoria()
    if self.terminado then return end
    self.terminado = true
    Audio.play("victoria", 1, "Efectos")
    local partida = { monedas = self.monedas, total = self.total, tiempo = self.tiempo }
    local mejor = Save.get("mejor", nil)
    local record = mejor == nil or partida.monedas > mejor.monedas
        or (partida.monedas == mejor.monedas and partida.tiempo < mejor.tiempo)
    if record then Save.set("mejor", partida) end
    Save.set("ultima", partida)
    Save.set("record", record)
    Save.set("completado", true)
    Save.write()
    Debug.log(string.format("DEMO victoria: %d/%d monedas en %.1f s", self.monedas, self.total, self.tiempo))
    self:avisar("¡Nivel completado!", 3)
    self.cambiar = 2.2
end

function Juego:onUpdate(dt)
    if not self.terminado then self.tiempo = self.tiempo + dt end
    self.textoTiempo:setText(reloj(self.tiempo))
    if self.mensajeTiempo > 0 then
        self.mensajeTiempo = self.mensajeTiempo - dt
        if self.mensajeTiempo <= 0 then self.mensaje:setText("") end
    end
    if self.cambiar then
        self.cambiar = self.cambiar - dt
        if self.cambiar <= 0 then self.cambiar = nil; Scene.load(self.siguiente) end
    end
    if self.reiniciar then
        self.reiniciar = self.reiniciar - dt
        if self.reiniciar <= 0 then self.reiniciar = nil; Scene.load(Scene.name()) end
    end
    if Input.pressed("Pausa") or self.botonMenu:wasClicked() then
        Save.write()
        Scene.load("menu")
    end
end

return Juego
