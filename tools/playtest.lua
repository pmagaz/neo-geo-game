-- Drive the game from MAME and capture frames, with no window and nobody
-- watching it. This is how scrolling and animation get checked: holding a
-- button for a while and comparing the frames that come out.
--
--   mame ... -autoboot_script tools/playtest.lua -autoboot_delay 0 \
--            -snapshot_directory .shot -seconds_to_run 8
--
-- Always pass -seconds_to_run. The script asks MAME to quit once it has the
-- frames it wants, but that is a request from inside the emulated machine;
-- -seconds_to_run is enforced from outside and cannot be left hanging.
--
-- Configured through the environment, so the script itself needs no editing:
--
--   PT_HOLD    button to hold, as MAME names it   (default "P1 Right")
--   PT_START   frame to start holding it          (default 90)
--   PT_SHOTS   frames to snapshot on, comma separated (default "95,160")
--   PT_END     frame to quit on                   (default 340)
--   PT_TAPS    buttons to press briefly, as FRAME:NAME, comma separated
--
-- PT_TAPS is what gets past a menu or a page of dialogue, which advance on a
-- button going down rather than on one being held - holding it just sits on
-- the first page forever. Each tap is held PT_TAP_FRAMES frames (default 4),
-- long enough for the game to sample the pad on one of them.
--
--   PT_TAPS="40:P1 A,110:P1 A,180:P1 A" PT_HOLD="P1 Down" ...
--
-- MAME names the pad "P1 Up", "P1 Down", "P1 Left", "P1 Right", "P1 A" to
-- "P1 D", and "P1 Start"; run tools/ports.lua if a name is ever rejected.
-- They are not all in one port, which is why the lookup searches every one.

-- Only an unset variable falls back to the default. Setting one to empty has
-- to mean empty: PT_HOLD="" reads as "hold nothing", and treating that as
-- unset silently holds P1 Right for the whole run instead, which looks
-- exactly like the game moving the character by itself.
local function getenv(name, fallback)
    local v = os.getenv(name)
    if v == nil then return fallback end
    return v
end

local hold_name = getenv("PT_HOLD", "P1 Right")
local start_frame = tonumber(getenv("PT_START", "90"))
local end_frame = tonumber(getenv("PT_END", "340"))

local shots = {}
for f in string.gmatch(getenv("PT_SHOTS", "95,160"), "([^,]+)") do
    shots[tonumber(f)] = true
end

local tap_frames = tonumber(getenv("PT_TAP_FRAMES", "4"))

-- The stick and the buttons live in one port, Start in another.
local function find_field(name)
    for _, port in pairs(manager.machine.ioport.ports) do
        if port.fields[name] then return port.fields[name] end
    end
    print("playtest: no such input: " .. name)
    return nil
end

-- Every input the script drives, resolved once and keyed by name.
--
-- Looking a field up returns a fresh wrapper each time, so two taps on the
-- same button would be two objects that compare unequal - and anything that
-- tried to combine them by identity would silently treat them as different
-- inputs. Names are the stable key.
local fields = {}
local function field_for(name)
    if fields[name] == nil then
        fields[name] = find_field(name) or false
    end
    return fields[name]
end

local hold = (hold_name ~= "") and field_for(hold_name) or false

local taps = {}
for entry in string.gmatch(getenv("PT_TAPS", ""), "([^,]+)") do
    local at, name = string.match(entry, "^%s*(%d+)%s*:%s*(.-)%s*$")
    if at and field_for(name) then
        table.insert(taps, { at = tonumber(at), name = name })
    elseif not at then
        print("playtest: bad PT_TAPS entry: " .. entry)
    end
end

local frame = 0
local function tick()
    frame = frame + 1

    -- Work out what each field should be, then write each one once.
    --
    -- Several taps normally share a button - four presses of A to get through
    -- a menu and three pages of dialogue - and writing them one after another
    -- lets a later tap's zero cancel an earlier tap's press on the same frame.
    -- Only the last tap in the list would ever register.
    local want = {}
    for _, tap in ipairs(taps) do
        want[tap.name] = want[tap.name]
            or (frame >= tap.at and frame < tap.at + tap_frames)
    end
    if hold then
        want[hold_name] = want[hold_name] or frame >= start_frame
    end
    for name, on in pairs(want) do
        fields[name]:set_value(on and 1 or 0)
    end

    if shots[frame] then
        manager.machine.video:snapshot()
    end
    if frame >= end_frame then
        manager.machine:exit()
    end
end

-- Deliberately a global. add_machine_frame_notifier hands back a subscription
-- object, and dropping it unsubscribes: held in a local, it is collected
-- partway through the run and the callback simply stops being called. The
-- symptom is a run that takes the first few snapshots, never reaches PT_END
-- and sits there until -seconds_to_run kills it.
playtest_subscription = emu.add_machine_frame_notifier(tick)
