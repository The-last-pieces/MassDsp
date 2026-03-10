#include "MassDspBeltTypes.h"

#include "GameConst.h"
#include "MassCommonFragments.h"

bool FBeltTrajectory::IsValid() const
{
    return !LUT.IsEmpty();
}

void FBeltTrajectory::ComputeBoundsOnly(const USplineComponent* Spline, float CoarseStep)
{
    if (!Spline || TotalLength <= 0.f) return;

    const float Step = FMath::Max(CoarseStep, 1.0f);
    const int32 NumSamples = FMath::CeilToInt(TotalLength / Step) + 1;

    // 使用栈上小数组避免堆分配（粗采样点通常很少）
    TArray<FVector> Positions;
    Positions.SetNumUninitialized(NumSamples);
    for (int32 i = 0; i < NumSamples; ++i)
    {
        const float Dist = FMath::Min(static_cast<float>(i) * Step, TotalLength);
        FVector Pos = Spline->GetLocationAtDistanceAlongSpline(Dist, ESplineCoordinateSpace::World);
        Pos.Z += FGameConst::ZOffset;
        Positions[i] = Pos;
    }

    RepresentativePosition = Positions[NumSamples / 2];
    BoundRadius = 0.f;
    for (const FVector& P : Positions)
        BoundRadius = FMath::Max(BoundRadius, FVector::Dist(RepresentativePosition, P));
    BoundRadius += 200.f;
}

void FBeltTrajectory::UnloadLUT()
{
    LUT.Empty(); // 释放内存（不同于 Reset，Empty 也归还容量）
    CurrentLOD = -1;
}

void FBeltTrajectory::BakeLUTForLOD(const USplineComponent* Spline, int32 LODLevel)
{
    // LOD 等级 → LUT 采样步长映射
    // LOD0(<30m)=20cm, LOD1(<80m)=50cm, LOD2(<200m)=150cm, LOD3(>=200m)=500cm
    static constexpr float LODSteps[] = {20.f, 50.f, 150.f, 500.f};
    const float Step = LODSteps[FMath::Clamp(LODLevel, 0, 3)];
    BakeLUT(Spline, Step);
    CurrentLOD = FMath::Clamp(LODLevel, 0, 3);
}

void FBeltTrajectory::BakeLUT(const USplineComponent* Spline, float Step)
{
    if (!Spline || TotalLength <= 0.f) return;

    LUTStep = FMath::Max(Step, 1.0f);

    // 采样数 = ceil(TotalLength / LUTStep) + 1，确保末端点也被覆盖
    const int32 NumSamples = FMath::CeilToInt(TotalLength / LUTStep) + 1;
    LUT.SetNumUninitialized(NumSamples);

    for (int32 i = 0; i < NumSamples; ++i)
    {
        const float Dist = FMath::Min(static_cast<float>(i) * LUTStep, TotalLength);

        FVector Pos = Spline->GetLocationAtDistanceAlongSpline(Dist, ESplineCoordinateSpace::World);
        Pos.Z += FGameConst::ZOffset;

        const FVector Tangent = Spline->GetTangentAtDistanceAlongSpline(Dist, ESplineCoordinateSpace::World).GetSafeNormal();

        LUT[i].Position = Pos;
        LUT[i].Rotation = FQuat(Tangent.Rotation());
    }

    // 取 LUT 中点作为代表位置，并计算包围球半径（所有采样点到中点的最大距离）
    RepresentativePosition = LUT[NumSamples / 2].Position;
    BoundRadius = 0.f;
    for (const FBeltLUTSample& Sample : LUT)
        BoundRadius = FMath::Max(BoundRadius, FVector::Dist(RepresentativePosition, Sample.Position));
    // 额外加一点裕量，避免边界处物品闪烁
    BoundRadius += 200.f;
}

void FBeltTrajectory::GetTransformAtDistance(float Distance, FTransform& OutTransform) const
{
    // --- 快速路径：LUT 查表 + 线性插值（O(1)，无 UObject 访问，线程安全）---
    if (!LUT.IsEmpty())
    {
        const float ClampedDist = FMath::Clamp(Distance, 0.f, TotalLength);
        const float FloatIdx = ClampedDist / LUTStep;
        const int32 Idx0 = FMath::FloorToInt(FloatIdx);
        const int32 Idx1 = FMath::Min(Idx0 + 1, LUT.Num() - 1);
        const float Alpha = FloatIdx - static_cast<float>(Idx0);

        const FBeltLUTSample& S0 = LUT[Idx0];
        const FBeltLUTSample& S1 = LUT[Idx1];

        OutTransform.SetLocation(FMath::Lerp(S0.Position, S1.Position, Alpha));
        OutTransform.SetRotation(FQuat::Slerp(S0.Rotation, S1.Rotation, Alpha));
        OutTransform.SetScale3D(FVector(1.f, 1.f, 0.2f));
        return;
    }

    // LUT 未加载（传送带在视距外）：Scale=0 告知 ISM 不渲染此实例
    OutTransform.SetLocation(RepresentativePosition);
    OutTransform.SetRotation(FQuat::Identity);
    OutTransform.SetScale3D(FVector::ZeroVector);
}
