// Copyright Epic Games, Inc. All Rights Reserved.

#include "MCPRequestRouter.h"
#include "Services/IMCPService.h"
#include "Services/AssetService.h"
#include "Services/WorldService.h"
#include "Services/PythonService.h"
#include "Services/ViewportService.h"
#include "Services/ScreenshotService.h"
#include "Services/LightingService.h"
#include "Services/FoliageService.h"
#include "Services/LandscapeService.h"
#include "Services/StreamingService.h"
#include "Services/PerformanceService.h"
#include "Services/NavigationService.h"
#include "Services/GameplayService.h"
#include "Services/UtilityService.h"

namespace
{
	static FString BuildSpecialAgentInstructions()
	{
		// Balanced instructions - comprehensive but concise
	    return TEXT(
	        "SpecialAgent controls Unreal Editor. "
	        "WORKFLOW: 1) screenshot_capture to SEE viewport, 2) trace/select to GET 3D info, 3) act, 4) screenshot to VERIFY. "
	        "SCREEN COORDS: All screen tools use 0-1 percentage (0.5,0.5=center, 0.25,0.75=25% from left, 75% from top). "
	        "KEY TOOLS: "
	        "viewport_trace_from_screen(screen_x,screen_y) - get world location AND surface normal at any visible point. Use to find WHERE to place things and HOW to orient them. "
	        "utility_select_at_screen(screen_x,screen_y) - click to select actor, returns full info. "
	        "assets_get_bounds(asset_path) - get mesh dimensions, pivot_offset, bottom_z BEFORE spawning. Essential for correct placement height. "
	        "assets_get_info(asset_path) - get detailed asset info including materials, collision, LODs. "
	        "PLACEMENT: 1) trace_from_screen to get location+normal, 2) get_bounds to understand mesh pivot, 3) spawn ONE actor, 4) screenshot verify, 5) adjust rotation using normal. "
	        "ROTATION: Surface normal from trace tells you which way is 'up' for that surface - use to calculate actor rotation."
	    );
	}
}

FMCPRequestRouter::FMCPRequestRouter()
{
	// Register all services
	RegisterService(TEXT("assets"), MakeShared<FAssetService>());
	RegisterService(TEXT("world"), MakeShared<FWorldService>());
	RegisterService(TEXT("python"), MakeShared<FPythonService>());
	RegisterService(TEXT("viewport"), MakeShared<FViewportService>());
	RegisterService(TEXT("screenshot"), MakeShared<FScreenshotService>());
	RegisterService(TEXT("lighting"), MakeShared<FLightingService>());
	RegisterService(TEXT("foliage"), MakeShared<FFoliageService>());
	RegisterService(TEXT("landscape"), MakeShared<FLandscapeService>());
	RegisterService(TEXT("streaming"), MakeShared<FStreamingService>());
	RegisterService(TEXT("performance"), MakeShared<FPerformanceService>());
	RegisterService(TEXT("navigation"), MakeShared<FNavigationService>());
	RegisterService(TEXT("gameplay"), MakeShared<FGameplayService>());
	RegisterService(TEXT("utility"), MakeShared<FUtilityService>());

	UE_LOG(LogTemp, Log, TEXT("SpecialAgent: Registered %d services"), Services.Num());
}

FMCPRequestRouter::~FMCPRequestRouter()
{
}

