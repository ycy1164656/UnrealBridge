#include "UnrealBridgeNetworkSessionLibrary.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBridgeNetworkSessionContractTest,"UnrealBridge.Upgrade.NetworkSessionContract",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBridgeNetworkSessionContractTest::RunTest(const FString& Parameters)
{
	const FString Base=TEXT("{\"schema\":\"unrealbridge.network_session.v2\",\"run_id\":\"contract-test\",\"editor_session_id\":\"invalid\",\"topology\":\"dedicated\",\"remote_client_count\":2,\"out_lag_ms\":100,\"out_loss_percent\":2}");
	for (const FString& Input:{Base.Replace(TEXT("\"remote_client_count\":2"),TEXT("\"remote_client_count\":true")),
		Base.Replace(TEXT("\"remote_client_count\":2"),TEXT("\"remote_client_count\":1.5")),
		Base.Replace(TEXT("\"out_loss_percent\":2"),TEXT("\"out_loss_percent\":100")),
		Base.Replace(TEXT("\"dedicated\""),TEXT("\"auto\"")),Base+TEXT("x")})
		TestTrue(TEXT("Invalid typed startup refuses before mutation"),UUnrealBridgeNetworkSessionLibrary::StartNetworkSession(Input).Contains(TEXT("ValidationFailed")));
	TestTrue(TEXT("Foreign editor cannot start"),UUnrealBridgeNetworkSessionLibrary::StartNetworkSession(Base).Contains(TEXT("StaleHandle")));
	TestTrue(TEXT("Unknown session cannot stop PIE"),UUnrealBridgeNetworkSessionLibrary::StopOwnedNetworkSession(TEXT("missing"),TEXT("nonce"),TEXT("stop" )).Contains(TEXT("UnknownSession")));
	TestTrue(TEXT("Unknown session cannot late join"),UUnrealBridgeNetworkSessionLibrary::JoinNetworkClient(TEXT("missing"),TEXT("nonce"),TEXT("join"),2).Contains(TEXT("UnknownSession")));
	return true;
}
#endif
