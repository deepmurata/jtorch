if (jtorch_root == nil) then
  jtorch_root = "./"
end

dofile(jtorch_root.."/lua/save_tensor.lua")
dofile(jtorch_root.."/lua/load_tensor.lua")
dofile(jtorch_root.."/lua/save_model.lua")
