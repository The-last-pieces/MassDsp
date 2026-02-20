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
