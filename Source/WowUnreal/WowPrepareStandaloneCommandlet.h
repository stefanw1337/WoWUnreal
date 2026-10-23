#pragma once
#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "WowPrepareStandaloneCommandlet.generated.h"
UCLASS()
class UWowPrepareStandaloneCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    virtual int32 Main(const FString& Params) override;
};
