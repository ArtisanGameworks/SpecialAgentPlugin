// Copyright Epic Games, Inc. All Rights Reserved.

#include "MCPServer.h"
#include "MCPRequestRouter.h"
#include "JsonObjectConverter.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Misc/Guid.h"
#include "Async/Async.h"

FSpecialAgentMCPServer::FSpecialAgentMCPServer()
	: bIsRunning(false)
	, ServerPort(8767)
	, LastClientActivity(FDateTime::MinValue())
{
	RequestRouter = MakeShared<FMCPRequestRouter>();
}

FSpecialAgentMCPServer::~FSpecialAgentMCPServer()
{
	StopServer();
}

bool FSpecialAgentMCPServer::StartServer(int32 Port)
{
	if (bIsRunning)
	{
		UE_LOG(LogTemp, Warning, TEXT("SpecialAgent: MCP Server is already running"));
		return false;
	}

	ServerPort = Port;

	// Get the HTTP server module
	FHttpServerModule& HttpServerModule = FHttpServerModule::Get();

	// UE 5.7: Get router FIRST, bind routes, THEN start listeners
	HttpRouter = HttpServerModule.GetHttpRouter(ServerPort);

	if (!HttpRouter.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("SpecialAgent: Failed to get HTTP router for port %d"), ServerPort);
		return false;
	}

	// Register MCP endpoint (POST /mcp) - Main streamable HTTP endpoint
	HttpRouter->BindRoute(
		FHttpPath(TEXT("/mcp")),
		EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateRaw(this, &FSpecialAgentMCPServer::HandleMessage)
	);

	// Register SSE endpoint (GET /sse) - For SSE transport fallback
	SSERouteHandle = HttpRouter->BindRoute(
		FHttpPath(TEXT("/sse")),
		EHttpServerRequestVerbs::VERB_GET,
		FHttpRequestHandler::CreateRaw(this, &FSpecialAgentMCPServer::HandleSSEConnection)
	);
	
	// Also handle POST on /sse for streamable HTTP transport
	HttpRouter->BindRoute(
		FHttpPath(TEXT("/sse")),
		EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateRaw(this, &FSpecialAgentMCPServer::HandleMessage)
	);

	// Register message endpoint (POST /message)
	MessageRouteHandle = HttpRouter->BindRoute(
		FHttpPath(TEXT("/message")),
		EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateRaw(this, &FSpecialAgentMCPServer::HandleMessage)
	);

	// Register health endpoint (GET /health)
	HealthRouteHandle = HttpRouter->BindRoute(
		FHttpPath(TEXT("/health")),
		EHttpServerRequestVerbs::VERB_GET,
		FHttpRequestHandler::CreateRaw(this, &FSpecialAgentMCPServer::HandleHealth)
	);

	// Register OPTIONS handlers for CORS preflight on all endpoints
	HttpRouter->BindRoute(
		FHttpPath(TEXT("/mcp")),
		EHttpServerRequestVerbs::VERB_OPTIONS,
		FHttpRequestHandler::CreateRaw(this, &FSpecialAgentMCPServer::HandleCORS)
	);
	
	HttpRouter->BindRoute(
		FHttpPath(TEXT("/sse")),
		EHttpServerRequestVerbs::VERB_OPTIONS,
		FHttpRequestHandler::CreateRaw(this, &FSpecialAgentMCPServer::HandleCORS)
	);
	
	HttpRouter->BindRoute(
		FHttpPath(TEXT("/message")),
		EHttpServerRequestVerbs::VERB_OPTIONS,
		FHttpRequestHandler::CreateRaw(this, &FSpecialAgentMCPServer::HandleCORS)
	);

	// UE 5.7: Start listeners AFTER all routes are bound
	HttpServerModule.StartAllListeners();

	bIsRunning = true;
	UE_LOG(LogTemp, Log, TEXT("SpecialAgent: MCP HTTP Server started on port %d"), ServerPort);
	UE_LOG(LogTemp, Log, TEXT("SpecialAgent: SSE endpoint: http://localhost:%d/sse"), ServerPort);
	UE_LOG(LogTemp, Log, TEXT("SpecialAgent: Message endpoint: http://localhost:%d/message"), ServerPort);
	UE_LOG(LogTemp, Log, TEXT("SpecialAgent: Health endpoint: http://localhost:%d/health"), ServerPort);

	return true;
}

