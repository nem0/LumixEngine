-- Simple Tetris demo for black.h Engine
-- Spawns cubes using engine/models/cube.fbx and implements basic gameplay logic

score_counter = black.h.Entity.NULL -- gui for score

local GRID_W = 10
local GRID_H = 20
local CELL_SIZE = 2.1 -- world units between cubes
local DROP_INTERVAL = 0.8 -- seconds per drop (will speed up with level)

local lmath = require "scripts/math"

local shapes = {
    -- I
    {{0,1},{1,1},{2,1},{3,1}},
    -- J
    {{0,0},{0,1},{1,1},{2,1}},
    -- L
    {{2,0},{0,1},{1,1},{2,1}},
    -- O
    {{1,0},{2,0},{1,1},{2,1}},
    -- S
    {{1,0},{2,0},{0,1},{1,1}},
    -- Z
    {{0,0},{1,0},{1,1},{2,1}},
    -- T
    {{1,0},{0,1},{1,1},{2,1}},
}

local grid = {} -- grid[x][y] = entity or nil (1-based x,y)
local entities = {} -- map of entity ids for cleanup

local current = nil -- {x,y,shape,rotation,blocks = {{x,y},...}}
local drop_timer = 0
local level = 1
local score = 0

local function worldPosFromCell(cx, cy)
    local wx = (cx - (GRID_W/2) - 0.5) * CELL_SIZE
    local wy = (cy - 1) * CELL_SIZE + 2 -- lift up a bit
    return {wx, wy, 0}
end

local function createCubeAt(cx, cy)
    local pos = worldPosFromCell(cx, cy)
    local e = Editor.createEntityEx{
        position = pos,
        model_instance = { source = "engine/models/cube.fbx" },
    }
    return e
end

local function clearGridTable()
    for x = 1, GRID_W do
        grid[x] = {}
        for y = 1, GRID_H do
            grid[x][y] = nil
        end
    end
end

