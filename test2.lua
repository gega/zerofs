local m = require('zerofs')

-- --- CONFIG ------------------------------------------------------
local TEST_DIR = "data"
local SIZES = require("testfilesizes")
local FILES = {};
local ITERATIONS = 22
local CHUNK_SIZE = 2048
local SPEED_FACTOR = 0
local DELAY_MS = 0
local DELETE_RATIO = 0.45   -- fraction of existing files to delete when full
local SEED = 8839;
-- ----------------------------------------------------------------

m.badblock(false)

math.randomseed(SEED)

m.setdir(TEST_DIR)
m.speed(SPEED_FACTOR, DELAY_MS)
m.setstep(false)

for _, s in ipairs(SIZES) do FILES[s]="f" .. s .. ".csv" end

-- track file states: "good", "deleted", "failed"
local state = {}
for _, s in ipairs(SIZES) do state[s] = "deleted" end

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

  for _, s in ipairs(SIZES) do
    f=FILES[s]
    -- only write if currently deleted or previously failed
    if state[f] ~= "good" then
      local res = m.write(f, CHUNK_SIZE)
      if res == 0 then
        state[f] = "good"
      else
        state[f] = "failed"
        warn("Write failed for " .. f .. " (code " .. res .. ")")
        break
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
    for _, s in ipairs(SIZES) do
      f=FILES[s]
      if state[f] == "good" then
        table.insert(existing, f)
      end
    end

    local victims = random_subset(existing, DELETE_RATIO)
    --table.sort(victims)

    for _, f in ipairs(victims) do
      warn("Removing " .. f .. " to free space")
      m.delete(f);
      state[f] = "deleted"
    end
  end

  -- VERIFY PHASE -------------------------------------------------
  m.setmode("read")
  for _, s in ipairs(SIZES) do
    f=FILES[s]
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

