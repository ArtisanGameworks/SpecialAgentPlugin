// Copyright Epic Games, Inc. All Rights Reserved.

#include "Services/PythonService.h"
#include "GameThreadDispatcher.h"
#include "IPythonScriptPlugin.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

FPythonService::FPythonService()
{
}

FString FPythonService::GetServiceDescription() const
{
	return TEXT("Python script execution - PRIMARY control mechanism with full UE5 API access");
}

TArray<FMCPToolInfo> FPythonService::GetAvailableTools() const
{
	TArray<FMCPToolInfo> Tools;
	
	// execute
	{
		FMCPToolInfo Tool;
		Tool.Name = TEXT("execute");
		Tool.Description = TEXT("Execute Python with full UE5 API. Use for: spawning actors (unreal.get_editor_subsystem(unreal.EditorActorSubsystem)), modifying properties, batch operations, anything not covered by other tools. Import 'unreal' module is automatic.");
		
		TSharedPtr<FJsonObject> CodeParam = MakeShared<FJsonObject>();
		CodeParam->SetStringField(TEXT("type"), TEXT("string"));
		CodeParam->SetStringField(TEXT("description"), TEXT("Python code to execute. Has access to 'unreal' module and all UE5 Python API."));
		Tool.Parameters->SetObjectField(TEXT("code"), CodeParam);
		
		TSharedPtr<FJsonObject> TimeoutParam = MakeShared<FJsonObject>();
		TimeoutParam->SetStringField(TEXT("type"), TEXT("number"));
		TimeoutParam->SetStringField(TEXT("description"), TEXT("Execution timeout in seconds (default: 30.0)"));
		Tool.Parameters->SetObjectField(TEXT("timeout"), TimeoutParam);
		
		Tool.RequiredParams.Add(TEXT("code"));
		Tools.Add(Tool);
	}
	
	// execute_file
	{
		FMCPToolInfo Tool;
		Tool.Name = TEXT("execute_file");
		Tool.Description = TEXT("Execute a Python script file from the Content/Python directory.");
		
		TSharedPtr<FJsonObject> FilePathParam = MakeShared<FJsonObject>();
		FilePathParam->SetStringField(TEXT("type"), TEXT("string"));
		FilePathParam->SetStringField(TEXT("description"), TEXT("Path to Python file relative to Content/Python/"));
		Tool.Parameters->SetObjectField(TEXT("file_path"), FilePathParam);
		
		Tool.RequiredParams.Add(TEXT("file_path"));
		Tools.Add(Tool);
	}
	
	// list_modules
	{
		FMCPToolInfo Tool;
		Tool.Name = TEXT("list_modules");
		Tool.Description = TEXT("List available Python modules and scripts.");
		Tools.Add(Tool);
	}
	
	return Tools;
}

FMCPResponse FPythonService::HandleRequest(const FMCPRequest& Request, const FString& MethodName)
{
	if (MethodName == TEXT("execute")) return HandleExecute(Request);
	if (MethodName == TEXT("execute_file")) return HandleExecuteFile(Request);
	if (MethodName == TEXT("list_modules")) return HandleListModules(Request);

	return MethodNotFound(Request.Id, TEXT("python"), MethodName);
}

