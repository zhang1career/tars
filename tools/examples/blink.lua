-- Example Lua app: blink LD3 on Discovery board
for i = 1, 6 do
  tars.gpio_write("pg13", 0)
  tars.sleep(150)
  tars.gpio_write("pg13", 1)
  tars.sleep(150)
end
tars.log("blink done")
