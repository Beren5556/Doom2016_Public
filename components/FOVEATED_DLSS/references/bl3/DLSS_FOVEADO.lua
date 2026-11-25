-- Fixed foveation controls. This script contains no rendering or game hooks.
local draft, requested, base, general, pending
local dirty=false
local status,saved,details="Esperando al modulo","",""
local presets={"Heredar / Auto","J","K","L","M"}
local presetIds={0,10,11,12,13}
local function copy(v) local r={};for i,x in ipairs(v) do r[i]=x end;return r end
local function encode(v) local r={};for i,x in ipairs(v) do r[i]=string.format("%d",x) end;return table.concat(r,";") end
local function decode(s)
    if type(s)~="string" or #s>1024 or not s:match("^%d+[;%d]*%d$") or s:find(";;",1,true) then return end
    local v={};for n in s:gmatch("[^;]+") do v[#v+1]=tonumber(n) end
    if #v~=30 or v[1]~=2 then return end
    return v
end
uevr.sdk.callbacks.on_lua_event(function(event,data)
    if event=="DLSS_FOVEADO.Status" then status=data;return end
    if event=="DLSS_FOVEADO.Saved" then saved=data;return end
    if event=="DLSS_FOVEADO.Details" then details=data;return end
    if event=="DLSS_FOVEADO.Rejected" then pending=nil;dirty=true;saved="Solicitud rechazada; revisa o descarta el borrador";return end
    if event~="DLSS_FOVEADO.Controls" or type(data)~="string" then return end
    local revision,ticket,packet=data:match("^(%d+)|(%d+)|(.+)$")
    local value=decode(packet);if not value then return end
    requested={values=value,revision=revision,general=ticket}
    if pending and ticket~=general then pending=nil;dirty=true;saved="Ajustes DLSS cambiados; descarta o revisa el borrador"
    elseif pending and (packet==pending or revision~=base) then pending=nil end
    if not draft or (not dirty and not pending) then draft=copy(value);base=revision;general=ticket end
end)
local function check(label,index)
    local changed,v=imgui.checkbox(label,draft[index]==1)
    if changed then draft[index]=v and 1 or 0;dirty=true end
end
local function percent(label,index,low,high)
    local changed,v=imgui.slider_float(label,draft[index]/100,low,high,"%.2f")
    if changed then draft[index]=math.floor(v*100+0.5);dirty=true end
end
local function combo(label,index,items)
    local changed,v=imgui.combo(label,draft[index]+1,items)
    if changed then draft[index]=v-1;dirty=true end
end
local function preset(label,index)
    local items=index==10 and {"Auto","J","K","L","M"} or presets
    local changed,v=imgui.combo(label,draft[index]==0 and 1 or draft[index]-8,items)
    if changed then draft[index]=presetIds[v];dirty=true end
end
local function region(label,shape,width,transition,lock,ratio,upper)
    combo("Forma "..label,shape,{"Cuadrado / rectangulo","Circulo / elipse"})
    percent("Campo de alta calidad "..label.." (%)",width,10,100)
    imgui.text("Menor campo = mas foveado por los lados. 100% = anchura completa.")
    check("Bloquear altura "..label,lock)
    imgui.begin_disabled(draft[lock]==1)
    percent("Proporcion vertical del estrechamiento "..label.." (%)",ratio,0,100)
    percent("Reparto superior "..label.." (%)",upper,0,100)
    local changed,lower=imgui.slider_float("Reparto inferior "..label.." (%)",(10000-draft[upper])/100,0,100,"%.2f")
    if changed then draft[upper]=10000-math.floor(lower*100+0.5);dirty=true end
    imgui.end_disabled()
    local reduction=draft[lock]==1 and 0 or (10000-draft[width])*draft[ratio]/10000
    local top=reduction*draft[upper]/10000
    imgui.text(string.format("Altura conservada %.2f%% | Recorte arriba %.2f%% / abajo %.2f%%",
        100-reduction/100,top/100,(reduction-top)/100))
    percent("Transicion "..label.." (%)",transition,0,50)
end
local function draw()
    imgui.text("DLSS_FOVEADO | 0.9.4")
    imgui.text(status)
    imgui.text(details)
    imgui.text("Foveacion fija. Pimax OpenXR / Quest 3 con VDXR o Meta Link OpenXR.")
    imgui.text("Contorno continuo entre ambos ojos; sin corte recto hacia la nariz.")
    imgui.text("Selecciona DLSS o DLSS5 en NVIDIA_DLSS. Seguimiento ocular previsto para otra version.")
    if not draft then return end
    check("Activar foveacion fija",2)
    imgui.begin_disabled(draft[2]==0)
    region("SR",3,4,6,25,26,27)
    preset("Preset central",11)
    percent("Supermuestreo central (%)",12,100,200)
    combo("Periferia",8,{"Espacial","Temporal (DLAA)"})
    percent("Resolucion periferica (% de entrada DLSS)",9,25,100)
    imgui.begin_disabled(draft[8]==0);preset("Preset periferico",10);imgui.end_disabled()
    if imgui.tree_node("Alineacion avanzada por ojo") then
        imgui.text("50% conserva la union nasal del ovalo. Y no actua con altura bloqueada.")
        percent("Ojo izquierdo X (%)",13,0,100);percent("Ojo izquierdo Y (%)",14,0,100)
        percent("Ojo derecho X (%)",15,0,100);percent("Ojo derecho Y (%)",16,0,100)
        if imgui.button("Centrar ambos ojos") then for i=13,16 do draft[i]=5000 end;dirty=true end
        imgui.tree_pop()
    end
    check("Vincular region Neural con SR",18)
    imgui.begin_disabled(draft[18]==1);region("Neural",19,20,22,28,29,30);imgui.end_disabled()
    imgui.text("Neural antes/despues, intensidad y escala: panel NVIDIA_DLSS.")
    check("Mostrar contorno de la zona",17)
    combo("Contornos visibles",24,{"SR (cian)","Neural (rosa)","Ambos"})
    imgui.end_disabled()
    imgui.separator()
    imgui.begin_disabled(pending~=nil)
    local function submit(save)
        pending=encode(draft);dirty=false
        uevr.api:dispatch_custom_event(save and "DLSS_FOVEADO.ApplySave" or "DLSS_FOVEADO.Apply",base.."|"..general.."|"..pending)
    end
    if imgui.button("Aplicar") then submit(false) end
    imgui.same_line();if imgui.button("Aplicar y guardar") then submit(true) end
    imgui.same_line();if imgui.button("Descartar") and requested then
        draft=copy(requested.values);base=requested.revision;general=requested.general;dirty=false
    end
    imgui.end_disabled()
    if dirty then imgui.text("Borrador sin aplicar") end
    if pending then imgui.text("Solicitud enviada; esperando confirmacion") end
    imgui.text(saved)
end
if uevr.lua and type(uevr.lua.add_script_panel)=="function" then
    uevr.lua.add_script_panel("DLSS_FOVEADO",draw)
else
    uevr.sdk.callbacks.on_draw_ui(function() if imgui.tree_node("DLSS_FOVEADO") then draw();imgui.tree_pop() end end)
end
