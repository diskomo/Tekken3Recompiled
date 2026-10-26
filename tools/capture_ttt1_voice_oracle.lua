-- MAME 0.289, tektagt World TEG2/VER.C1, fresh NVRAM. No ROM data embedded.
-- Request sound IDs (TTT_VOICE_IDS="673 674 ...") through the original H8
-- driver during attract mode and record the C352 registers of voice 21 when
-- the execute-key-on register is written, in the request's own frame.
-- Writes voice-oracle.csv for tools/data/ttt1_voices.json.
local machine=manager.machine
local space=machine.devices[':sub'].spaces['program']
local ids={}
for s in string.gmatch(os.getenv('TTT_VOICE_IDS') or '','%d+') do ids[#ids+1]=tonumber(s) end
assert(#ids>0,'set TTT_VOICE_IDS')
local out=assert(io.open('voice-oracle.csv','w'))
out:write('requested_id,frequency,flags,bank,start,end\n')
local tick,requested,start=0,0,1500
ttt1_voice_tap=space:install_write_tap(0x280404,0x280405,'ttt1-voice-keyon',function()
 local base=0x280000+21*16
 if requested>0 and (space:read_u16(base+6)&0x4000)~=0 then
  out:write(string.format('%d,%d,%d,%d,%d,%d\n',requested,space:read_u16(base+4),
   space:read_u16(base+6),space:read_u16(base+8),space:read_u16(base+10),space:read_u16(base+12)))
  requested=0
 end
end)
ttt1_voice_frames=emu.add_machine_frame_notifier(function()
 tick=tick+1
 requested=0
 if tick>=start and (tick-start)%90==0 and (tick-start)//90<#ids then
  requested=ids[(tick-start)//90+1]
  space:write_u16(0x080204,0x10)
  space:write_u16(0x080222,requested)
  space:write_u16(0x08012a,0x4101)
 end
 if tick==start+90*#ids then out:close();machine:exit() end
end)
