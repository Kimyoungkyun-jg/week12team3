-- 애니메이션 상태 머신을 Lua로 정한다. 캐릭터(스켈레탈 메시가 있는 액터)에 붙인다.
-- C++(ULuaAnimInstance)은 상태별 포즈 재생과 블렌드만 맡고, 어느 상태로 갈지는 전부 이 스크립트가 정한다.
--
-- 조작: WASD 이동, Space 점프(캐릭터 기본 입력), F 공격, H 피격 시험
--
-- 엔진은 포즈를 진행하기 직전에 AnimTick(dt)을 부른다. 판단을 Tick이 아니라 AnimTick에서 해야
-- 같은 프레임에 판단 → 재생 순서가 지켜진다.

local BASE = "Models/UnrealModel/"
-- 피격 전용 클립이 없어 착지 클립으로 대신한다. 피격 클립을 임포트하면 이 경로만 바꾼다.
local HIT_CLIP = BASE .. "AS_MM_Land.fbxbin"

local anim = nil
local current = nil
local lastJumpCounter = 0
local pendingHit = false

local States = {}

-- 공중 동작. 이 상태들은 떨어지는 중이어도 FallLoop로 끊지 않는다.
local Airborne = { Jump = true, FallLoop = true, Hit = true }

-- 상태를 바꾼다. exit → 전환 요청 → enter 순서로 부른다.
local function Change(name, blend, restart)
    if current and States[current].exit then States[current].exit() end
    current = name
    anim:TransitionTo(name, blend, restart)
    if States[name].enter then States[name].enter() end
end

States.Locomotion = {
    update = function(dt, input)
        -- simple.blendspace: X = 이동 방향(도), Y = 속력
        anim:SetInput("Locomotion", anim.Direction, math.min(anim.Speed, 600))
        if input.attack and anim.IsGrounded then return "Attack", 0.08 end
    end,
}

States.Jump = {
    update = function(dt, input)
        if anim.IsGrounded then return "Land", 0.08 end
        if anim:IsNearEnd("Jump", 0.12) then return "FallLoop", 0.12 end
    end,
}

States.FallLoop = {
    update = function(dt, input)
        if anim.IsGrounded then return "Land", 0.08 end
    end,
}

States.Land = {
    update = function(dt, input)
        -- 착지 후 바로 움직이면 클립 끝을 기다리지 않는다
        if anim.Speed > 30 then return "Locomotion", 0.05 end
        if anim:IsNearEnd("Land", 0.12) then return "Locomotion", 0.12 end
    end,
}

States.Attack = {
    enter = function() print("[LuaAnim] Attack start") end,
    update = function(dt, input)
        if anim:IsNearEnd("Attack", 0.15) then return "Locomotion", 0.15 end
    end,
    exit = function() print("[LuaAnim] Attack end") end,
}

States.Hit = {
    update = function(dt, input)
        if anim:IsNearEnd("Hit", 0.10) then return "Locomotion", 0.10 end
    end,
}

-- 바깥에서 피격을 알릴 때 부른다(예: 다른 스크립트, 오버랩 이벤트)
function OnHit()
    pendingHit = true
end

function BeginPlay()
    -- 엔진은 기본 폰일 때만 커서를 잠근다. 캐릭터도 시작하자마자 마우스로 시점을 돌리게 한다(PIE에서 Shift+F1로 해제).
    SetCursorMode(CursorMode.Locked)

    anim = obj:GetAnimStateMachine()
    if not anim then
        print("[LuaAnim] 스켈레탈 메시가 없는 액터입니다: " .. obj.Name)
        return
    end
    -- 처음 등록한 상태가 시작 상태다
    anim:AddBlendSpaceState("Locomotion", "Animations/simple.blendspace")
    anim:AddSequenceState("Jump", BASE .. "AS_MM_Jump.fbxbin", false)
    anim:AddSequenceState("FallLoop", BASE .. "AS_MM_Fall_Loop.fbxbin", true)
    anim:AddSequenceState("Land", BASE .. "AS_MM_Land.fbxbin", false)
    anim:AddSequenceState("Attack", BASE .. "AS_MM_Attack_01.fbxbin", false)
    anim:AddSequenceState("Hit", HIT_CLIP, false)
    -- 등록에 실패한 상태(에셋 없음)는 쓰지 않는다
    for name, _ in pairs(States) do
        if not anim:HasState(name) then
            print("[LuaAnim] 상태를 만들지 못해 끕니다: " .. name)
            States[name] = nil
        end
    end
    lastJumpCounter = anim.JumpCounter
    current = "Locomotion"
    anim:TransitionTo("Locomotion", 0)
end

-- 스크립트를 고쳐 다시 읽으면 Lua 변수는 처음 값으로 돌아가지만 C++ 쪽 재생은 계속된다. 상태를 다시 맞춘다.
function OnReload()
    anim = obj:GetAnimStateMachine()
    if not anim then return end
    current = anim.State
    if not States[current] then current = "Locomotion"; anim:TransitionTo("Locomotion", 0.2) end
    lastJumpCounter = anim.JumpCounter
end

function AnimTick(dt)
    if not anim or not anim:IsValid() or not current then return end

    -- 이번 프레임에만 참인 입력을 한 번에 모은다
    local jumpCounter = anim.JumpCounter
    local input = {
        jumped = jumpCounter ~= lastJumpCounter,
        attack = IsKeyPressed("F"),
        hit = pendingHit or IsKeyPressed("H"),
    }
    lastJumpCounter = jumpCounter
    pendingHit = false

    -- Any State: 어느 상태에서든 피격이 먼저다. 이미 피격 중이면 처음부터 다시 튼다.
    if input.hit and States.Hit then
        Change("Hit", 0.05, true)
        return
    end

    -- 점프와 낙하도 어느 상태에서든 걸린다. 몸은 캐릭터 이동이 이미 띄웠으므로 동작이 따라가야 한다.
    -- (착지·공격 도중에 점프해도 점프 입력을 버리지 않는다)
    if input.jumped and States.Jump then
        Change("Jump", 0.08)
        return
    end
    if anim.IsFalling and not Airborne[current] and States.FallLoop then
        Change("FallLoop", 0.10)
        return
    end

    local nextState, blend = States[current].update(dt, input)
    if nextState and States[nextState] then
        Change(nextState, blend)
    end
end