FMCPResponse FMCPRequestRouter::RouteRequest(const FMCPRequest& Request)
{
	UE_LOG(LogTemp, Log, TEXT("SpecialAgent: RouteRequest called with method: %s"), *Request.Method);
	
	// Validate JSON-RPC version
	if (Request.JsonRpc != TEXT("2.0"))
	{
		return FMCPResponse::Error(Request.Id, -32600, TEXT("Invalid Request: jsonrpc must be '2.0'"));
	}

	// Handle MCP protocol methods
	if (Request.Method == TEXT("initialize"))
	{
		return HandleInitialize(Request);
	}
	
	if (Request.Method == TEXT("tools/list"))
	{
		return HandleToolsList(Request);
	}
	
	if (Request.Method == TEXT("tools/call"))
	{
		return HandleToolsCall(Request);
	}

	// Handle server info request
	if (Request.Method == TEXT("server/info") || Request.Method == TEXT("serverInfo"))
	{
		return HandleServerInfo(Request);
	}
	
	// Handle getInstructions - Cursor calls this to get server instructions
	// Match any method containing "instruction" (case-insensitive)
	if (Request.Method.Contains(TEXT("instruction"), ESearchCase::IgnoreCase) ||
	    Request.Method.Contains(TEXT("Instruction"), ESearchCase::IgnoreCase))
	{
		UE_LOG(LogTemp, Log, TEXT("SpecialAgent: Matched instruction method: %s"), *Request.Method);
		return HandleGetInstructions(Request);
	}
	
	// Handle resources/list - return available resources
	if (Request.Method == TEXT("resources/list"))
	{
		return HandleResourcesList(Request);
	}
	
	// Handle resources/read - read a specific resource
	if (Request.Method == TEXT("resources/read"))
	{
		return HandleResourcesRead(Request);
	}
	
	// Handle prompts/list - return available prompts
	if (Request.Method == TEXT("prompts/list"))
	{
		return HandlePromptsList(Request);
	}
	
	// Handle prompts/get - return a specific prompt
	if (Request.Method == TEXT("prompts/get"))
	{
		return HandlePromptsGet(Request);
	}
	
	// Handle notifications (requests without an ID expecting no response content)
	if (Request.Method == TEXT("notifications/initialized") || Request.Method == TEXT("initialized"))
	{
		// This is a notification - return minimal success response
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		return FMCPResponse::Success(Request.Id, Result);
	}

	// Split method into service prefix and method name
	FString ServicePrefix;
	FString MethodName;
	if (!Request.Method.Split(TEXT("/"), &ServicePrefix, &MethodName))
	{
		return FMCPResponse::Error(
			Request.Id, 
			-32601, 
			TEXT("Method not found: Invalid method format (expected 'service/method')")
		);
	}

	// Find service
	TSharedPtr<IMCPService>* ServicePtr = Services.Find(ServicePrefix);
	if (!ServicePtr || !ServicePtr->IsValid())
	{
		TSharedPtr<FJsonObject> ErrorData = MakeShared<FJsonObject>();
		ErrorData->SetStringField(TEXT("service"), ServicePrefix);
		ErrorData->SetStringField(TEXT("method"), MethodName);

		return FMCPResponse::Error(
			Request.Id,
			-32601,
			FString::Printf(TEXT("Method not found: Service '%s' is not registered"), *ServicePrefix),
			ErrorData
		);
	}

	// Route to service
	TSharedPtr<IMCPService> Service = *ServicePtr;
	return Service->HandleRequest(Request, MethodName);
}

void FMCPRequestRouter::RegisterService(const FString& ServicePrefix, TSharedPtr<IMCPService> Service)
{
	Services.Add(ServicePrefix, Service);
	UE_LOG(LogTemp, Verbose, TEXT("SpecialAgent: Registered service '%s'"), *ServicePrefix);
}