FMCPResponse FPythonService::HandleExecute(const FMCPRequest& Request)
{
	// Get parameters
	if (!Request.Params.IsValid())
	{
		return InvalidParams(Request.Id, TEXT("Missing params object"));
	}

	FString Code;
	if (!Request.Params->TryGetStringField(TEXT("code"), Code))
	{
		return InvalidParams(Request.Id, TEXT("Missing required parameter 'code'"));
	}

	float Timeout = 30.0f;
	Request.Params->TryGetNumberField(TEXT("timeout"), Timeout);

	// Execute on game thread
	auto ExecuteTask = [Code]() -> TSharedPtr<FJsonObject>
	{
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

		// Check if Python plugin is available
		IPythonScriptPlugin* PythonPlugin = IPythonScriptPlugin::Get();
		if (!PythonPlugin)
		{
			Result->SetBoolField(TEXT("success"), false);
			Result->SetStringField(TEXT("stdout"), TEXT(""));
			Result->SetStringField(TEXT("stderr"), TEXT("Python Script Plugin is not available. Make sure it is enabled in Project Settings."));
			Result->SetNumberField(TEXT("execution_time"), 0.0);
			return Result;
		}

		// Execute Python code
		double StartTime = FPlatformTime::Seconds();
		
		// Generate a unique temporary file path
		FString TempDir = FPaths::ProjectIntermediateDir();
		FString TempFile = FPaths::Combine(TempDir, TEXT("mcp_python_output.txt"));
		
		// Wrap user code to capture stdout/stderr and write to temp file
		FString IndentedCode = TEXT("    ") + Code.Replace(TEXT("\n"), TEXT("\n    "));
		
		FString WrappedCode = FString::Printf(TEXT(
			"import sys\n"
			"import io\n"
			"import json\n"
			"_stdout_capture = io.StringIO()\n"
			"_stderr_capture = io.StringIO()\n"
			"_old_stdout = sys.stdout\n"
			"_old_stderr = sys.stderr\n"
			"sys.stdout = _stdout_capture\n"
			"sys.stderr = _stderr_capture\n"
			"_exec_success = True\n"
			"try:\n"
			"%s\n"
			"except Exception as _e:\n"
			"    _exec_success = False\n"
			"    import traceback\n"
			"    sys.stderr.write(traceback.format_exc())\n"
			"finally:\n"
			"    sys.stdout = _old_stdout\n"
			"    sys.stderr = _old_stderr\n"
			"    # Write result to temp file\n"
			"    with open(r'%s', 'w', encoding='utf-8') as _f:\n"
			"        import json\n"
			"        json.dump({\n"
			"            'stdout': _stdout_capture.getvalue(),\n"
			"            'stderr': _stderr_capture.getvalue(),\n"
			"            'success': _exec_success\n"
			"        }, _f)\n"
		), *IndentedCode, *TempFile);
		
		FPythonCommandEx PythonCommand;
		PythonCommand.Command = WrappedCode;
		PythonCommand.ExecutionMode = EPythonCommandExecutionMode::ExecuteFile;
		PythonCommand.FileExecutionScope = EPythonFileExecutionScope::Public;

		bool bSuccess = PythonPlugin->ExecPythonCommandEx(PythonCommand);
		
		// Read the JSON result from the temp file
		FString JsonString;
		FString StdOut;
		FString StdErr;
		
		if (FFileHelper::LoadFileToString(JsonString, *TempFile))
		{
			// Parse the JSON
			TSharedPtr<FJsonObject> JsonObject;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
			if (FJsonSerializer::Deserialize(Reader, JsonObject) && JsonObject.IsValid())
			{
				JsonObject->TryGetStringField(TEXT("stdout"), StdOut);
				JsonObject->TryGetStringField(TEXT("stderr"), StdErr);
				JsonObject->TryGetBoolField(TEXT("success"), bSuccess);
				
				UE_LOG(LogTemp, Log, TEXT("SpecialAgent: Successfully retrieved output from temp file"));
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("SpecialAgent: Failed to parse JSON from temp file: %s"), *JsonString);
				StdErr = TEXT("Failed to parse execution result");
				bSuccess = false;
			}
			
			// Clean up temp file
			IFileManager::Get().Delete(*TempFile);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("SpecialAgent: Failed to read temp file: %s"), *TempFile);
			StdErr = TEXT("Failed to read execution result");
			bSuccess = false;
		}
		
		double ExecutionTime = FPlatformTime::Seconds() - StartTime;

		// Build result
		Result->SetBoolField(TEXT("success"), bSuccess);
		Result->SetStringField(TEXT("stdout"), StdOut);
		Result->SetStringField(TEXT("stderr"), StdErr);
		Result->SetNumberField(TEXT("execution_time"), ExecutionTime);

		if (!bSuccess)
		{
			UE_LOG(LogTemp, Warning, TEXT("SpecialAgent: Python execution failed in %.3f seconds: %s"), 
				ExecutionTime, *StdErr);
		}
		else
		{
			UE_LOG(LogTemp, Log, TEXT("SpecialAgent: Python execution succeeded in %.3f seconds"), ExecutionTime);
		}

		return Result;
	};

	// Execute synchronously on game thread
	TSharedPtr<FJsonObject> Result = FGameThreadDispatcher::DispatchToGameThreadSyncWithReturn<TSharedPtr<FJsonObject>>(ExecuteTask);

	return FMCPResponse::Success(Request.Id, Result);
}