void FSpecialAgentMCPServer::StopServer()
{
	if (!bIsRunning)
	{
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("SpecialAgent: MCP Server stopping"));

	// Unbind routes
	if (HttpRouter.IsValid())
	{
		HttpRouter->UnbindRoute(SSERouteHandle);
		HttpRouter->UnbindRoute(MessageRouteHandle);
		HttpRouter->UnbindRoute(HealthRouteHandle);
	}

	// Clear connections
	{
		FScopeLock Lock(&ConnectionsLock);
		SSEConnections.Empty();
	}

	bIsRunning = false;
	UE_LOG(LogTemp, Log, TEXT("SpecialAgent: MCP Server stopped"));
}

FString FSpecialAgentMCPServer::GenerateSessionId()
{
	return FGuid::NewGuid().ToString();
}

bool FSpecialAgentMCPServer::HandleSSEConnection(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	UE_LOG(LogTemp, Log, TEXT("SpecialAgent: New SSE connection request"));

	// Generate session ID for this connection
	FString SessionId = GenerateSessionId();

	// Record client activity
	RecordClientActivity();

	// Build the SSE response with the endpoint event
	// MCP SSE transport expects: event: endpoint\ndata: <url>\n\n
	FString MessageEndpoint = FString::Printf(TEXT("http://localhost:%d/message?sessionId=%s"), ServerPort, *SessionId);
	
	// Format as proper SSE event
	FString SSEData = FString::Printf(
		TEXT("event: endpoint\ndata: %s\n\n"),
		*MessageEndpoint
	);

	// Create response with SSE content type
	TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(TEXT(""), TEXT("text/event-stream"));
	
	// Set required SSE headers
	Response->Headers.Add(TEXT("Cache-Control"), { TEXT("no-cache, no-store, must-revalidate") });
	Response->Headers.Add(TEXT("Connection"), { TEXT("keep-alive") });
	Response->Headers.Add(TEXT("Access-Control-Allow-Origin"), { TEXT("*") });
	Response->Headers.Add(TEXT("Access-Control-Allow-Methods"), { TEXT("GET, POST, OPTIONS") });
	Response->Headers.Add(TEXT("Access-Control-Allow-Headers"), { TEXT("Content-Type, Accept") });
	Response->Headers.Add(TEXT("X-Accel-Buffering"), { TEXT("no") });
	
	// Add the SSE event data
	FTCHARToUTF8 Converter(*SSEData);
	Response->Body.Append((const uint8*)Converter.Get(), Converter.Length());

	Response->Code = EHttpServerResponseCodes::Ok;

	UE_LOG(LogTemp, Log, TEXT("SpecialAgent: SSE endpoint event sent, session: %s, endpoint: %s"), *SessionId, *MessageEndpoint);

	OnComplete(MoveTemp(Response));

	return true;
}