FMCPResponse FMCPRequestRouter::HandleInitialize(const FMCPRequest& Request)
{
	UE_LOG(LogTemp, Log, TEXT("SpecialAgent: HandleInitialize called, building response..."));
	
	// MCP initialization handshake
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	
	Result->SetStringField(TEXT("protocolVersion"), TEXT("2024-11-05"));
	Result->SetStringField(TEXT("instructions"), BuildSpecialAgentInstructions());
	
	TSharedPtr<FJsonObject> ServerInfo = MakeShared<FJsonObject>();
	ServerInfo->SetStringField(TEXT("name"), TEXT("SpecialAgent"));
	ServerInfo->SetStringField(TEXT("version"), TEXT("1.0.0"));
	Result->SetObjectField(TEXT("serverInfo"), ServerInfo);
	
	// Declare capabilities - tools supported, resources/prompts supported
	TSharedPtr<FJsonObject> Capabilities = MakeShared<FJsonObject>();
	
	// Tools capability
	TSharedPtr<FJsonObject> ToolsCap = MakeShared<FJsonObject>();
	ToolsCap->SetBoolField(TEXT("listChanged"), false);
	Capabilities->SetObjectField(TEXT("tools"), ToolsCap);
	
	// Resources capability
	TSharedPtr<FJsonObject> ResourcesCap = MakeShared<FJsonObject>();
	ResourcesCap->SetBoolField(TEXT("subscribe"), false);
	ResourcesCap->SetBoolField(TEXT("listChanged"), false);
	Capabilities->SetObjectField(TEXT("resources"), ResourcesCap);
	
	// Prompts capability
	TSharedPtr<FJsonObject> PromptsCap = MakeShared<FJsonObject>();
	PromptsCap->SetBoolField(TEXT("listChanged"), false);
	Capabilities->SetObjectField(TEXT("prompts"), PromptsCap);
	
	Result->SetObjectField(TEXT("capabilities"), Capabilities);
	
	UE_LOG(LogTemp, Log, TEXT("SpecialAgent: Initialize response ready, sending..."));
	
	return FMCPResponse::Success(Request.Id, Result);
}

FMCPResponse FMCPRequestRouter::HandleToolsList(const FMCPRequest& Request)
{
	// Return list of available tools
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> ToolsArray;
	
	// Collect tools from all services
	for (const auto& ServicePair : Services)
	{
		TArray<FMCPToolInfo> ServiceTools = ServicePair.Value->GetAvailableTools();
		
		for (const FMCPToolInfo& ToolInfo : ServiceTools)
		{
			TSharedPtr<FJsonObject> ToolObj = MakeShared<FJsonObject>();
					   ToolObj->SetStringField(TEXT("name"), FString::Printf(TEXT("%s_%s"), *ServicePair.Key, *ToolInfo.Name));
			ToolObj->SetStringField(TEXT("description"), ToolInfo.Description);
			
			// Add input schema
			TSharedPtr<FJsonObject> InputSchema = MakeShared<FJsonObject>();
			InputSchema->SetStringField(TEXT("type"), TEXT("object"));
			InputSchema->SetObjectField(TEXT("properties"), ToolInfo.Parameters);
			if (ToolInfo.RequiredParams.Num() > 0)
			{
				TArray<TSharedPtr<FJsonValue>> RequiredArray;
				for (const FString& Param : ToolInfo.RequiredParams)
				{
					RequiredArray.Add(MakeShared<FJsonValueString>(Param));
				}
				InputSchema->SetArrayField(TEXT("required"), RequiredArray);
			}
			
			ToolObj->SetObjectField(TEXT("inputSchema"), InputSchema);
			ToolsArray.Add(MakeShared<FJsonValueObject>(ToolObj));
		}
	}
	
	Result->SetArrayField(TEXT("tools"), ToolsArray);
	
	UE_LOG(LogTemp, Log, TEXT("SpecialAgent: Returning %d tools"), ToolsArray.Num());
	
	return FMCPResponse::Success(Request.Id, Result);
}

