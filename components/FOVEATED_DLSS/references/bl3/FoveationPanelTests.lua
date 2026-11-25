-- Execute the actual panel under Lua with a deterministic UEVR/ImGui adapter.
-- No game, headset, native plugin or persistence is started by this fixture.
local receive,draw,sent
local actions,seen,disabled,texts={},{},{},{}
local function blocked() for _,v in ipairs(disabled) do if v then return true end end return false end
local function edit(label,value)
    seen[label]=true
    local v=actions[label]
    if v~=nil and not blocked() then actions[label]=nil;return true,v end
    return false,value
end
imgui={
    text=function(v) texts[#texts+1]=v end,
    checkbox=edit,slider_float=edit,combo=edit,
    begin_disabled=function(v) disabled[#disabled+1]=v end,
    end_disabled=function() assert(#disabled>0);table.remove(disabled) end,
    tree_node=function()return false end,tree_pop=function()end,
    separator=function()end,same_line=function()end,
    button=function(label) local changed=edit(label,false);return changed end,
}
uevr={sdk={callbacks={on_lua_event=function(f)receive=f end}},
    lua={add_script_panel=function(name,f)assert(name=='DLSS_FOVEADO');draw=f end},
    api={dispatch_custom_event=function(_,event,packet)sent={event,packet}end}}
assert(arg[1],'panel path required');dofile(arg[1])
local base='2;1;1;8000;6000;1000;1;0;7500;0;0;10000;5000;5000;5000;5000;1;1;1;6000;6000;1000;1;0;1;9000;7000;1;10000;5000'
local function frame(a)
    actions=a or {};texts={};draw();assert(#disabled==0,'unbalanced disabled blocks')
end
local function values()
    assert(sent);local v={}
    for n in sent[2]:match('^%d+|%d+|(.+)$'):gmatch('[^;]+') do v[#v+1]=tonumber(n) end
    assert(#v==30 and v[1]==2);return v
end
receive('DLSS_FOVEADO.Controls','1|10|'..base)
frame({['Proporcion vertical del estrechamiento SR (%)']=50,['Aplicar']=true})
local v=values();assert(v[25]==1 and v[26]==9000,'locked control changed or lost stored ratio')
assert(table.concat(texts,' '):find('Altura conservada 100.00%%'),'locked summary wrong')
receive('DLSS_FOVEADO.Controls','2|10|'..sent[2]:match('^%d+|%d+|(.+)$'))
frame({['Bloquear altura SR']=false,['Aplicar y guardar']=true})
v=values();assert(sent[1]=='DLSS_FOVEADO.ApplySave' and v[25]==0 and v[26]==9000 and v[27]==7000)
assert(table.concat(texts,' '):find('Altura conservada 82.00%% | Recorte arriba 12.60%% / abajo 5.40%%'),'80/90/70-30 summary wrong')
receive('DLSS_FOVEADO.Controls','3|10|'..sent[2]:match('^%d+|%d+|(.+)$'))
frame({['Reparto inferior SR (%)']=80,['Vincular region Neural con SR']=false,
    ['Bloquear altura Neural']=false,['Proporcion vertical del estrechamiento Neural (%)']=60,
    ['Reparto superior Neural (%)']=30,['Aplicar']=true})
v=values();assert(v[27]==2000,'lower slider did not complement upper')
assert(v[18]==0 and v[28]==0 and v[29]==6000 and v[30]==3000,'independent Neural fields crossed')
receive('DLSS_FOVEADO.Controls','4|10|'..sent[2]:match('^%d+|%d+|(.+)$'))
frame({['Reparto superior SR (%)']=55,['Descartar']=true})
frame({['Aplicar']=true});assert(values()[27]==2000,'discard failed to restore distribution')
receive('DLSS_FOVEADO.Controls','5|10|'..sent[2]:match('^%d+|%d+|(.+)$'))
frame({['Bloquear altura SR']=true,['Proporcion vertical del estrechamiento SR (%)']=0,['Aplicar']=true})
v=values();assert(v[25]==1 and v[26]==9000 and v[27]==2000,'relocking erased ratio/split')
assert(seen['Reparto inferior SR (%)'] and seen['Reparto inferior Neural (%)'],'missing controls')
print('PASS: actual Lua panel, height lock, relative summary, complementary split, SR/NR, Apply/Save/Discard')
