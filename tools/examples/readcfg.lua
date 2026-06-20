-- Example: read a text file from LittleFS
local f = tars.open("/apps/hello.tlua", "r")
local chunk = f:read("*a")
f:close()
tars.log("read " .. tostring(#chunk) .. " bytes")