FMCPResponse FMCPRequestRouter::HandleToolsCall(const FMCPRequest& Request)
{
	// Execute a tool
	if (!Request.Params.IsValid())
	{
		return FMCPResponse::Error(Request.Id, -32602, TEXT("Invalid params"));
	}
	
	FString ToolName = Request.Params->GetStringField(TEXT("name"));
	TSharedPtr<FJsonObject> Arguments = Request.Params->GetObjectField(TEXT("arguments"));
	
	       // Split tool name into service_method
	       FString ServicePrefix;
	       FString MethodName;
	       if (!ToolName.Split(TEXT("_"), &ServicePrefix, &MethodName))
	       {
		       return FMCPResponse::Error(Request.Id, -32602, TEXT("Invalid tool name format"));
	       }
	
	// Find service
	TSharedPtr<IMCPService>* ServicePtr = Services.Find(ServicePrefix);
	if (!ServicePtr || !ServicePtr->IsValid())
	{
		return FMCPResponse::Error(Request.Id, -32601, FString::Printf(TEXT("Service '%s' not found"), *ServicePrefix));
	}
	
	// Create a modified request with the arguments as params
	FMCPRequest ModifiedRequest;
	ModifiedRequest.JsonRpc = Request.JsonRpc;
	ModifiedRequest.Method = ToolName;
	ModifiedRequest.Params = Arguments;
	ModifiedRequest.Id = Request.Id;
	
	// Route to service
	TSharedPtr<IMCPService> Service = *ServicePtr;
	FMCPResponse ServiceResponse = Service->HandleRequest(ModifiedRequest, MethodName);
	
	// Wrap response in MCP content format
	return WrapToolResponse(ServiceResponse, ServicePrefix, MethodName);
}

FMCPResponse FMCPRequestRouter::WrapToolResponse(const FMCPResponse& ServiceResponse, const FString& ServicePrefix, const FString& MethodName)
{
	TSharedPtr<FJsonObject> MCPResult = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> ContentArray;
	
	if (ServiceResponse.bSuccess && ServiceResponse.Result.IsValid())
	{
		// Check if this is a screenshot response with base64 data
		FString Base64Data;
		if (ServiceResponse.Result->TryGetStringField(TEXT("base64_data"), Base64Data))
		{
			// Add image content block
			TSharedPtr<FJsonObject> ImageContent = MakeShared<FJsonObject>();
			ImageContent->SetStringField(TEXT("type"), TEXT("image"));
			ImageContent->SetStringField(TEXT("data"), Base64Data);
			ImageContent->SetStringField(TEXT("mimeType"), TEXT("image/png"));
			ContentArray.Add(MakeShared<FJsonValueObject>(ImageContent));
			
			// Also add text description
			TSharedPtr<FJsonObject> TextContent = MakeShared<FJsonObject>();
			TextContent->SetStringField(TEXT("type"), TEXT("text"));
			
			int32 Width = 0, Height = 0;
			ServiceResponse.Result->TryGetNumberField(TEXT("width"), Width);
			ServiceResponse.Result->TryGetNumberField(TEXT("height"), Height);
			
			TextContent->SetStringField(TEXT("text"), 
				FString::Printf(TEXT("Screenshot captured: %dx%d"), Width, Height));
			ContentArray.Add(MakeShared<FJsonValueObject>(TextContent));
		}
		else
		{
			// Convert result to formatted JSON text
			FString ResultJson;
			TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer = 
				TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&ResultJson);
			FJsonSerializer::Serialize(ServiceResponse.Result.ToSharedRef(), Writer);
			
			TSharedPtr<FJsonObject> TextContent = MakeShared<FJsonObject>();
			TextContent->SetStringField(TEXT("type"), TEXT("text"));
			TextContent->SetStringField(TEXT("text"), ResultJson);
			ContentArray.Add(MakeShared<FJsonValueObject>(TextContent));
		}
		
		MCPResult->SetArrayField(TEXT("content"), ContentArray);
		MCPResult->SetBoolField(TEXT("isError"), false);
	}
	else
	{
		// Error response
		TSharedPtr<FJsonObject> TextContent = MakeShared<FJsonObject>();
		TextContent->SetStringField(TEXT("type"), TEXT("text"));
		
		FString ErrorMessage = TEXT("Unknown error");
		if (ServiceResponse.ErrorObject.IsValid())
		{
			ServiceResponse.ErrorObject->TryGetStringField(TEXT("message"), ErrorMessage);
		}
		
		TextContent->SetStringField(TEXT("text"), ErrorMessage);
		ContentArray.Add(MakeShared<FJsonValueObject>(TextContent));
		
		MCPResult->SetArrayField(TEXT("content"), ContentArray);
		MCPResult->SetBoolField(TEXT("isError"), true);
	}
	
	return FMCPResponse::Success(ServiceResponse.Id, MCPResult);
}

