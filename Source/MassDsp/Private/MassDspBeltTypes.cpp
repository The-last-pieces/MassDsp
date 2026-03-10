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

// ============================================================
//  Dubins 解析采样 —— 不依赖 USplineComponent
//  ZLiftTotal = BuildBeltSplineFromDubins 的 20cm ZLift
//             + BakeLUT/ComputeBoundsOnly 的 FGameConst::ZOffset(30cm)
//             = 50cm 合计，与原 Spline 管线完全一致
// ============================================================

namespace
{
    static void DubBeltSegs(EDubinsWordType W, EDubinsSegType Out[3])
    {
        switch (W)
        {
        case EDubinsWordType::LSL: Out[0]=EDubinsSegType::Left;  Out[1]=EDubinsSegType::Straight; Out[2]=EDubinsSegType::Left;  break;
        case EDubinsWordType::RSR: Out[0]=EDubinsSegType::Right; Out[1]=EDubinsSegType::Straight; Out[2]=EDubinsSegType::Right; break;
        case EDubinsWordType::LSR: Out[0]=EDubinsSegType::Left;  Out[1]=EDubinsSegType::Straight; Out[2]=EDubinsSegType::Right; break;
        case EDubinsWordType::RSL: Out[0]=EDubinsSegType::Right; Out[1]=EDubinsSegType::Straight; Out[2]=EDubinsSegType::Left;  break;
        case EDubinsWordType::LRL: Out[0]=EDubinsSegType::Left;  Out[1]=EDubinsSegType::Right;    Out[2]=EDubinsSegType::Left;  break;
        case EDubinsWordType::RLR: Out[0]=EDubinsSegType::Right; Out[1]=EDubinsSegType::Left;     Out[2]=EDubinsSegType::Right; break;
        default: Out[0]=Out[1]=Out[2]=EDubinsSegType::Straight; break;
        }
    }

    static void DubBeltStep(FVector2D& Pos, float& H, EDubinsSegType Seg, float r, float t)
    {
        if (Seg == EDubinsSegType::Straight)
        {
            Pos.X += t * FMath::Cos(H);
            Pos.Y += t * FMath::Sin(H);
        }
        else if (Seg == EDubinsSegType::Left)
        {
            const float dT = t / r;
            const FVector2D C(Pos.X - r * FMath::Sin(H), Pos.Y + r * FMath::Cos(H));
            const float newH = H + dT;
            Pos = FVector2D(C.X + r * FMath::Sin(newH), C.Y - r * FMath::Cos(newH));
            H = newH;
        }
        else // Right
        {
            const float dT = t / r;
            const FVector2D C(Pos.X + r * FMath::Sin(H), Pos.Y - r * FMath::Cos(H));
            const float newH = H - dT;
            Pos = FVector2D(C.X - r * FMath::Sin(newH), C.Y + r * FMath::Cos(newH));
            H = newH;
        }
    }

    // Dubins 路径解析求值器：每次 Eval() 调用 O(1)，构造时预计算两次 DubBeltStep
    struct FDubinsEvaluator
    {
        const FDubinsPathData& D;
        float r;
        EDubinsSegType Types[3];
        float CumLen[3];        // [0]=SegLen[0], [1]=SegLen[0]+SegLen[1], [2]=TotalLength
        FVector2D Seg1Pos, Seg2Pos;
        float Seg1H = 0.f, Seg2H = 0.f;
        float StartExtLen = 0.f, EndExtLen = 0.f;
        float dZdCm = 0.f;

        explicit FDubinsEvaluator(const FDubinsPathData& InD)
            : D(InD), r(InD.TurningRadius)
        {
            DubBeltSegs(D.WordType, Types);
            CumLen[0] = D.SegLen[0];
            CumLen[1] = D.SegLen[0] + D.SegLen[1];
            CumLen[2] = D.TotalLength;

            if (D.bHasStartExtend)
                StartExtLen = FVector::Dist(D.StartExtendPos, FVector(D.StartPos.X, D.StartPos.Y, D.StartZ));
            if (D.bHasEndExtend)
                EndExtLen = FVector::Dist(FVector(D.EndPos.X, D.EndPos.Y, D.EndZ), D.EndExtendPos);

            dZdCm = D.TotalLength > 0.f ? (D.EndZ - D.StartZ) / D.TotalLength : 0.f;

            // 预计算 seg1/seg2 起点状态（仅两次 DubBeltStep，构造时只算一次）
            Seg1Pos = D.StartPos; Seg1H = D.StartHeading;
            DubBeltStep(Seg1Pos, Seg1H, Types[0], r, D.SegLen[0]);
            Seg2Pos = Seg1Pos;  Seg2H = Seg1H;
            DubBeltStep(Seg2Pos, Seg2H, Types[1], r, D.SegLen[1]);
        }

