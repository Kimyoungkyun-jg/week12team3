-- UMyAnimInstance(C++)와 같은 애니메이션 그래프를 Lua로 조립한다.
-- AMyCharacter에 이 스크립트를 붙이면 MyAnimInstance 대신 이 그래프를 쓴다.
-- 좌클릭 공격(IsAttacking)과 피격(IsHit) 변수는 AMyCharacter가 C++에서 켠다.

local Anim = "Assets/FBX/UAL1_Standard/UAL1_Standard_Armature_"

local graph
local machine

local function BuildAnimGraph()
    graph = obj:CreateAnimGraph()

    -- 에셋 (인스턴스마다 따로 로드되므로 Notify를 붙여도 다른 캐릭터와 섞이지 않는다)
    local idle    = graph:LoadSequence(Anim .. "Idle_Loop.anim.bin")
    local walk    = graph:LoadSequence(Anim .. "Walk_Loop.anim.bin")
    local running = graph:LoadSequence(Anim .. "Jog_Fwd_Loop.anim.bin")
    local punch   = graph:LoadSequence(Anim .. "Punch_Jab.anim.bin")
    local hit     = graph:LoadSequence(Anim .. "Hit_Chest.anim.bin")

    -- 발소리(Walk/Jog)와 공격 판정(Punch_Jab) Notify는 .anim.bin에 저장돼 있어 로드할 때 함께 들어온다.
    -- AnimSequence 에디터 타임라인에서 편집한다. 스크립트에서 더 붙이려면 seq:AddNotify / AddPlaySound / AddNotifyClass.

    local locomotion = graph:CreateBlendSpace()
    locomotion:AddSample(idle, 0)
    locomotion:AddSample(walk, 200)
    locomotion:AddSample(running, 550)

    -- 노드
    local locoPlayer   = graph:BlendSpacePlayer(locomotion, "GroundSpeed")   -- GroundSpeed는 엔진이 매 프레임 채운다
    local attackPlayer = graph:SequencePlayer(punch, { Loop = false })
    local hitPlayer    = graph:SequencePlayer(hit, { Loop = false })

    -- 상태 머신
    machine = graph:StateMachine()
    local sLoco   = machine:AddState("Locomotion", locoPlayer)
    local sHit    = machine:AddState("Hit", hitPlayer, { OnExit = { Bool = "IsHit", Value = false } })
    local sAttack = machine:AddState("Attack", attackPlayer, { OnExit = { Bool = "IsAttacking", Value = false } })

    machine:AddTransition(sLoco, sAttack, { Bool = "IsAttacking" })
    machine:AddTransition(sAttack, sLoco, { Finished = true })
    machine:AddTransition(nil, sHit, { Bool = "IsHit" }, { Priority = -1, Crossfade = 0.1 })   -- Any State
    machine:AddTransition(sHit, sLoco, { Finished = true })
    machine:SetInitialState(sLoco)

    graph:SetRoot(machine)
end

function BeginPlay()
    BuildAnimGraph()
end

-- 핫 리로드 시 이전 그래프의 Lua 콜백이 해제되므로 다시 조립한다
function OnReload()
    BuildAnimGraph()
end

function Tick(dt)
end
