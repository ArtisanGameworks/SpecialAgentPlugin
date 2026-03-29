// Copyright Epic Games, Inc. All Rights Reserved.

#include "SpecialAgentModule.h"
#include "MCPServer.h"
#include "MCPStatusBarWidget.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "LevelEditor.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"
#include "Styling/AppStyle.h"
#include "Framework/MultiBox/MultiBoxExtender.h"

#define LOCTEXT_NAMESPACE "FSpecialAgentModule"

void FSpecialAgentModule::StartupModule()
{
	UE_LOG(LogTemp, Log, TEXT("SpecialAgent: Module starting up"));

	// Create the MCP server instance
	MCPServer = MakeShared<FSpecialAgentMCPServer>();

	// Get config file path - plugin configs are in Game.ini, not Engine.ini
	FString ConfigFilePath = FPaths::ProjectConfigDir() / TEXT("DefaultGame.ini");
	
	// Check if auto-start is enabled in config
	bool bAutoStart = true;  // Default to true for now
	int32 ServerPort = 8767;  // HTTP/SSE port for MCP client connections
	
	// Try to read from config (may not exist yet)
	// UE 5.7: Use explicit config file path instead of GGameIni
	if (GConfig)
	{
		const FString ConfigPath = FPaths::ProjectConfigDir() / TEXT("DefaultSpecialAgent.ini");
		GConfig->GetBool(TEXT("/Script/SpecialAgent.SpecialAgentSettings"), TEXT("ServerEnabled"), bAutoStart, ConfigPath);
		GConfig->GetInt(TEXT("/Script/SpecialAgent.SpecialAgentSettings"), TEXT("ServerPort"), ServerPort, ConfigPath);
	}
	
	UE_LOG(LogTemp, Log, TEXT("SpecialAgent: ServerEnabled=%d, ServerPort=%d"), bAutoStart, ServerPort);

	if (bAutoStart)
	{
		if (MCPServer->StartServer(ServerPort))
		{
			UE_LOG(LogTemp, Log, TEXT("SpecialAgent: MCP Server started on port %d"), ServerPort);
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("SpecialAgent: Failed to start MCP Server"));
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("SpecialAgent: MCP Server auto-start is disabled"));
	}

	// Register status bar widget
	RegisterStatusBarWidget();
}

void FSpecialAgentModule::ShutdownModule()
{
	UE_LOG(LogTemp, Log, TEXT("SpecialAgent: Module shutting down"));

	UnregisterStatusBarWidget();

	if (MCPServer.IsValid())
	{
		MCPServer->StopServer();
		MCPServer.Reset();
	}
}

bool FSpecialAgentModule::IsMCPServerRunning() const
{
	return MCPServer.IsValid() && MCPServer->IsRunning();
}

void FSpecialAgentModule::RegisterStatusBarWidget()
{
	// Use UToolMenus to add our status widget to the level editor status bar
	UToolMenus* ToolMenus = UToolMenus::Get();
	if (!ToolMenus)
	{
		return;
	}

	// Register with the status bar
	const FName StatusBarName = TEXT("LevelEditor.StatusBar.ToolBar");
	UToolMenu* StatusBarMenu = ToolMenus->ExtendMenu(StatusBarName);
	
	if (StatusBarMenu)
	{
		TSharedPtr<FSpecialAgentMCPServer> Server = MCPServer;

		// UE 5.7: FindOrAddSection() was removed. Use AddSection() + FindSection() pattern.
		const FName SectionName = TEXT("SpecialAgent");
		FToolMenuSection* Section = StatusBarMenu->FindSection(SectionName);
		if (!Section)
		{
			StatusBarMenu->AddSection(SectionName, LOCTEXT("SpecialAgentSection", "SpecialAgent"));
			Section = StatusBarMenu->FindSection(SectionName);
		}

		if (Section)
		{
			Section->AddEntry(FToolMenuEntry::InitWidget(
				TEXT("MCPStatus"),
				SNew(SMCPStatusBarWidget, Server),
				FText::GetEmpty(),
				true,  // bNoIndent
				false  // bSearchable
			));

			UE_LOG(LogTemp, Log, TEXT("SpecialAgent: Status bar widget registered via ToolMenus"));
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("SpecialAgent: Could not create ToolMenu section"));
		}
	}
	else
	{
		// Fallback: Register with level editor module directly
		if (FModuleManager::Get().IsModuleLoaded("LevelEditor"))
		{
			FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor");
			
			TSharedPtr<FSpecialAgentMCPServer> Server = MCPServer;
			ToolBarExtender = MakeShareable(new FExtender);
			
			ToolBarExtender->AddToolBarExtension(
				TEXT("SourceControl"),
				EExtensionHook::After,
				nullptr,
				FToolBarExtensionDelegate::CreateLambda([Server](FToolBarBuilder& Builder)
				{
					Builder.AddWidget(SNew(SMCPStatusBarWidget, Server));
				})
			);
			
			LevelEditorModule.GetToolBarExtensibilityManager()->AddExtender(ToolBarExtender);
			UE_LOG(LogTemp, Log, TEXT("SpecialAgent: Status bar widget registered via toolbar extender"));
		}
	}
}

void FSpecialAgentModule::UnregisterStatusBarWidget()
{
	// Remove from ToolMenus
	UToolMenus* ToolMenus = UToolMenus::Get();
	if (ToolMenus)
	{
		const FName StatusBarName = TEXT("LevelEditor.StatusBar.ToolBar");
		UToolMenu* StatusBarMenu = ToolMenus->FindMenu(StatusBarName);
		if (StatusBarMenu)
		{
			StatusBarMenu->RemoveSection(TEXT("SpecialAgent"));
		}
	}

	// Remove toolbar extender if used
	if (ToolBarExtender.IsValid() && FModuleManager::Get().IsModuleLoaded("LevelEditor"))
	{
		FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor");
		LevelEditorModule.GetToolBarExtensibilityManager()->RemoveExtender(ToolBarExtender);
		ToolBarExtender.Reset();
	}
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FSpecialAgentModule, SpecialAgent)