        void Eval(float globalDist, FVector& OutPos, FVector& OutTangent) const
        {
            constexpr float ZLiftTotal = 20.f + 30.f; // ZLift(Dubins build) + ZOffset

            const float dubinsEnd = StartExtLen + D.TotalLength;

            // ── StartExtend 直线段 ─────────────────────────────────────────────
            if (D.bHasStartExtend && globalDist < StartExtLen)
            {
                const FVector A(D.StartExtendPos.X, D.StartExtendPos.Y, D.StartExtendPos.Z + ZLiftTotal);
                const FVector B(D.StartPos.X, D.StartPos.Y, D.StartZ + ZLiftTotal);
                const float alpha = StartExtLen > 0.f ? FMath::Clamp(globalDist / StartExtLen, 0.f, 1.f) : 1.f;
                OutPos     = FMath::Lerp(A, B, alpha);
                OutTangent = (B - A).GetSafeNormal();
                return;
            }

            // ── EndExtend 直线段 ──────────────────────────────────────────────
            if (D.bHasEndExtend && globalDist >= dubinsEnd)
            {
                const FVector C(D.EndPos.X, D.EndPos.Y, D.EndZ + ZLiftTotal);
                const FVector Dend(D.EndExtendPos.X, D.EndExtendPos.Y, D.EndExtendPos.Z + ZLiftTotal);
                const float rem   = globalDist - dubinsEnd;
                const float alpha = EndExtLen > 0.f ? FMath::Clamp(rem / EndExtLen, 0.f, 1.f) : 1.f;
                OutPos     = FMath::Lerp(C, Dend, alpha);
                OutTangent = (Dend - C).GetSafeNormal();
                return;
            }

            // ── Dubins 三段（圆弧 + 直线）────────────────────────────────────
            const float dubinsDist = FMath::Clamp(globalDist - StartExtLen, 0.f, D.TotalLength);
            const float Z          = D.StartZ + dZdCm * dubinsDist + ZLiftTotal;

            FVector2D pos2D; float H; EDubinsSegType type; float rem;
            if (dubinsDist <= CumLen[0])
                { pos2D = D.StartPos;  H = D.StartHeading; type = Types[0]; rem = dubinsDist; }
            else if (dubinsDist <= CumLen[1])
                { pos2D = Seg1Pos;     H = Seg1H;           type = Types[1]; rem = dubinsDist - CumLen[0]; }
            else
                { pos2D = Seg2Pos;     H = Seg2H;           type = Types[2]; rem = dubinsDist - CumLen[1]; }

            DubBeltStep(pos2D, H, type, r, rem);
            OutPos     = FVector(pos2D.X, pos2D.Y, Z);
            OutTangent = FVector(FMath::Cos(H), FMath::Sin(H), dZdCm).GetSafeNormal();
        }
    };
} // anonymous namespace

void FBeltTrajectory::ComputeBoundsOnly(const FDubinsPathData& DPath, float CoarseStep)
{
    if (!DPath.IsValid() || TotalLength <= 0.f) return;

    const FDubinsEvaluator Ev(DPath);
    const float Step       = FMath::Max(CoarseStep, 1.f);
    const int32 NumSamples = FMath::CeilToInt(TotalLength / Step) + 1;

    TArray<FVector> Positions;
    Positions.SetNumUninitialized(NumSamples);
    for (int32 i = 0; i < NumSamples; ++i)
    {
        FVector Tangent;
        Ev.Eval(FMath::Min(static_cast<float>(i) * Step, TotalLength), Positions[i], Tangent);
    }

    RepresentativePosition = Positions[NumSamples / 2];
    BoundRadius = 0.f;
    for (const FVector& P : Positions)
        BoundRadius = FMath::Max(BoundRadius, FVector::Dist(RepresentativePosition, P));
    BoundRadius += 200.f;
}

void FBeltTrajectory::BakeLUT(const FDubinsPathData& DPath, float Step)
{
    if (!DPath.IsValid() || TotalLength <= 0.f) return;

    const FDubinsEvaluator Ev(DPath);
    LUTStep            = FMath::Max(Step, 1.f);
    const int32 NumSamples = FMath::CeilToInt(TotalLength / LUTStep) + 1;
    LUT.SetNumUninitialized(NumSamples);

    for (int32 i = 0; i < NumSamples; ++i)
    {
        const float Dist = FMath::Min(static_cast<float>(i) * LUTStep, TotalLength);
        FVector Pos, Tangent;
        Ev.Eval(Dist, Pos, Tangent);
        LUT[i].Position = Pos;
        LUT[i].Rotation = FQuat(Tangent.Rotation());
    }

    RepresentativePosition = LUT[NumSamples / 2].Position;
    BoundRadius = 0.f;
    for (const FBeltLUTSample& Sample : LUT)
        BoundRadius = FMath::Max(BoundRadius, FVector::Dist(RepresentativePosition, Sample.Position));
    BoundRadius += 200.f;
}

void FBeltTrajectory::BakeLUTForLOD(const FDubinsPathData& DPath, int32 LODLevel)
{
    static constexpr float LODSteps[] = {20.f, 50.f, 150.f, 500.f};
    BakeLUT(DPath, LODSteps[FMath::Clamp(LODLevel, 0, 3)]);
    CurrentLOD = FMath::Clamp(LODLevel, 0, 3);
}
