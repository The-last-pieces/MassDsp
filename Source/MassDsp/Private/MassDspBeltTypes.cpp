#include "MassDspBeltTypes.h"

#include "GameConst.h"
#include "MassCommonFragments.h"

bool FBeltTrajectory::IsValid() const
{
    return SplineComponent != nullptr;
}

FVector FBeltTrajectory::GetLocationAtDistance(float Distance) const
{
    if (SplineComponent)
    {
        return SplineComponent->GetLocationAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World);
    }
    return FVector::ZeroVector;
}

FVector FBeltTrajectory::GetTangentAtDistance(float Distance) const
{
    if (SplineComponent)
    {
        return SplineComponent->GetTangentAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World);
    }
    return FVector::ForwardVector;
}

void FBeltTrajectory::ApplyTransform(FTransformFragment& Transform, float Distance) const
{
    FVector OutPos = GetLocationAtDistance(Distance);
    FVector OutTangent = GetTangentAtDistance(Distance);

    OutPos.Z += FGameConst::ZOffset;

    FTransform& TargetTransform = Transform.GetMutableTransform();
    TargetTransform.SetLocation(OutPos);
    TargetTransform.SetRotation(OutTangent.Rotation().Quaternion());
}

void FBeltTrajectory::BakeLUT(float Step)
{
    if (!SplineComponent || TotalLength <= 0.f) return;

    LUTStep = FMath::Max(Step, 1.0f);

    // 采样数 = ceil(TotalLength / LUTStep) + 1，确保末端点也被覆盖
    const int32 NumSamples = FMath::CeilToInt(TotalLength / LUTStep) + 1;
    LUT.SetNumUninitialized(NumSamples);

    for (int32 i = 0; i < NumSamples; ++i)
    {
        const float Dist = FMath::Min(static_cast<float>(i) * LUTStep, TotalLength);

        FVector Pos = SplineComponent->GetLocationAtDistanceAlongSpline(Dist, ESplineCoordinateSpace::World);
        Pos.Z += FGameConst::ZOffset;

        const FVector Tangent = SplineComponent->GetTangentAtDistanceAlongSpline(Dist, ESplineCoordinateSpace::World).GetSafeNormal();

        LUT[i].Position = Pos;
        LUT[i].Rotation  = FQuat(Tangent.Rotation());
    }
}

void FBeltTrajectory::GetTransformAtDistance(float Distance, FTransform& OutTransform) const
{
    // --- 快速路径：LUT 查表 + 线性插值（O(1)，无 UObject 访问，线程安全）---
    if (!LUT.IsEmpty())
    {
        const float ClampedDist = FMath::Clamp(Distance, 0.f, TotalLength);
        const float FloatIdx    = ClampedDist / LUTStep;
        const int32 Idx0        = FMath::FloorToInt(FloatIdx);
        const int32 Idx1        = FMath::Min(Idx0 + 1, LUT.Num() - 1);
        const float Alpha       = FloatIdx - static_cast<float>(Idx0);

        const FBeltLUTSample& S0 = LUT[Idx0];
        const FBeltLUTSample& S1 = LUT[Idx1];

        OutTransform.SetLocation(FMath::Lerp(S0.Position, S1.Position, Alpha));
        OutTransform.SetRotation(FQuat::Slerp(S0.Rotation, S1.Rotation, Alpha));
        OutTransform.SetScale3D(FVector(1.f, 1.f, 0.2f));
        return;
    }

    // --- 慢速 fallback（LUT 未烘焙时，保持原逻辑）---
    FVector OutPos = GetLocationAtDistance(Distance);
    FVector OutTangent = GetTangentAtDistance(Distance);

    OutPos.Z += FGameConst::ZOffset;

    OutTransform.SetLocation(OutPos);
    OutTransform.SetRotation(OutTangent.Rotation().Quaternion());
    OutTransform.SetScale3D(FVector(1, 1, 0.2));
}
