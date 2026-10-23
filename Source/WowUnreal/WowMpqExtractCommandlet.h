#pragma once
#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "WowMpqExtractCommandlet.generated.h"

// Dumps stock 3.3.5 interface files from the user's MPQs for reference while matching
// the original UI (never shipped). Input: Saved/ClaudeBuild/mpqextract.txt, one MPQ path
// per line. A .toc or .xml line also extracts the files it lists/includes; "+tex" after a
// path also extracts the .blp textures those XML/Lua files name.
// Output: Saved/Reference/Extracted/<path>.
UCLASS()
class UWowMpqExtractCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    virtual int32 Main(const FString& Params) override;
};
