#include "Misc/AutomationTest.h"
#include "WowLuaVM.h"
#include "WowEventSystem.h"
extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUiExecutionBudgetRecovery,
    "WowUnreal.UI.ExecutionBudget.RepeatedEventsAndNestedLimit",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FUiExecutionBudgetRecovery::RunTest(const FString& Parameters)
{
    FWowLuaVM VM;
    if (!TestTrue(TEXT("Lua VM initializes"), VM.Initialize())) return false;
    FWowEventSystem Events;
    Events.SetLuaVM(&VM);
    Events.CreateFrameObject(1, TEXT("ExecutionBudgetTest"));
    Events.SetFrameScript(1, TEXT("OnEvent"), TEXT("local n=0; for i=1,500 do n=n+i end; BudgetCalls=(BudgetCalls or 0)+1"));
    VM.SetInstructionLimit(6000);
    for (int32 Call = 0; Call < 40; ++Call)
        if (!TestTrue(TEXT("A later UI event has its own execution budget"), Events.RunFrameScript(1, TEXT("OnEvent")))) return false;
    TestTrue(TEXT("All events completed after cumulative instructions exceeded one budget"),
        VM.ExecuteString(TEXT("assert(BudgetCalls == 40)")));

    lua_State* L = VM.GetState();
    VM.ExecuteString(TEXT("function BudgetWork() local n=0; for i=1,500 do n=n+i end end"));
    lua_pushcfunction(L, [](lua_State* State) -> int
    {
        for (int32 Call = 0; Call < 40; ++Call)
        {
            lua_getglobal(State, "BudgetWork");
            if (FWowLuaVM::ProtectedCall(State, 0, 0, 0) != 0) return lua_error(State);
        }
        return 0;
    });
    const int Result = FWowLuaVM::ProtectedCall(L, 0, 0, 0);
    TestTrue(TEXT("Nested callbacks cannot reset the budget to bypass the limit"), Result != 0);
    if (Result != 0) lua_pop(L, 1);
    TestTrue(TEXT("A subsequent independent event recovers from an exhausted budget"), Events.RunFrameScript(1, TEXT("OnEvent")));
    Events.RemoveFrameScripts(1);
    return true;
}
