#pragma once
#include "Styling/SlateBrush.h"
#include "UObject/GCObject.h"

// Native Slate widgets are not UObjects. Their brushes must report texture
// references explicitly; a TObjectPtr inside an untracked brush is not a GC root.
class FWowRetainedSlateBrush final : public FSlateBrush, public FGCObject
{
public:
    virtual void AddReferencedObjects(FReferenceCollector& Collector) override
    { FSlateBrush::AddReferencedObjects(Collector); }
    virtual FString GetReferencerName() const override
    { return TEXT("FWowRetainedSlateBrush"); }
};