bool FSpecialAgentMCPServer::HandleMessage(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	// Get session ID from query parameters (optional)
	FString SessionId;
	const FString* SessionIdValue = Request.QueryParams.Find(TEXT("sessionId"));
	if (SessionIdValue)
	{
		SessionId = *SessionIdValue;
	}

	// Get request body - handle potentially empty or malformed data
	FString BodyString;
	if (Request.Body.Num() > 0)
	{
		// Ensure null termination for string conversion
		TArray<uint8> BodyWithNull = Request.Body;
		BodyWithNull.Add(0);
		BodyString = UTF8_TO_TCHAR(reinterpret_cast<const char*>(BodyWithNull.GetData()));
	}
	
	UE_LOG(LogTemp, Log, TEXT("SpecialAgent: Received message (session: %s, size: %d): %s"), 
		*SessionId, Request.Body.Num(), *BodyString.Left(1000));

	// Record client activity for status tracking
	RecordClientActivity();

	// Handle empty body - some clients send empty POST to check connection
	if (BodyString.IsEmpty() || BodyString.TrimStartAndEnd().IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("SpecialAgent: Received empty request body"));
		
		// Return a simple acknowledgment for empty requests
		TSharedPtr<FJsonObject> AckResult = MakeShared<FJsonObject>();
		AckResult->SetStringField(TEXT("status"), TEXT("ready"));
		AckResult->SetStringField(TEXT("server"), TEXT("SpecialAgent"));
		
		FString ResponseJson;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResponseJson);
		FJsonSerializer::Serialize(AckResult.ToSharedRef(), Writer);
		
		TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(ResponseJson, TEXT("application/json"));
		Response->Headers.Add(TEXT("Access-Control-Allow-Origin"), { TEXT("*") });
		Response->Code = EHttpServerResponseCodes::Ok;
		OnComplete(MoveTemp(Response));
		return true;
	}

	// Parse the JSON-RPC request
	FMCPRequest MCPRequest;
	if (!ParseRequest(BodyString, MCPRequest))
	{
		UE_LOG(LogTemp, Error, TEXT("SpecialAgent: Failed to parse JSON: %s"), *BodyString.Left(500));
		
		FMCPResponse ErrorResponse = FMCPResponse::Error(
			TEXT(""),
			-32700,
			TEXT("Parse error: Invalid JSON")
		);

		FString ResponseJson = FormatResponse(ErrorResponse);
		TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(ResponseJson, TEXT("application/json"));
		Response->Headers.Add(TEXT("Access-Control-Allow-Origin"), { TEXT("*") });
		Response->Code = EHttpServerResponseCodes::BadRequest;
		OnComplete(MoveTemp(Response));
		return true;
	}

	// Process on game thread and send response
	AsyncTask(ENamedThreads::GameThread, [this, MCPRequest, OnComplete, SessionId]()
	{
		UE_LOG(LogTemp, Log, TEXT("SpecialAgent: Processing request on game thread: %s"), *MCPRequest.Method);
		
		FMCPResponse MCPResponse = RequestRouter->RouteRequest(MCPRequest);
		
		UE_LOG(LogTemp, Log, TEXT("SpecialAgent: RouteRequest completed for: %s"), *MCPRequest.Method);
		
		FString ResponseJson = FormatResponse(MCPResponse);

		UE_LOG(LogTemp, Log, TEXT("SpecialAgent: Response ready for %s (size=%d): %s"), 
			*MCPRequest.Method, ResponseJson.Len(), *ResponseJson.Left(300));

		TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(ResponseJson, TEXT("application/json"));
		Response->Headers.Add(TEXT("Access-Control-Allow-Origin"), { TEXT("*") });
		Response->Headers.Add(TEXT("Access-Control-Allow-Methods"), { TEXT("GET, POST, OPTIONS") });
		Response->Headers.Add(TEXT("Access-Control-Allow-Headers"), { TEXT("Content-Type") });
		Response->Code = EHttpServerResponseCodes::Ok;

		UE_LOG(LogTemp, Log, TEXT("SpecialAgent: Calling OnComplete for: %s"), *MCPRequest.Method);
		OnComplete(MoveTemp(Response));
		UE_LOG(LogTemp, Log, TEXT("SpecialAgent: OnComplete returned for: %s"), *MCPRequest.Method);
	});

	return true;
}

bool FSpecialAgentMCPServer::HandleCORS(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(TEXT(""), TEXT("text/plain"));
	Response->Headers.Add(TEXT("Access-Control-Allow-Origin"), { TEXT("*") });
	Response->Headers.Add(TEXT("Access-Control-Allow-Methods"), { TEXT("GET, POST, OPTIONS") });
	Response->Headers.Add(TEXT("Access-Control-Allow-Headers"), { TEXT("Content-Type, Accept, Authorization") });
	Response->Headers.Add(TEXT("Access-Control-Max-Age"), { TEXT("86400") });
	Response->Code = EHttpServerResponseCodes::NoContent;
	OnComplete(MoveTemp(Response));
	return true;
}

bool FSpecialAgentMCPServer::HandleHealth(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	TSharedPtr<FJsonObject> HealthObj = MakeShared<FJsonObject>();
	HealthObj->SetStringField(TEXT("status"), TEXT("healthy"));
	HealthObj->SetStringField(TEXT("server"), TEXT("SpecialAgent MCP Server"));
	HealthObj->SetStringField(TEXT("version"), TEXT("1.0.0"));
	HealthObj->SetNumberField(TEXT("port"), ServerPort);
	HealthObj->SetBoolField(TEXT("running"), bIsRunning);

	FString ResponseJson;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResponseJson);
	FJsonSerializer::Serialize(HealthObj.ToSharedRef(), Writer);

	TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(ResponseJson, TEXT("application/json"));
	Response->Headers.Add(TEXT("Access-Control-Allow-Origin"), { TEXT("*") });
	Response->Code = EHttpServerResponseCodes::Ok;

	OnComplete(MoveTemp(Response));
	return true;
}