FMCPResponse FMCPRequestRouter::HandleResourcesList(const FMCPRequest& Request)
{
	// MCP resources/list - return empty list for now (resources may cause client issues)
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> Resources;
	Result->SetArrayField(TEXT("resources"), Resources);
	
	UE_LOG(LogTemp, Log, TEXT("SpecialAgent: Returning empty resources list"));
	
	return FMCPResponse::Success(Request.Id, Result);
}

FMCPResponse FMCPRequestRouter::HandleResourcesRead(const FMCPRequest& Request)
{
	// Get the URI from params
	FString Uri;
	if (Request.Params.IsValid())
	{
		Request.Params->TryGetStringField(TEXT("uri"), Uri);
	}
	
	UE_LOG(LogTemp, Log, TEXT("SpecialAgent: resources/read for URI: %s"), *Uri);
	
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> Contents;
	
	if (Uri == TEXT("mcp://instructions") || Uri.Contains(TEXT("instruction"), ESearchCase::IgnoreCase))
	{
		// Return the instructions as text content
		TSharedPtr<FJsonObject> Content = MakeShared<FJsonObject>();
		Content->SetStringField(TEXT("uri"), Uri);
		Content->SetStringField(TEXT("mimeType"), TEXT("text/plain"));
		Content->SetStringField(TEXT("text"), BuildSpecialAgentInstructions());
		Contents.Add(MakeShared<FJsonValueObject>(Content));
	}
	
	Result->SetArrayField(TEXT("contents"), Contents);
	
	return FMCPResponse::Success(Request.Id, Result);
}

FMCPResponse FMCPRequestRouter::HandlePromptsList(const FMCPRequest& Request)
{
	// MCP prompts/list - return empty list for now (prompts may cause client issues)
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> Prompts;
	Result->SetArrayField(TEXT("prompts"), Prompts);
	
	UE_LOG(LogTemp, Log, TEXT("SpecialAgent: Returning empty prompts list"));
	
	return FMCPResponse::Success(Request.Id, Result);
}

