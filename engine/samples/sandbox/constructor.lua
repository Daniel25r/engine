-- Sandbox de bloques: el jugador explora el mundo, rompe bloques (J), coloca el bloque elegido (E) y cambia de
-- bloque con 1-9 o Q. El mundo se guarda solo cada 30 s y al salir (en .titan/mundos/ o junto a la partida).
local Constructor = { props = { alcance = 6, autoguardado = 30 } }

local BLOQUES = { "Hierba", "Tierra", "Piedra", "Arena", "Tablones", "Ladrillo", "Madera", "Hojas", "Hielo" }

function Constructor:onStart()
    self.camara = Entity.find("Cámara")
    self.marco = Entity.find("Bloque apuntado")
    self.textoBloque = Entity.find("Texto bloque")
    self.textoInfo = Entity.find("Texto info")
    self.elegido = 1
    self.guardado = 0
    self.aviso, self.avisoTiempo = "", 0
    if not Voxel.available() then
        self.textoInfo:setText("Esta escena no tiene un Mundo de bloques")
        return
    end
    -- Aparecer sobre el suelo (el mundo cercano ya está cargado al empezar).
    local x, _, z = self.entity:getPosition()
    local suelo = Voxel.surface(math.floor(x), math.floor(z))
    if suelo >= 0 then self.entity:teleport(x, suelo + 2, z) end
    self:mostrarBloque()
end

function Constructor:mostrarBloque()
    self.textoBloque:setText("Bloque: " .. BLOQUES[self.elegido] .. "  (1-9 / Q)")
end

function Constructor:avisar(texto)
    self.aviso, self.avisoTiempo = texto, 2
end

-- Bloque al que se apunta: un rayo desde la cabeza del jugador en la dirección de la cámara.
function Constructor:apuntar()
    local x, y, z = self.entity:getPosition()
    local dx, dy, dz = self.camara:forward()
    return Voxel.raycast(x, y + 1.5, z, dx, dy, dz, self.alcance)
end

function Constructor:onUpdate(dt)
    if not Voxel.available() then return end
    local e = self.entity
    local mx, mz = Input.cameraAxis("Mover")
    local rapida = Input.action("Correr") and 8 or 5
    e:move(mx * rapida, mz * rapida)
    if Input.pressed("Saltar") and e:isGrounded() then e:jump(7) end

    for i = 1, #BLOQUES do
        if Input.keyPressed(tostring(i)) then self.elegido = i; self:mostrarBloque() end
    end
    if Input.keyPressed("Q") then
        self.elegido = self.elegido % #BLOQUES + 1
        self:mostrarBloque()
    end

    local golpe = self:apuntar()
    if golpe then
        self.marco:setActive(true)
        self.marco:setPosition(golpe.x + 0.5, golpe.y + 0.5, golpe.z + 0.5)
        if Input.pressed("Atacar") then
            local nombre = Voxel.blockName(golpe.block)
            if Voxel.set(golpe.x, golpe.y, golpe.z, "Aire") then self:avisar("Roto: " .. nombre) end
        elseif Input.pressed("Interactuar") then
            local px, py, pz = golpe.x + golpe.nx, golpe.y + golpe.ny, golpe.z + golpe.nz
            -- No se coloca dentro del jugador (se quedaría atascado).
            local x, y, z = e:getPosition()
            local dentro = px == math.floor(x) and pz == math.floor(z) and py >= math.floor(y) and py <= math.floor(y + 1.7)
            if dentro then
                self:avisar("Ahí estás tú")
            elseif Voxel.set(px, py, pz, BLOQUES[self.elegido]) then
                self:avisar("Colocado: " .. BLOQUES[self.elegido])
            end
        end
    else
        self.marco:setActive(false)
    end

    -- Guardado automático (solo escribe los chunks que cambiaron).
    self.guardado = self.guardado + dt
    if self.guardado >= self.autoguardado then
        self.guardado = 0
        local n = Voxel.save()
        if n > 0 then self:avisar("Mundo guardado (" .. n .. " chunks)") end
    end

    self.avisoTiempo = self.avisoTiempo - dt
    local x, y, z = e:getPosition()
    local info = string.format("%s   x %d  y %d  z %d   chunks %d   semilla %d",
        Voxel.biome(math.floor(x), math.floor(z)), math.floor(x), math.floor(y), math.floor(z), Voxel.loaded(), Voxel.seed())
    if self.avisoTiempo > 0 then info = info .. "\n" .. self.aviso end
    self.textoInfo:setText(info)
end

return Constructor
