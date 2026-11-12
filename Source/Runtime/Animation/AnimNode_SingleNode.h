#pragma once

#include "AnimationAsset.h"
#include "AnimNode_SequencePlayer.h"
#include "AnimNode_BlendSpacePlayer.h"
#include "UObject/Casts.h"

struct FAnimNode_SingleNode : public FAnimNode_Base
{

public:
    bool bPlaying = false;

    void SetAnimationAsset(UAnimationAsset* Asset, bool bLoop, float PlayRate)
    {
        // 이전 에셋 연결 해제
        ActivePlayer = nullptr;
        SequencePlayer.Sequence = nullptr;
        BlendSpacePlayer.BlendSpace = nullptr;

        if (auto* Sequence = Cast<UAnimSequence>(Asset))
        {
            SequencePlayer.Sequence = Sequence;
            SequencePlayer.CurrentTime = 0.0f;
            SequencePlayer.bLooping = bLoop;
            SequencePlayer.PlayRate = PlayRate;

            ActivePlayer = &SequencePlayer;
        }
        else if (auto* BlendSpace = Cast<UBlendSpace1D>(Asset))
        {
            BlendSpacePlayer.BlendSpace = BlendSpace;
            BlendSpacePlayer.Phase = 0.0f;
            BlendSpacePlayer.PlayRate = PlayRate;

            ActivePlayer = &BlendSpacePlayer;
        }
    }

    void SetBlendSpaceInput(float Value)
    {
        BlendSpacePlayer.X = Value;
    }

    void SetLooping(bool bLoop)
    {
        SequencePlayer.bLooping = bLoop;
        BlendSpacePlayer.bLooping = bLoop;
    }

    void SetPlayRate(float Rate)
    {
        SequencePlayer.PlayRate = Rate;
        BlendSpacePlayer.PlayRate = Rate;
    }

    float GetLength() const
    {
        if (SequencePlayer.Sequence && SequencePlayer.Sequence->GetDataModel())
        {
            return SequencePlayer.Sequence->GetDataModel()->GetPlayLength();
        }

        FBlendSample A;
        FBlendSample B;
        float Alpha = 0.0f;

        if (BlendSpacePlayer.BlendSpace 
            && BlendSpacePlayer.BlendSpace->GetSamples(BlendSpacePlayer.X, A, B, Alpha) 
            && A.Animation->GetDataModel() 
            && B.Animation->GetDataModel())
        {
            return FMath::Lerp(A.Animation->GetDataModel()->GetPlayLength(), B.Animation->GetDataModel()->GetPlayLength(), Alpha);
        }
        return 0.0f;
    }

    float GetCurrentTime() const
    {
        return SequencePlayer.Sequence 
            ? SequencePlayer.CurrentTime 
            : BlendSpacePlayer.Phase * GetLength();
    }

    void SetPosition(float Time)
    {
        const float Length = GetLength();
        SequencePlayer.CurrentTime = FMath::Clamp(Time, 0.0f, Length);
        BlendSpacePlayer.Phase = Length > 0.0f 
            ? FMath::Clamp(Time / Length, 0.0f, 1.0f) 
            : 0.0f;
    }

    void Initialize(const FAnimationInitializeContext& Context) override
    {
        if (ActivePlayer)
        {
            ActivePlayer->Initialize(Context);
        }
    }

    void Update(const FAnimationUpdateContext& Context) override
    {
        if (ActivePlayer && bPlaying)
        {
            ActivePlayer->Update(Context);
            const bool bLoop = SequencePlayer.Sequence ? SequencePlayer.bLooping : BlendSpacePlayer.bLooping;
            const float Rate = SequencePlayer.Sequence ? SequencePlayer.PlayRate : BlendSpacePlayer.PlayRate;
            if (!bLoop && ((Rate > 0.0f && GetCurrentTime() >= GetLength()) || (Rate < 0.0f && GetCurrentTime() <= 0.0f)))
            {
                bPlaying = false;
            }
        }
    }

    void Evaluate(FPoseContext& Output) override
    {
        if (ActivePlayer)
        {
            ActivePlayer->Evaluate(Output);
        }
        else
        {
            Output.ResetToRefPose();
        }
    }

private:
    FAnimNode_SequencePlayer SequencePlayer;
    FAnimNode_BlendSpacePlayer BlendSpacePlayer;

    FAnimNode_Base* ActivePlayer = nullptr;
};