bool FSpecialAgentMCPServer::ParseRequest(const FString& JsonString, FMCPRequest& OutRequest)
{
	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);

	if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
	{
		return false;
	}

	// Parse JSON-RPC fields
	OutRequest.JsonRpc = JsonObject->GetStringField(TEXT("jsonrpc"));
	OutRequest.Method = JsonObject->GetStringField(TEXT("method"));
	
	// Params can be object or omitted
	const TSharedPtr<FJsonObject>* ParamsObj;
	if (JsonObject->TryGetObjectField(TEXT("params"), ParamsObj))
	{
		OutRequest.Params = *ParamsObj;
	}
	else
	{
		OutRequest.Params = MakeShared<FJsonObject>();
	}

	// ID can be string or number
	const TSharedPtr<FJsonValue> IdValue = JsonObject->TryGetField(TEXT("id"));
	if (IdValue.IsValid())
	{
		if (IdValue->Type == EJson::String)
		{
			OutRequest.Id = IdValue->AsString();
		}
		else if (IdValue->Type == EJson::Number)
		{
			OutRequest.Id = FString::Printf(TEXT("%d"), (int32)IdValue->AsNumber());
		}
	}

	return true;
}

FString FSpecialAgentMCPServer::FormatResponse(const FMCPResponse& Response)
{
	TSharedPtr<FJsonObject> JsonObject = MakeShared<FJsonObject>();

	JsonObject->SetStringField(TEXT("jsonrpc"), Response.JsonRpc);
	
	// Handle ID - can be string or number based on what was sent
	if (!Response.Id.IsEmpty())
	{
		// Try to parse as number first
		if (Response.Id.IsNumeric())
		{
			JsonObject->SetNumberField(TEXT("id"), FCString::Atoi(*Response.Id));
		}
		else
		{
			JsonObject->SetStringField(TEXT("id"), Response.Id);
		}
	}
	else
	{
		JsonObject->SetField(TEXT("id"), MakeShared<FJsonValueNull>());
	}

	if (Response.bSuccess && Response.Result.IsValid())
	{
		JsonObject->SetObjectField(TEXT("result"), Response.Result);
	}
	else if (!Response.bSuccess && Response.ErrorObject.IsValid())
	{
		JsonObject->SetObjectField(TEXT("error"), Response.ErrorObject);
	}

	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer);

	return OutputString;
}

void FSpecialAgentMCPServer::SendSSEEvent(const FString& SessionId, const FString& EventType, const FString& Data)
{
	FScopeLock Lock(&ConnectionsLock);

	TSharedPtr<FSSEConnection>* ConnectionPtr = SSEConnections.Find(SessionId);
	if (ConnectionPtr && (*ConnectionPtr)->bIsValid)
	{
		FString EventData = FString::Printf(TEXT("event: %s\ndata: %s\n\n"), *EventType, *Data);
		UE_LOG(LogTemp, Verbose, TEXT("SpecialAgent: Sending SSE event to %s: %s"), *SessionId, *EventType);
	}
}

void FSpecialAgentMCPServer::BroadcastSSEEvent(const FString& EventType, const FString& Data)
{
	FScopeLock Lock(&ConnectionsLock);

	for (auto& Pair : SSEConnections)
	{
		if (Pair.Value->bIsValid)
		{
			SendSSEEvent(Pair.Key, EventType, Data);
		}
	}
}

void FSpecialAgentMCPServer::CleanupConnections()
{
	FScopeLock Lock(&ConnectionsLock);

	TArray<FString> ToRemove;
	for (auto& Pair : SSEConnections)
	{
		if (!Pair.Value->bIsValid)
		{
			ToRemove.Add(Pair.Key);
		}
	}

	for (const FString& Key : ToRemove)
	{
		SSEConnections.Remove(Key);
		UE_LOG(LogTemp, Log, TEXT("SpecialAgent: Cleaned up stale SSE connection: %s"), *Key);
	}
}

int32 FSpecialAgentMCPServer::GetConnectedClientCount() const
{
	if (!bIsRunning)
	{
		return 0;
	}

	// Consider a client "connected" if we've received activity recently
	FTimespan TimeSinceActivity = FDateTime::Now() - LastClientActivity;
	if (TimeSinceActivity.GetTotalSeconds() < ClientActivityTimeoutSeconds)
	{
		return 1; // At least one active client
	}

	return 0;
}

void FSpecialAgentMCPServer::RecordClientActivity()
{
	LastClientActivity = FDateTime::Now();
}
