local PROFILE_PPM8 = 1
local PROFILE_SBUS16 = 2
local STABLE_FRAMES = 20
local ARM_MAGIC_MIN = 700
local PROFILE_THRESHOLD = 500

local last_seq = -1
local pending_key = ""
local pending_count = 0

local function round(x)
  if x >= 0 then
    return math.floor(x + 0.5)
  end
  return math.ceil(x - 0.5)
end

local function read_trainer(idx)
  local value = getValue("trn" .. idx)
  if type(value) ~= "number" then
    return nil
  end
  return value
end

local function decode_window(value)
  local decoded = round((value + 900) / 20)
  if decoded < 0 or decoded > 90 then
    return nil
  end
  return decoded
end

local function expected_checksum(profile, seq)
  return (profile * 37 + seq * 11 + 173) % 91
end

local function decode_command()
  local ch5 = read_trainer(5)
  local ch6 = read_trainer(6)
  local ch7 = read_trainer(7)
  local ch8 = read_trainer(8)
  if ch5 == nil or ch6 == nil or ch7 == nil or ch8 == nil then
    return nil
  end
  if ch8 < ARM_MAGIC_MIN then
    return nil
  end

  local profile = nil
  if ch5 < -PROFILE_THRESHOLD then
    profile = PROFILE_PPM8
  elseif ch5 > PROFILE_THRESHOLD then
    profile = PROFILE_SBUS16
  else
    return nil
  end

  local seq = decode_window(ch6)
  local checksum = decode_window(ch7)
  if seq == nil or checksum == nil then
    return nil
  end

  local expected = expected_checksum(profile, seq)
  if checksum ~= expected then
    return nil
  end

  return profile, seq, checksum
end

local function apply_profile(profile)
  model.setModule(0, { Type = 0 })
  if profile == PROFILE_PPM8 then
    model.setModule(1, { Type = 1, firstChannel = 0, channelsCount = 8 })
  elseif profile == PROFILE_SBUS16 then
    model.setModule(1, { Type = 13, firstChannel = 0, channelsCount = 16 })
  end
end

local function background()
  local profile, seq, checksum = decode_command()
  if profile == nil then
    pending_key = ""
    pending_count = 0
    return
  end

  local key = profile .. ":" .. seq .. ":" .. checksum
  if key == pending_key then
    pending_count = pending_count + 1
  else
    pending_key = key
    pending_count = 1
  end

  if pending_count >= STABLE_FRAMES and seq ~= last_seq then
    apply_profile(profile)
    last_seq = seq
  end
end

local function run()
  background()
end

return { run = run, background = background }
