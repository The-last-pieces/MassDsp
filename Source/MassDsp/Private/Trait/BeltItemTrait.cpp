#include "Trait/BeltItemTrait.h"
#include "Fragments/BeltItemFragment.h"

#include "MassZoneGraphNavigationFragments.h"
#include "MassEntityTemplateRegistry.h"
#include "MassCommonFragments.h"

void UBeltItemTrait::BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const
{
    BuildContext.AddFragment_GetRef<FBeltItemFragment>();

    BuildContext.AddFragment<FMassZoneGraphCachedLaneFragment>();
    BuildContext.AddFragment<FMassZoneGraphLaneLocationFragment>();

    BuildContext.AddFragment<FTransformFragment>();
}