local function spawnPiece()
    local s = shapes[math.random(#shapes)]
    -- deep copy blocks
    local blocks = {}
    for i=1,#s do blocks[i] = {s[i][1], s[i][2]} end
    local px = math.floor(GRID_W/2) - 1
    local py = GRID_H - 3
    current = {x = px, y = py, blocks = blocks, rotation = 0, entity_ids = {}}
    -- create visual cubes for current piece
    for i, b in ipairs(current.blocks) do
        local cx = current.x + b[1] + 1
        local cy = current.y + b[2] + 1
        local e = createCubeAt(cx, cy)
        table.insert(current.entity_ids, e)
    end
end

local function destroyEntities(list)
    for _, e in ipairs(list) do
        if e then e:destroy() end
    end
end

local function pieceCellsAt(offx, offy, blocks)
    local cells = {}
    for _, b in ipairs(blocks) do
        table.insert(cells, {x = offx + b[1] + 1, y = offy + b[2] + 1})
    end
    return cells
end

local function isCellFree(cx, cy)
    if cx < 1 or cx > GRID_W or cy < 1 then return false end
    if cy > GRID_H then return true end
    return grid[cx][cy] == nil
end

local function canPlace(offx, offy, blocks)
    for _, c in ipairs(pieceCellsAt(offx, offy, blocks)) do
        if not isCellFree(c.x, c.y) then return false end
    end
    return true
end

local function lockPiece()
    if not current then return end
    local cells = pieceCellsAt(current.x, current.y, current.blocks)
    for i,c in ipairs(cells) do
        local e = current.entity_ids[i]
        -- ensure entity exists and register into grid
        if e then
            grid[c.x][c.y] = e
        end
    end
    current = nil
end

local function clearLines()
    local removed = 0
    local y = 1
    while y <= GRID_H do
        local full = true
        for x = 1, GRID_W do
            if not grid[x][y] then full = false; break end
        end
        if full then
            removed = removed + 1
            -- destroy row entities
            for x = 1, GRID_W do
                if grid[x][y] then grid[x][y]:destroy(); grid[x][y] = nil end
            end
            -- shift down all rows above
            for yy = y, GRID_H - 1 do
                for x = 1, GRID_W do
                    grid[x][yy] = grid[x][yy + 1]
                    grid[x][yy + 1] = nil
                    if grid[x][yy] then
                        local pos = worldPosFromCell(x, yy)
                        grid[x][yy].position = pos
                    end
                end
            end
            -- do not increment y, re-check the same position
        else
            y = y + 1
        end
    end
    if removed > 0 then
        score = score + removed * 100
        score_counter.gui_text.text = "Score: " .. tostring(score)
    end
end

local function rotateBlocksCW(blocks)
    local res = {}
    for i,b in ipairs(blocks) do
        table.insert(res, {-b[2], b[1]})
    end
    return res
end

function start()
    math.randomseed(os.time())
    clearGridTable()
    spawnPiece()
    drop_timer = 0
end

function update(dt)
    drop_timer = drop_timer + dt
    local interval = DROP_INTERVAL / (1 + (level-1)*0.1)
    if drop_timer >= interval then
        drop_timer = drop_timer - interval
        if current then
            if canPlace(current.x, current.y - 1, current.blocks) then
                -- move down
                current.y = current.y - 1
                -- update visuals
                for i,b in ipairs(current.blocks) do
                    local cx = current.x + b[1] + 1
                    local cy = current.y + b[2] + 1
                    local e = current.entity_ids[i]
                    if e then e.position = worldPosFromCell(cx, cy) end
                end
            else
                lockPiece()
                spawnPiece()
            end
        end
    end
end

-- Simple cleanup helper (call from console if needed)
function unload()
    if current and current.entity_ids then destroyEntities(current.entity_ids); current = nil end
    for x=1,GRID_W do for y=1,GRID_H do if grid[x][y] then grid[x][y]:destroy(); grid[x][y] = nil end end end
end

-- Expose a small status function
function status()
    print("Tetris: level=" .. tostring(level) .. " score=" .. tostring(score))
end

local function updateCurrentEntities()
    if not current then return end
    for i,b in ipairs(current.blocks) do
        local cx = current.x + b[1] + 1
        local cy = current.y + b[2] + 1
        local e = current.entity_ids[i]
        if e then e.position = worldPosFromCell(cx, cy) end
    end
end

function onInputEvent(event : InputEvent)
    if event.type ~= "button" or event.device.type ~= "keyboard" then return end
    if not current then return end

    local k = event.key_id
    -- Move left (A)
    if k == black.hAPI.Keycode.A then
        if event.down and canPlace(current.x - 1, current.y, current.blocks) then
            current.x = current.x - 1
            updateCurrentEntities()
        end
    end
    -- Move right (D)
    if k == black.hAPI.Keycode.D then
        if event.down and canPlace(current.x + 1, current.y, current.blocks) then
            current.x = current.x + 1
            updateCurrentEntities()
        end
    end
    -- Soft drop (S)
    if k == black.hAPI.Keycode.S then
        if event.down then
            if canPlace(current.x, current.y - 1, current.blocks) then
                current.y = current.y - 1
                updateCurrentEntities()
            else
                lockPiece()
                clearLines()
                spawnPiece()
            end
        end
    end
    -- Rotate clockwise (E)
    if k == black.hAPI.Keycode.E then
        if event.down then
            local nb = rotateBlocksCW(current.blocks)
            if canPlace(current.x, current.y, nb) then
                current.blocks = nb
                updateCurrentEntities()
            end
        end
    end
    -- Hard drop (Space)
    if k == black.hAPI.Keycode.SPACE then
        if event.down then
            while canPlace(current.x, current.y - 1, current.blocks) do
                current.y = current.y - 1
            end
            updateCurrentEntities()
            lockPiece()
            clearLines()
            spawnPiece()
        end
    end
end

-- Note: Controls bound: A = left, D = right, S = soft drop, E = rotate, Space = hard drop
