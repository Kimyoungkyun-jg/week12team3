local detectionRadius = 100.0
local attackRadius = 1.2
local attackCooldown = 1.8
local walkSpeed = 6.0
local health = 10

local function ApplyTuning()
    assert(zombie:SetCombatTuning(detectionRadius, attackRadius, attackCooldown, walkSpeed))
    assert(zombie:SetHealth(health))
end

function BeginPlay()
    ApplyTuning()
end

function OnReload()
    ApplyTuning()
end