FMCPResponse FMCPRequestRouter::HandlePromptsGet(const FMCPRequest& Request)
{
	FString PromptName;
	TSharedPtr<FJsonObject> Arguments;
	
	if (Request.Params.IsValid())
	{
		Request.Params->TryGetStringField(TEXT("name"), PromptName);
		const TSharedPtr<FJsonObject>* ArgsObj;
		if (Request.Params->TryGetObjectField(TEXT("arguments"), ArgsObj))
		{
			Arguments = *ArgsObj;
		}
	}
	
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> Messages;
	
	if (PromptName == TEXT("explore_level"))
	{
		TSharedPtr<FJsonObject> Msg = MakeShared<FJsonObject>();
		Msg->SetStringField(TEXT("role"), TEXT("user"));
		Msg->SetStringField(TEXT("content"), TEXT(
			"Please explore the current Unreal Engine level:\n"
			"1. First, take a screenshot to see the current viewport view\n"
			"2. List all actors in the level to understand what exists\n"
			"3. Focus on interesting actors and take screenshots of them\n"
			"4. Summarize what you found in the level"
		));
		Messages.Add(MakeShared<FJsonValueObject>(Msg));
	}
	else if (PromptName == TEXT("find_actor"))
	{
		FString SearchTerm;
		if (Arguments.IsValid())
		{
			Arguments->TryGetStringField(TEXT("search_term"), SearchTerm);
		}
		
		TSharedPtr<FJsonObject> Msg = MakeShared<FJsonObject>();
		Msg->SetStringField(TEXT("role"), TEXT("user"));
		       Msg->SetStringField(TEXT("content"), FString::Printf(TEXT(
			       "Find and focus on actors matching '%s':\n"
			       "1. List actors and filter for ones matching the search term\n"
			       "2. Use viewport_focus_actor to frame each matching actor\n"
			       "3. Take a screenshot after focusing to show me the actor\n"
			       "4. Report what you found with key details (location, bounds, etc.)"
		       ), *SearchTerm));
		Messages.Add(MakeShared<FJsonValueObject>(Msg));
	}
	else if (PromptName == TEXT("inspect_selection"))
	{
		TSharedPtr<FJsonObject> Msg = MakeShared<FJsonObject>();
		Msg->SetStringField(TEXT("role"), TEXT("user"));
		       Msg->SetStringField(TEXT("content"), TEXT(
			       "Inspect the currently selected actors:\n"
			       "1. Use utility_get_selection to see what's selected\n"
			       "2. Use utility_get_selection_bounds to get detailed bounds and orientation\n"
			       "3. Focus on each selected actor and take a screenshot\n"
			       "4. Summarize the selection with key properties"
		       ));
		Messages.Add(MakeShared<FJsonValueObject>(Msg));
	}
	else if (PromptName == TEXT("place_objects"))
	{
		FString Description;
		if (Arguments.IsValid())
		{
			Arguments->TryGetStringField(TEXT("description"), Description);
		}
		
		TSharedPtr<FJsonObject> Msg = MakeShared<FJsonObject>();
		Msg->SetStringField(TEXT("role"), TEXT("user"));
		       Msg->SetStringField(TEXT("content"), FString::Printf(TEXT(
			       "Help me place objects in the level: %s\n\n"
			       "Use Python (python_execute) with the unreal module to:\n"
			       "1. First screenshot to see the current state\n"
			       "2. Use unreal.EditorLevelLibrary or unreal.EditorAssetLibrary as needed\n"
			       "3. Place/modify the requested objects\n"
			       "4. Screenshot again to verify the results"
		       ), *Description));
		Messages.Add(MakeShared<FJsonValueObject>(Msg));
	}
	else
	{
		return FMCPResponse::Error(Request.Id, -32602, FString::Printf(TEXT("Unknown prompt: %s"), *PromptName));
	}
	
	Result->SetStringField(TEXT("description"), FString::Printf(TEXT("Prompt: %s"), *PromptName));
	Result->SetArrayField(TEXT("messages"), Messages);
	
	return FMCPResponse::Success(Request.Id, Result);
}

FMCPResponse FMCPRequestRouter::HandleServerInfo(const FMCPRequest& Request)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	
	Result->SetStringField(TEXT("name"), TEXT("SpecialAgent"));
	Result->SetStringField(TEXT("version"), TEXT("1.0.0"));
	Result->SetStringField(TEXT("protocol_version"), TEXT("2.0"));
	Result->SetStringField(TEXT("description"), TEXT("MCP Server for Unreal Engine 5"));
	Result->SetStringField(TEXT("instructions"), BuildSpecialAgentInstructions());
	
	// List available services
	TArray<TSharedPtr<FJsonValue>> ServiceArray;
	for (const auto& ServicePair : Services)
	{
		TSharedPtr<FJsonObject> ServiceObj = MakeShared<FJsonObject>();
		ServiceObj->SetStringField(TEXT("prefix"), ServicePair.Key);
		ServiceObj->SetStringField(TEXT("description"), ServicePair.Value->GetServiceDescription());
		
		ServiceArray.Add(MakeShared<FJsonValueObject>(ServiceObj));
	}
	Result->SetArrayField(TEXT("services"), ServiceArray);

	return FMCPResponse::Success(Request.Id, Result);
}

FMCPResponse FMCPRequestRouter::HandleGetInstructions(const FMCPRequest& Request)
{
	UE_LOG(LogTemp, Log, TEXT("SpecialAgent: Handling getInstructions request"));
	
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("instructions"), BuildSpecialAgentInstructions());
	
	return FMCPResponse::Success(Request.Id, Result);
}

