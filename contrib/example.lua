
if {DO:0x15} and {W1:0xf64969f1} < 47 then
   return false
end

if not {DO:0x15} and {W1:0xf64969f1} > 100 then
   return true
end

return {DO:0x15}
