-- Cooperative loop: one step per scheduler timeslice
local n = 0
while n < 5 do
  n = n + 1
  tars.log("coop step " .. n)
  tars.yield()
end
tars.log("coop done")