FMCPResponse FPythonService::HandleExecuteFile(const FMCPRequest& Request)
{
	// Get parameters
	if (!Request.Params.IsValid())
	{
		return InvalidParams(Request.Id, TEXT("Missing params object"));
	}

	FString FilePath;
	if (!Request.Params->TryGetStringField(TEXT("file_path"), FilePath))
	{
		return InvalidParams(Request.Id, TEXT("Missing required parameter 'file_path'"));
	}

	// Read file content
	FString FileContent;
	if (!FFileHelper::LoadFileToString(FileContent, *FilePath))
	{
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("stderr"), FString::Printf(TEXT("Failed to read file: %s"), *FilePath));
		return FMCPResponse::Success(Request.Id, Result);
	}

	// Execute the file content as Python code
	auto ExecuteTask = [FileContent, FilePath]() -> TSharedPtr<FJsonObject>
	{
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

		IPythonScriptPlugin* PythonPlugin = IPythonScriptPlugin::Get();
		if (!PythonPlugin)
		{
			Result->SetBoolField(TEXT("success"), false);
			Result->SetStringField(TEXT("stderr"), TEXT("Python Script Plugin is not available"));
			return Result;
		}

		double StartTime = FPlatformTime::Seconds();

		FPythonCommandEx PythonCommand;
		PythonCommand.Command = FileContent;
		PythonCommand.ExecutionMode = EPythonCommandExecutionMode::ExecuteFile;
		PythonCommand.FileExecutionScope = EPythonFileExecutionScope::Private;

		bool bSuccess = PythonPlugin->ExecPythonCommandEx(PythonCommand);
		
		double ExecutionTime = FPlatformTime::Seconds() - StartTime;

		Result->SetBoolField(TEXT("success"), bSuccess);
		Result->SetStringField(TEXT("stdout"), PythonCommand.CommandResult);
		Result->SetStringField(TEXT("stderr"), bSuccess ? TEXT("") : PythonCommand.CommandResult);
		Result->SetNumberField(TEXT("execution_time"), ExecutionTime);
		Result->SetStringField(TEXT("file_path"), FilePath);

		UE_LOG(LogTemp, Log, TEXT("SpecialAgent: Python file execution %s in %.3f seconds: %s"), 
			bSuccess ? TEXT("succeeded") : TEXT("failed"), ExecutionTime, *FilePath);

		return Result;
	};

	TSharedPtr<FJsonObject> Result = FGameThreadDispatcher::DispatchToGameThreadSyncWithReturn<TSharedPtr<FJsonObject>>(ExecuteTask);

	return FMCPResponse::Success(Request.Id, Result);
}

FMCPResponse FPythonService::HandleListModules(const FMCPRequest& Request)
{
	// Execute on game thread
	auto ListTask = []() -> TSharedPtr<FJsonObject>
	{
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

		IPythonScriptPlugin* PythonPlugin = IPythonScriptPlugin::Get();
		if (!PythonPlugin)
		{
			Result->SetBoolField(TEXT("success"), false);
			Result->SetStringField(TEXT("error"), TEXT("Python Script Plugin is not available"));
			return Result;
		}

		// Execute Python code to list available modules
		FPythonCommandEx PythonCommand;
		PythonCommand.Command = TEXT(
			"import sys\n"
			"import json\n"
			"modules = []\n"
			"for name in sorted(sys.modules.keys()):\n"
			"    if not name.startswith('_'):\n"
			"        modules.append(name)\n"
			"print(json.dumps(modules[:100]))  # Limit to first 100\n"
		);
		PythonCommand.ExecutionMode = EPythonCommandExecutionMode::ExecuteStatement;
		
		bool bSuccess = PythonPlugin->ExecPythonCommandEx(PythonCommand);

		if (bSuccess && !PythonCommand.CommandResult.IsEmpty())
		{
			// Parse the JSON output
			TSharedPtr<FJsonValue> JsonValue;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(PythonCommand.CommandResult);
			
			if (FJsonSerializer::Deserialize(Reader, JsonValue) && JsonValue->Type == EJson::Array)
			{
				Result->SetBoolField(TEXT("success"), true);
				Result->SetArrayField(TEXT("modules"), JsonValue->AsArray());
			}
			else
			{
				Result->SetBoolField(TEXT("success"), false);
				Result->SetStringField(TEXT("error"), TEXT("Failed to parse module list"));
			}
		}
		else
		{
			Result->SetBoolField(TEXT("success"), false);
			Result->SetStringField(TEXT("error"), TEXT("Failed to list modules"));
		}

		return Result;
	};

	TSharedPtr<FJsonObject> Result = FGameThreadDispatcher::DispatchToGameThreadSyncWithReturn<TSharedPtr<FJsonObject>>(ListTask);

	return FMCPResponse::Success(Request.Id, Result);
}

