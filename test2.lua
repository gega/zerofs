local m = require('zerofs')

-- --- CONFIG ------------------------------------------------------
local TEST_DIR = "stressdata"
local FILES = {
  "f128.csv", "f256.csv", "f512.csv", "f1024.csv", "f2048.csv",
  "f3072.csv", "f4096.csv", "f6144.csv", "f8192.csv", "f12288.csv",
  "f16384.csv", "f24576.csv", "f32768.csv", "f49152.csv", "f65536.csv",
  "f98304.csv", "f131072.csv", "f196608.csv", "f262144.csv", "f393216.csv",
  "f524288.csv", "f786432.csv", "f1048576.csv", "f1572864.csv",
  "f2097152.csv", "f3145728.csv", "f3670016.csv"
}
local ITERATIONS = 12
local CHUNK_SIZE = 300000
local SPEED_FACTOR = 0
local DELAY_MS = 0
local DELETE_RATIO = 0.3   -- fraction of existing files to delete when full
local SEED = 34341;
-- ----------------------------------------------------------------

math.randomseed(SEED)

m.setdir(TEST_DIR)
m.speed(SPEED_FACTOR, DELAY_MS)
m.setstep(false)

-- track file states: "good", "deleted", "failed"
local state = {}
for _, f in ipairs(FILES) do state[f] = "deleted" end

local function random_subset(list, ratio)
  local out = {}
  for _, v in ipairs(list) do
    if math.random() < ratio then
      table.insert(out, v)
    end
  end
  return out
end

for iter = 1, ITERATIONS do
  warn("=== Iteration " .. iter .. " ===")

  -- try to fill the filesystem -----------------------------------
  m.setmode("write")
  warn("setmode(write) ok")

  for _, f in ipairs(FILES) do
    -- only write if currently deleted or previously failed
    if state[f] ~= "good" then
      local res = m.write(f, CHUNK_SIZE)
      if res == 0 then
        state[f] = "good"
      else
        state[f] = "failed"
        warn("Write failed for " .. f .. " (code " .. res .. ")")
      end
    end
  end


  -- handle out-of-space: delete some random files ----------------
  local failed_any = false
  for _, s in pairs(state) do
    if s == "failed" then
      failed_any = true
      break
    end
  end

  if failed_any then
    local existing = {}
    for _, f in ipairs(FILES) do
      if state[f] == "good" then
        table.insert(existing, f)
      end
    end

    local victims = random_subset(existing, DELETE_RATIO)
    table.sort(victims)

    for _, f in ipairs(victims) do
      warn("Removing " .. f .. " to free space")
      m.delete(f);
      state[f] = "deleted"
    end
  end

  -- VERIFY PHASE -------------------------------------------------
  m.setmode("read")
  for _, f in ipairs(FILES) do
    if state[f] == "good" then
      local res = m.verify(f)
      if res ~= 0 then
        m.assert("Verification failed for " .. f .. " (code " .. res .. ")")
      end
    end
  end

  warn("Iteration " .. iter .. " complete")
end

warn("STRESS TEST FINISHED OK")

