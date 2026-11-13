local normalArmLength = 2.8
local normalFOV = 80.0
local aimArmLength = 1.0
local aimFOV = 70.0

-- nil이면 조준 상태에 따라 자동 보간한다. 0.0~1.0으로 바꾸면 그 값으로 고정한다.
local previewAimBlendAlpha = nil

local function ApplyTuning()
    assert(obj:SetCameraTuning(normalArmLength, normalFOV, aimArmLength, aimFOV))
    if previewAimBlendAlpha == nil then
        obj:ClearAimBlendAlphaOverride()
    else
        assert(obj:SetAimBlendAlphaOverride(previewAimBlendAlpha))
    end
end

function BeginPlay()
    ApplyTuning()
end

function OnReload()
    ApplyTuning()
end
