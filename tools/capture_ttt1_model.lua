-- MAME 0.289, tektagt World TEG2/VER.C1, fresh NVRAM. No ROM data embedded.
-- Loads any arcade model into the select-screen preview so its pointer
-- relocations can be audited, including locked fighters. TTT_CAPTURE_MODEL
-- replaces the model number in Jun's first costume lookup while Jun is
-- highlighted; ROM, NVRAM and the selected fighter are unchanged.
local model=tonumber(os.getenv("TTT_CAPTURE_MODEL") or "")
assert(model and model>=0 and model<=130,"set TTT_CAPTURE_MODEL")
local machine=manager.machine
local space=machine.devices[":maincpu"].spaces["program"]
local port=machine.ioport.ports[":JVS_PLAYER1"]
local frame=0
local function dump(name)
    local f=assert(io.open(name,"wb"))
    f:write(space:read_range(0,0x3fffff,8));f:close()
end
ttt1_model_capture=emu.add_machine_frame_notifier(function()
    frame=frame+1
    if frame>1800 then
        assert(space:read_u8(0x800111fd)==47 and space:read_u8(0x80011200)==118)
        space:write_u8(0x800111fc,model)
    end
    if frame==1800 then machine.ioport.ports[":JVS_COIN1"].fields["Coin 1"]:set_value(1) end
    if frame==1804 then machine.ioport.ports[":JVS_COIN1"].fields["Coin 1"]:set_value(0) end
    if frame==1860 then port.fields["1 Player Start"]:set_value(1) end
    if frame==1864 then port.fields["1 Player Start"]:set_value(0) end
    if frame==1962 then port.fields["P1 Up"]:set_value(1) end
    if frame==1966 then port.fields["P1 Up"]:set_value(0) end
    if frame>=1974 and frame<=2070 and (frame-1974)%12==0 then port.fields["P1 Right"]:set_value(1) end
    if frame>=1978 and frame<=2074 and (frame-1978)%12==0 then port.fields["P1 Right"]:set_value(0) end
    if frame==2130 then dump("ttt1-model-"..model.."-select.bin");machine.video:snapshot();machine:exit() end
end)
