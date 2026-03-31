// Copyright Epic Games, Inc. All Rights Reserved.
// WorldService Implementation - Core world/actor manipulation methods

#include "Services/WorldService.h"
#include "Services/PythonService.h"
#include "GameThreadDispatcher.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Editor.h"
#include "GameFramework/Actor.h"
#include "GameFramework/WorldSettings.h"
// UE 5.7: EditorLevelLibrary is deprecated; use subsystems instead
#include "Subsystems/EditorActorSubsystem.h"
#include "Subsystems/UnrealEditorSubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Engine/Blueprint.h"
#include "Selection.h"

FWorldService::FWorldService()
{
}

FString FWorldService::GetServiceDescription() const
{
	return TEXT("World and actor manipulation - query, spawn, modify, and organize actors");
}

// Helper function to execute Python code from request params
FMCPResponse FWorldService::ExecutePythonFromParams(const FMCPRequest& Request)
{
	if (!Request.Params.IsValid() || !Request.Params->HasField(TEXT("code")))
	{
		return InvalidParams(Request.Id, TEXT("Missing required parameter: 'code' (Python script)"));
	}

	FString Code = Request.Params->GetStringField(TEXT("code"));
	float Timeout = Request.Params->HasField(TEXT("timeout")) ? Request.Params->GetNumberField(TEXT("timeout")) : 30.0f;

	FPythonService PythonService;
	TSharedPtr<FJsonObject> PythonParams = MakeShared<FJsonObject>();
	PythonParams->SetStringField(TEXT("code"), Code);
	PythonParams->SetNumberField(TEXT("timeout"), Timeout);
	
	FMCPRequest PythonRequest;
	PythonRequest.JsonRpc = Request.JsonRpc;
	PythonRequest.Id = Request.Id;
	PythonRequest.Method = TEXT("python/execute");
	PythonRequest.Params = PythonParams;
	
	return PythonService.HandleExecute(PythonRequest);
}

// Helper functions
static TSharedPtr<FJsonObject> SerializeActor(AActor* Actor)
{
	if (!Actor) return nullptr;

	TSharedPtr<FJsonObject> ActorObj = MakeShared<FJsonObject>();
	ActorObj->SetStringField(TEXT("name"), Actor->GetActorLabel());
	ActorObj->SetStringField(TEXT("class"), Actor->GetClass()->GetName());
	
	FVector Loc = Actor->GetActorLocation();
	TArray<TSharedPtr<FJsonValue>> LocArr;
	LocArr.Add(MakeShared<FJsonValueNumber>(Loc.X));
	LocArr.Add(MakeShared<FJsonValueNumber>(Loc.Y));
	LocArr.Add(MakeShared<FJsonValueNumber>(Loc.Z));
	ActorObj->SetArrayField(TEXT("location"), LocArr);

	FRotator Rot = Actor->GetActorRotation();
	TArray<TSharedPtr<FJsonValue>> RotArr;
	RotArr.Add(MakeShared<FJsonValueNumber>(Rot.Pitch));
	RotArr.Add(MakeShared<FJsonValueNumber>(Rot.Yaw));
	RotArr.Add(MakeShared<FJsonValueNumber>(Rot.Roll));
	ActorObj->SetArrayField(TEXT("rotation"), RotArr);

	FVector Scale = Actor->GetActorScale3D();
	TArray<TSharedPtr<FJsonValue>> ScaleArr;
	ScaleArr.Add(MakeShared<FJsonValueNumber>(Scale.X));
	ScaleArr.Add(MakeShared<FJsonValueNumber>(Scale.Y));
	ScaleArr.Add(MakeShared<FJsonValueNumber>(Scale.Z));
	ActorObj->SetArrayField(TEXT("scale"), ScaleArr);

	// Tags
	TArray<TSharedPtr<FJsonValue>> TagsArr;
	for (const FName& Tag : Actor->Tags)
	{
		TagsArr.Add(MakeShared<FJsonValueString>(Tag.ToString()));
	}
	ActorObj->SetArrayField(TEXT("tags"), TagsArr);

	return ActorObj;
}

static AActor* FindActor(UWorld* World, const FString& ActorName)
{
	if (!World) return nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if ((*It)->GetActorLabel() == ActorName) return *It;
	}
	return nullptr;
}

// Request Handler
FMCPResponse FWorldService::HandleRequest(const FMCPRequest& Request, const FString& MethodName)
{
	// Query methods
	if (MethodName == TEXT("list_actors")) return HandleListActors(Request);
	if (MethodName == TEXT("get_actor")) return HandleGetActor(Request);
	if (MethodName == TEXT("find_actors_by_tag")) return HandleFindActorsByTag(Request);
	if (MethodName == TEXT("get_level_info")) return HandleGetLevelInfo(Request);

	// Spawn/Delete methods
	if (MethodName == TEXT("spawn_actor")) return HandleSpawnActor(Request);
	if (MethodName == TEXT("spawn_actors_batch")) return HandleSpawnActorsBatch(Request);
	if (MethodName == TEXT("delete_actor")) return HandleDeleteActor(Request);
	if (MethodName == TEXT("delete_actors_batch")) return HandleDeleteActorsBatch(Request);
	if (MethodName == TEXT("duplicate_actor")) return HandleDuplicateActor(Request);

	// Transform methods
	if (MethodName == TEXT("set_actor_transform")) return HandleSetActorTransform(Request);
	if (MethodName == TEXT("set_actor_location")) return HandleSetActorLocation(Request);
	if (MethodName == TEXT("set_actor_rotation")) return HandleSetActorRotation(Request);
	if (MethodName == TEXT("set_actor_scale")) return HandleSetActorScale(Request);

	// Property methods
	if (MethodName == TEXT("set_actor_property")) return HandleSetActorProperty(Request);
	if (MethodName == TEXT("set_actor_label")) return HandleSetActorLabel(Request);
	if (MethodName == TEXT("set_actor_material")) return HandleSetActorMaterial(Request);
	if (MethodName == TEXT("set_material_parameter")) return HandleSetMaterialParameter(Request);

	// Organization methods
	if (MethodName == TEXT("create_folder")) return HandleCreateFolder(Request);
	if (MethodName == TEXT("move_actor_to_folder")) return HandleMoveActorToFolder(Request);
	if (MethodName == TEXT("add_actor_tag")) return HandleAddActorTag(Request);
	if (MethodName == TEXT("remove_actor_tag")) return HandleRemoveActorTag(Request);

	// Spatial analysis methods
	if (MethodName == TEXT("measure_distance")) return HandleMeasureDistance(Request);
	if (MethodName == TEXT("find_actors_in_radius")) return HandleFindActorsInRadius(Request);
	if (MethodName == TEXT("find_actors_in_bounds")) return HandleFindActorsInBounds(Request);
	if (MethodName == TEXT("raycast")) return HandleRaycast(Request);
	if (MethodName == TEXT("get_ground_height")) return HandleGetGroundHeight(Request);

	// Pattern placement methods
	if (MethodName == TEXT("place_in_grid")) return HandlePlaceInGrid(Request);
	if (MethodName == TEXT("place_along_spline")) return HandlePlaceAlongSpline(Request);
	if (MethodName == TEXT("place_in_circle")) return HandlePlaceInCircle(Request);
	if (MethodName == TEXT("scatter_in_area")) return HandleScatterInArea(Request);

	return MethodNotFound(Request.Id, TEXT("world"), MethodName);
}

// === CORE METHOD IMPLEMENTATIONS ===

FMCPResponse FWorldService::HandleListActors(const FMCPRequest& Request)
{
	int32 MaxResults = 1000;
	FString ClassFilter;

	if (Request.Params.IsValid())
	{
		const TSharedPtr<FJsonObject>* FilterObj;
		if (Request.Params->TryGetObjectField(TEXT("filter"), FilterObj))
		{
			(*FilterObj)->TryGetNumberField(TEXT("max_results"), MaxResults);
			(*FilterObj)->TryGetStringField(TEXT("class"), ClassFilter);
		}
	}

	auto ListTask = [MaxResults, ClassFilter]() -> TSharedPtr<FJsonObject>
	{
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

		UWorld* World = GEditor->GetEditorWorldContext().World();
		if (!World)
		{
			Result->SetBoolField(TEXT("success"), false);
			Result->SetStringField(TEXT("error"), TEXT("No editor world found"));
			return Result;
		}

		TArray<TSharedPtr<FJsonValue>> ActorsJson;
		int32 Count = 0;

		for (TActorIterator<AActor> It(World); It && Count < MaxResults; ++It)
		{
			AActor* Actor = *It;
			if (!Actor) continue;

			// Apply class filter
			if (!ClassFilter.IsEmpty() && !Actor->GetClass()->GetName().Contains(ClassFilter))
			{
				continue;
			}

			TSharedPtr<FJsonObject> ActorData = SerializeActor(Actor);
			if (ActorData.IsValid())
			{
				ActorsJson.Add(MakeShared<FJsonValueObject>(ActorData));
				Count++;
			}
		}

		Result->SetBoolField(TEXT("success"), true);
		Result->SetArrayField(TEXT("actors"), ActorsJson);
		Result->SetNumberField(TEXT("count"), ActorsJson.Num());

		UE_LOG(LogTemp, Log, TEXT("SpecialAgent: Listed %d actors"), ActorsJson.Num());
		return Result;
	};

	TSharedPtr<FJsonObject> Result = FGameThreadDispatcher::DispatchToGameThreadSyncWithReturn<TSharedPtr<FJsonObject>>(ListTask);
	return FMCPResponse::Success(Request.Id, Result);
}

FMCPResponse FWorldService::HandleGetActor(const FMCPRequest& Request)
{
	if (!Request.Params.IsValid())
		return InvalidParams(Request.Id, TEXT("Missing params"));

	FString ActorName;
	if (!Request.Params->TryGetStringField(TEXT("actor_name"), ActorName))
		return InvalidParams(Request.Id, TEXT("Missing 'actor_name'"));

	auto GetTask = [ActorName]() -> TSharedPtr<FJsonObject>
	{
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

		UWorld* World = GEditor->GetEditorWorldContext().World();
		if (!World)
		{
			Result->SetBoolField(TEXT("success"), false);
			Result->SetStringField(TEXT("error"), TEXT("No editor world"));
			return Result;
		}

		AActor* Actor = FindActor(World, ActorName);
		if (!Actor)
		{
			Result->SetBoolField(TEXT("success"), false);
			Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Actor not found: %s"), *ActorName));
			return Result;
		}

		TSharedPtr<FJsonObject> ActorData = SerializeActor(Actor);
		Result->SetBoolField(TEXT("success"), true);
		Result->SetObjectField(TEXT("actor"), ActorData);

		return Result;
	};

	TSharedPtr<FJsonObject> Result = FGameThreadDispatcher::DispatchToGameThreadSyncWithReturn<TSharedPtr<FJsonObject>>(GetTask);
	return FMCPResponse::Success(Request.Id, Result);
}

FMCPResponse FWorldService::HandleSpawnActor(const FMCPRequest& Request)
{
	if (!Request.Params.IsValid())
		return InvalidParams(Request.Id, TEXT("Missing params"));

	FString ActorClass;
	if (!Request.Params->TryGetStringField(TEXT("actor_class"), ActorClass))
		return InvalidParams(Request.Id, TEXT("Missing 'actor_class'"));

	const TArray<TSharedPtr<FJsonValue>>* LocArr;
	if (!Request.Params->TryGetArrayField(TEXT("location"), LocArr) || LocArr->Num() != 3)
		return InvalidParams(Request.Id, TEXT("Missing or invalid 'location'"));

	FVector Location((*LocArr)[0]->AsNumber(), (*LocArr)[1]->AsNumber(), (*LocArr)[2]->AsNumber());

	FRotator Rotation(0, 0, 0);
	bool bHasRotation = false;
	const TArray<TSharedPtr<FJsonValue>>* RotArr;
	if (Request.Params->TryGetArrayField(TEXT("rotation"), RotArr) && RotArr->Num() == 3)
	{
		// Rotation is [Pitch, Yaw, Roll]
		Rotation = FRotator((*RotArr)[0]->AsNumber(), (*RotArr)[1]->AsNumber(), (*RotArr)[2]->AsNumber());
		bHasRotation = true;
	}

	FVector Scale(1, 1, 1);
	const TArray<TSharedPtr<FJsonValue>>* ScaleArr;
	if (Request.Params->TryGetArrayField(TEXT("scale"), ScaleArr) && ScaleArr->Num() == 3)
	{
		Scale = FVector((*ScaleArr)[0]->AsNumber(), (*ScaleArr)[1]->AsNumber(), (*ScaleArr)[2]->AsNumber());
	}

	auto SpawnTask = [ActorClass, Location, Rotation, bHasRotation, Scale]() -> TSharedPtr<FJsonObject>
	{
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

		UWorld* World = GEditor->GetEditorWorldContext().World();
		if (!World)
		{
			Result->SetBoolField(TEXT("success"), false);
			Result->SetStringField(TEXT("error"), TEXT("No editor world"));
			return Result;
		}

		AActor* NewActor = nullptr;
		FString SpawnedType;
		
		// Check if this is an asset path (contains /Game/, /Engine/, etc.)
		bool bIsAssetPath = ActorClass.Contains(TEXT("/Game/")) || 
		                    ActorClass.Contains(TEXT("/Engine/")) || 
		                    ActorClass.StartsWith(TEXT("/"));
		
		if (bIsAssetPath)
		{
			// Try to load as StaticMesh first
			UStaticMesh* StaticMesh = LoadObject<UStaticMesh>(nullptr, *ActorClass);
			if (StaticMesh)
			{
				// Spawn StaticMeshActor with identity rotation (apply rotation after)
				FActorSpawnParameters SpawnParams;
				AStaticMeshActor* MeshActor = World->SpawnActor<AStaticMeshActor>(
					AStaticMeshActor::StaticClass(), 
					Location, 
					FRotator::ZeroRotator,  // Spawn with zero rotation
					SpawnParams
				);
				
				if (MeshActor)
				{
					// Set the mesh on the component
					UStaticMeshComponent* MeshComp = MeshActor->GetStaticMeshComponent();
					if (MeshComp)
					{
						MeshComp->SetStaticMesh(StaticMesh);
					}
					NewActor = MeshActor;
					SpawnedType = TEXT("StaticMesh");
				}
			}
			else
			{
				// Try to load as Blueprint
				UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *ActorClass);
				if (Blueprint && Blueprint->GeneratedClass)
				{
					FActorSpawnParameters SpawnParams;
					NewActor = World->SpawnActor<AActor>(
						Blueprint->GeneratedClass, 
						Location, 
						FRotator::ZeroRotator,  // Spawn with zero rotation
						SpawnParams
					);
					SpawnedType = TEXT("Blueprint");
				}
			}
		}
		
		// If not an asset path or asset not found, try as a class name
		if (!NewActor)
		{
			UClass* Class = FindFirstObject<UClass>(*ActorClass, EFindFirstObjectOptions::NativeFirst | EFindFirstObjectOptions::EnsureIfAmbiguous);
			if (Class)
			{
				FActorSpawnParameters SpawnParams;
				NewActor = World->SpawnActor<AActor>(
					Class, 
					Location, 
					FRotator::ZeroRotator,  // Spawn with zero rotation
					SpawnParams
				);
				SpawnedType = TEXT("Class");
			}
		}
		
		if (!NewActor)
		{
			Result->SetBoolField(TEXT("success"), false);
			Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to spawn actor from: %s. For meshes, use full path like /Game/Meshes/MyMesh.MyMesh"), *ActorClass));
			return Result;
		}

		// Apply rotation AFTER spawning to avoid gimbal lock issues in SpawnActor
		if (bHasRotation)
		{
			NewActor->SetActorRotation(Rotation);
		}
		
		// Apply scale
		NewActor->SetActorScale3D(Scale);

		TSharedPtr<FJsonObject> ActorData = SerializeActor(NewActor);
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("spawned_type"), SpawnedType);
		Result->SetObjectField(TEXT("actor"), ActorData);

		UE_LOG(LogTemp, Log, TEXT("SpecialAgent: Spawned %s actor: %s from %s"), 
			*SpawnedType, *NewActor->GetActorLabel(), *ActorClass);
		return Result;
	};

	TSharedPtr<FJsonObject> Result = FGameThreadDispatcher::DispatchToGameThreadSyncWithReturn<TSharedPtr<FJsonObject>>(SpawnTask);
	return FMCPResponse::Success(Request.Id, Result);
}

FMCPResponse FWorldService::HandleDeleteActor(const FMCPRequest& Request)
{
	if (!Request.Params.IsValid())
		return InvalidParams(Request.Id, TEXT("Missing params"));

	FString ActorName;
	if (!Request.Params->TryGetStringField(TEXT("actor_name"), ActorName))
		return InvalidParams(Request.Id, TEXT("Missing 'actor_name'"));

	auto DeleteTask = [ActorName]() -> TSharedPtr<FJsonObject>
	{
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

		UWorld* World = GEditor->GetEditorWorldContext().World();
		if (!World)
		{
			Result->SetBoolField(TEXT("success"), false);
			Result->SetStringField(TEXT("error"), TEXT("No editor world"));
			return Result;
		}

		AActor* Actor = FindActor(World, ActorName);
		if (!Actor)
		{
			Result->SetBoolField(TEXT("success"), false);
			Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Actor not found: %s"), *ActorName));
			return Result;
		}

		World->DestroyActor(Actor);

		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("actor_name"), ActorName);

		UE_LOG(LogTemp, Log, TEXT("SpecialAgent: Deleted actor: %s"), *ActorName);
		return Result;
	};

	TSharedPtr<FJsonObject> Result = FGameThreadDispatcher::DispatchToGameThreadSyncWithReturn<TSharedPtr<FJsonObject>>(DeleteTask);
	return FMCPResponse::Success(Request.Id, Result);
}

FMCPResponse FWorldService::HandleSetActorLocation(const FMCPRequest& Request)
{
	if (!Request.Params.IsValid())
		return InvalidParams(Request.Id, TEXT("Missing params"));

	FString ActorName;
	if (!Request.Params->TryGetStringField(TEXT("actor_name"), ActorName))
		return InvalidParams(Request.Id, TEXT("Missing 'actor_name'"));

	const TArray<TSharedPtr<FJsonValue>>* LocArr;
	if (!Request.Params->TryGetArrayField(TEXT("location"), LocArr) || LocArr->Num() != 3)
		return InvalidParams(Request.Id, TEXT("Missing or invalid 'location'"));

	FVector Location((*LocArr)[0]->AsNumber(), (*LocArr)[1]->AsNumber(), (*LocArr)[2]->AsNumber());

	auto SetLocTask = [ActorName, Location]() -> TSharedPtr<FJsonObject>
	{
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

		UWorld* World = GEditor->GetEditorWorldContext().World();
		if (!World)
		{
			Result->SetBoolField(TEXT("success"), false);
			Result->SetStringField(TEXT("error"), TEXT("No editor world"));
			return Result;
		}

		AActor* Actor = FindActor(World, ActorName);
		if (!Actor)
		{
			Result->SetBoolField(TEXT("success"), false);
			Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Actor not found: %s"), *ActorName));
			return Result;
		}

		Actor->SetActorLocation(Location);

		Result->SetBoolField(TEXT("success"), true);
		Result->SetObjectField(TEXT("actor"), SerializeActor(Actor));

		return Result;
	};

	TSharedPtr<FJsonObject> Result = FGameThreadDispatcher::DispatchToGameThreadSyncWithReturn<TSharedPtr<FJsonObject>>(SetLocTask);
	return FMCPResponse::Success(Request.Id, Result);
}

// Stub implementations for remaining methods
// These return "not_implemented" but are properly wired up

FMCPResponse FWorldService::HandleFindActorsByTag(const FMCPRequest& Request)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("status"), TEXT("not_implemented"));
	return FMCPResponse::Success(Request.Id, Result);
}

FMCPResponse FWorldService::HandleGetLevelInfo(const FMCPRequest& Request)
{
	auto GetInfoTask = []() -> TSharedPtr<FJsonObject>
	{
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

		UWorld* World = GEditor->GetEditorWorldContext().World();
		if (!World)
		{
			Result->SetBoolField(TEXT("success"), false);
			Result->SetStringField(TEXT("error"), TEXT("No editor world"));
			return Result;
		}

		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("level_name"), World->GetMapName());
		Result->SetStringField(TEXT("level_path"), World->GetPathName());
		
		// Count actors
		int32 ActorCount = 0;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			ActorCount++;
		}
		Result->SetNumberField(TEXT("actor_count"), ActorCount);

		// Get level bounds
		FBox LevelBounds(ForceInit);
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (Actor && !Actor->IsA<AWorldSettings>())
			{
				LevelBounds += Actor->GetComponentsBoundingBox(true);
			}
		}

		if (LevelBounds.IsValid)
		{
			TSharedPtr<FJsonObject> BoundsObj = MakeShared<FJsonObject>();
			
			TArray<TSharedPtr<FJsonValue>> MinArr, MaxArr, CenterArr, SizeArr;
			FVector Min = LevelBounds.Min;
			FVector Max = LevelBounds.Max;
			FVector Center = LevelBounds.GetCenter();
			FVector Size = LevelBounds.GetSize();

			MinArr.Add(MakeShared<FJsonValueNumber>(Min.X));
			MinArr.Add(MakeShared<FJsonValueNumber>(Min.Y));
			MinArr.Add(MakeShared<FJsonValueNumber>(Min.Z));
			MaxArr.Add(MakeShared<FJsonValueNumber>(Max.X));
			MaxArr.Add(MakeShared<FJsonValueNumber>(Max.Y));
			MaxArr.Add(MakeShared<FJsonValueNumber>(Max.Z));
			CenterArr.Add(MakeShared<FJsonValueNumber>(Center.X));
			CenterArr.Add(MakeShared<FJsonValueNumber>(Center.Y));
			CenterArr.Add(MakeShared<FJsonValueNumber>(Center.Z));
			SizeArr.Add(MakeShared<FJsonValueNumber>(Size.X));
			SizeArr.Add(MakeShared<FJsonValueNumber>(Size.Y));
			SizeArr.Add(MakeShared<FJsonValueNumber>(Size.Z));

			BoundsObj->SetArrayField(TEXT("min"), MinArr);
			BoundsObj->SetArrayField(TEXT("max"), MaxArr);
			BoundsObj->SetArrayField(TEXT("center"), CenterArr);
			BoundsObj->SetArrayField(TEXT("size"), SizeArr);

			Result->SetObjectField(TEXT("bounds"), BoundsObj);
		}

		return Result;
	};

	TSharedPtr<FJsonObject> Result = FGameThreadDispatcher::DispatchToGameThreadSyncWithReturn<TSharedPtr<FJsonObject>>(GetInfoTask);
	return FMCPResponse::Success(Request.Id, Result);
}

FMCPResponse FWorldService::HandleSpawnActorsBatch(const FMCPRequest& Request)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("status"), TEXT("not_implemented"));
	return FMCPResponse::Success(Request.Id, Result);
}

FMCPResponse FWorldService::HandleDeleteActorsBatch(const FMCPRequest& Request)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("status"), TEXT("not_implemented"));
	return FMCPResponse::Success(Request.Id, Result);
}

FMCPResponse FWorldService::HandleDuplicateActor(const FMCPRequest& Request)
{
	if (!Request.Params.IsValid())
		return InvalidParams(Request.Id, TEXT("Missing params"));

	FString ActorName;
	if (!Request.Params->TryGetStringField(TEXT("actor_name"), ActorName))
		return InvalidParams(Request.Id, TEXT("Missing 'actor_name'"));

	// Optional new location
	FVector NewLocation(0, 0, 0);
	bool bHasNewLocation = false;
	const TArray<TSharedPtr<FJsonValue>>* LocArr;
	if (Request.Params->TryGetArrayField(TEXT("new_location"), LocArr) && LocArr->Num() == 3)
	{
		NewLocation = FVector((*LocArr)[0]->AsNumber(), (*LocArr)[1]->AsNumber(), (*LocArr)[2]->AsNumber());
		bHasNewLocation = true;
	}

	auto DupeTask = [ActorName, NewLocation, bHasNewLocation]() -> TSharedPtr<FJsonObject>
	{
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

		UWorld* World = GEditor->GetEditorWorldContext().World();
		if (!World)
		{
			Result->SetBoolField(TEXT("success"), false);
			Result->SetStringField(TEXT("error"), TEXT("No editor world"));
			return Result;
		}

		AActor* SourceActor = FindActor(World, ActorName);
		if (!SourceActor)
		{
			Result->SetBoolField(TEXT("success"), false);
			Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Actor not found: %s"), *ActorName));
			return Result;
		}

		// Select the source actor and use editor copy/paste
		GEditor->SelectNone(true, true, false);
		GEditor->SelectActor(SourceActor, true, true, true);
		
		// Duplicate selected actors
		GEditor->edactDuplicateSelected(World->GetCurrentLevel(), false);
		
		// Get the newly selected actor (duplicate is auto-selected)
		AActor* NewActor = nullptr;
		USelection* Selection = GEditor->GetSelectedActors();
		if (Selection && Selection->Num() > 0)
		{
			NewActor = Cast<AActor>(Selection->GetSelectedObject(0));
		}

		if (!NewActor || NewActor == SourceActor)
		{
			Result->SetBoolField(TEXT("success"), false);
			Result->SetStringField(TEXT("error"), TEXT("Failed to duplicate actor"));
			return Result;
		}

		if (bHasNewLocation)
		{
			NewActor->SetActorLocation(NewLocation);
		}

		Result->SetBoolField(TEXT("success"), true);
		Result->SetObjectField(TEXT("actor"), SerializeActor(NewActor));

		UE_LOG(LogTemp, Log, TEXT("SpecialAgent: Duplicated actor %s -> %s"), *ActorName, *NewActor->GetActorLabel());

		return Result;
	};

	TSharedPtr<FJsonObject> Result = FGameThreadDispatcher::DispatchToGameThreadSyncWithReturn<TSharedPtr<FJsonObject>>(DupeTask);
	return FMCPResponse::Success(Request.Id, Result);
}

FMCPResponse FWorldService::HandleSetActorTransform(const FMCPRequest& Request)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("status"), TEXT("not_implemented"));
	return FMCPResponse::Success(Request.Id, Result);
}

FMCPResponse FWorldService::HandleSetActorRotation(const FMCPRequest& Request)
{
	if (!Request.Params.IsValid())
		return InvalidParams(Request.Id, TEXT("Missing params"));

	FString ActorName;
	if (!Request.Params->TryGetStringField(TEXT("actor_name"), ActorName))
		return InvalidParams(Request.Id, TEXT("Missing 'actor_name'"));

	const TArray<TSharedPtr<FJsonValue>>* RotArr;
	if (!Request.Params->TryGetArrayField(TEXT("rotation"), RotArr) || RotArr->Num() != 3)
		return InvalidParams(Request.Id, TEXT("Missing or invalid 'rotation' [Pitch, Yaw, Roll]"));

	FRotator Rotation((*RotArr)[0]->AsNumber(), (*RotArr)[1]->AsNumber(), (*RotArr)[2]->AsNumber());

	auto SetRotTask = [ActorName, Rotation]() -> TSharedPtr<FJsonObject>
	{
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

		UWorld* World = GEditor->GetEditorWorldContext().World();
		if (!World)
		{
			Result->SetBoolField(TEXT("success"), false);
			Result->SetStringField(TEXT("error"), TEXT("No editor world"));
			return Result;
		}

		AActor* Actor = FindActor(World, ActorName);
		if (!Actor)
		{
			Result->SetBoolField(TEXT("success"), false);
			Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Actor not found: %s"), *ActorName));
			return Result;
		}

		Actor->SetActorRotation(Rotation);

		Result->SetBoolField(TEXT("success"), true);
		Result->SetObjectField(TEXT("actor"), SerializeActor(Actor));

		UE_LOG(LogTemp, Log, TEXT("SpecialAgent: Set rotation for %s to (P=%.1f, Y=%.1f, R=%.1f)"), 
			*ActorName, Rotation.Pitch, Rotation.Yaw, Rotation.Roll);

		return Result;
	};

	TSharedPtr<FJsonObject> Result = FGameThreadDispatcher::DispatchToGameThreadSyncWithReturn<TSharedPtr<FJsonObject>>(SetRotTask);
	return FMCPResponse::Success(Request.Id, Result);
}

FMCPResponse FWorldService::HandleSetActorScale(const FMCPRequest& Request)
{
	if (!Request.Params.IsValid())
		return InvalidParams(Request.Id, TEXT("Missing params"));

	FString ActorName;
	if (!Request.Params->TryGetStringField(TEXT("actor_name"), ActorName))
		return InvalidParams(Request.Id, TEXT("Missing 'actor_name'"));

	const TArray<TSharedPtr<FJsonValue>>* ScaleArr;
	if (!Request.Params->TryGetArrayField(TEXT("scale"), ScaleArr) || ScaleArr->Num() != 3)
		return InvalidParams(Request.Id, TEXT("Missing or invalid 'scale' [X, Y, Z]"));

	FVector Scale((*ScaleArr)[0]->AsNumber(), (*ScaleArr)[1]->AsNumber(), (*ScaleArr)[2]->AsNumber());

	auto SetScaleTask = [ActorName, Scale]() -> TSharedPtr<FJsonObject>
	{
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

		UWorld* World = GEditor->GetEditorWorldContext().World();
		if (!World)
		{
			Result->SetBoolField(TEXT("success"), false);
			Result->SetStringField(TEXT("error"), TEXT("No editor world"));
			return Result;
		}

		AActor* Actor = FindActor(World, ActorName);
		if (!Actor)
		{
			Result->SetBoolField(TEXT("success"), false);
			Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Actor not found: %s"), *ActorName));
			return Result;
		}

		Actor->SetActorScale3D(Scale);

		Result->SetBoolField(TEXT("success"), true);
		Result->SetObjectField(TEXT("actor"), SerializeActor(Actor));

		UE_LOG(LogTemp, Log, TEXT("SpecialAgent: Set scale for %s to (%.2f, %.2f, %.2f)"), 
			*ActorName, Scale.X, Scale.Y, Scale.Z);

		return Result;
	};

	TSharedPtr<FJsonObject> Result = FGameThreadDispatcher::DispatchToGameThreadSyncWithReturn<TSharedPtr<FJsonObject>>(SetScaleTask);
	return FMCPResponse::Success(Request.Id, Result);
}

FMCPResponse FWorldService::HandleSetActorProperty(const FMCPRequest& Request)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("status"), TEXT("not_implemented"));
	return FMCPResponse::Success(Request.Id, Result);
}

FMCPResponse FWorldService::HandleSetActorLabel(const FMCPRequest& Request)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("status"), TEXT("not_implemented"));
	return FMCPResponse::Success(Request.Id, Result);
}

FMCPResponse FWorldService::HandleSetActorMaterial(const FMCPRequest& Request)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("status"), TEXT("not_implemented"));
	return FMCPResponse::Success(Request.Id, Result);
}

FMCPResponse FWorldService::HandleSetMaterialParameter(const FMCPRequest& Request)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("status"), TEXT("not_implemented"));
	return FMCPResponse::Success(Request.Id, Result);
}

FMCPResponse FWorldService::HandleCreateFolder(const FMCPRequest& Request)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("status"), TEXT("not_implemented"));
	return FMCPResponse::Success(Request.Id, Result);
}

FMCPResponse FWorldService::HandleMoveActorToFolder(const FMCPRequest& Request)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("status"), TEXT("not_implemented"));
	return FMCPResponse::Success(Request.Id, Result);
}

FMCPResponse FWorldService::HandleAddActorTag(const FMCPRequest& Request)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("status"), TEXT("not_implemented"));
	return FMCPResponse::Success(Request.Id, Result);
}

FMCPResponse FWorldService::HandleRemoveActorTag(const FMCPRequest& Request)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("status"), TEXT("not_implemented"));
	return FMCPResponse::Success(Request.Id, Result);
}

FMCPResponse FWorldService::HandleMeasureDistance(const FMCPRequest& Request)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("status"), TEXT("not_implemented"));
	return FMCPResponse::Success(Request.Id, Result);
}

FMCPResponse FWorldService::HandleFindActorsInRadius(const FMCPRequest& Request)
{
	if (!Request.Params.IsValid())
		return InvalidParams(Request.Id, TEXT("Missing params"));

	const TArray<TSharedPtr<FJsonValue>>* CenterArr;
	if (!Request.Params->TryGetArrayField(TEXT("center"), CenterArr) || CenterArr->Num() != 3)
		return InvalidParams(Request.Id, TEXT("Missing or invalid 'center' [X, Y, Z]"));

	double Radius = 0;
	if (!Request.Params->TryGetNumberField(TEXT("radius"), Radius))
		return InvalidParams(Request.Id, TEXT("Missing 'radius'"));

	FVector Center((*CenterArr)[0]->AsNumber(), (*CenterArr)[1]->AsNumber(), (*CenterArr)[2]->AsNumber());

	auto FindTask = [Center, Radius]() -> TSharedPtr<FJsonObject>
	{
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

		UWorld* World = GEditor->GetEditorWorldContext().World();
		if (!World)
		{
			Result->SetBoolField(TEXT("success"), false);
			Result->SetStringField(TEXT("error"), TEXT("No editor world"));
			return Result;
		}

		TArray<TSharedPtr<FJsonValue>> ActorsJson;
		float RadiusSq = Radius * Radius;

		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (!Actor) continue;

			float DistSq = FVector::DistSquared(Actor->GetActorLocation(), Center);
			if (DistSq <= RadiusSq)
			{
				TSharedPtr<FJsonObject> ActorObj = MakeShared<FJsonObject>();
				ActorObj->SetStringField(TEXT("name"), Actor->GetActorLabel());
				ActorObj->SetStringField(TEXT("class"), Actor->GetClass()->GetName());
				
				FVector Loc = Actor->GetActorLocation();
				TArray<TSharedPtr<FJsonValue>> LocArr;
				LocArr.Add(MakeShared<FJsonValueNumber>(Loc.X));
				LocArr.Add(MakeShared<FJsonValueNumber>(Loc.Y));
				LocArr.Add(MakeShared<FJsonValueNumber>(Loc.Z));
				ActorObj->SetArrayField(TEXT("location"), LocArr);
				ActorObj->SetNumberField(TEXT("distance"), FMath::Sqrt(DistSq));

				ActorsJson.Add(MakeShared<FJsonValueObject>(ActorObj));
			}
		}

		Result->SetBoolField(TEXT("success"), true);
		Result->SetNumberField(TEXT("count"), ActorsJson.Num());
		Result->SetArrayField(TEXT("actors"), ActorsJson);

		return Result;
	};

	TSharedPtr<FJsonObject> Result = FGameThreadDispatcher::DispatchToGameThreadSyncWithReturn<TSharedPtr<FJsonObject>>(FindTask);
	return FMCPResponse::Success(Request.Id, Result);
}

FMCPResponse FWorldService::HandleFindActorsInBounds(const FMCPRequest& Request)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("status"), TEXT("not_implemented"));
	return FMCPResponse::Success(Request.Id, Result);
}

FMCPResponse FWorldService::HandleRaycast(const FMCPRequest& Request)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("status"), TEXT("not_implemented"));
	return FMCPResponse::Success(Request.Id, Result);
}

FMCPResponse FWorldService::HandleGetGroundHeight(const FMCPRequest& Request)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("status"), TEXT("not_implemented"));
	return FMCPResponse::Success(Request.Id, Result);
}

FMCPResponse FWorldService::HandlePlaceInGrid(const FMCPRequest& Request)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("status"), TEXT("not_implemented"));
	return FMCPResponse::Success(Request.Id, Result);
}

FMCPResponse FWorldService::HandlePlaceAlongSpline(const FMCPRequest& Request)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("status"), TEXT("not_implemented"));
	return FMCPResponse::Success(Request.Id, Result);
}

FMCPResponse FWorldService::HandlePlaceInCircle(const FMCPRequest& Request)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("status"), TEXT("not_implemented"));
	return FMCPResponse::Success(Request.Id, Result);
}

FMCPResponse FWorldService::HandleScatterInArea(const FMCPRequest& Request)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("status"), TEXT("not_implemented"));
	return FMCPResponse::Success(Request.Id, Result);
}



TArray<FMCPToolInfo> FWorldService::GetAvailableTools() const
{
	TArray<FMCPToolInfo> Tools;
	
	// list_actors
	{
		FMCPToolInfo Tool;
		Tool.Name = TEXT("list_actors");
		Tool.Description = TEXT("List all actors in the current level. Can filter by class type.");
		
		TSharedPtr<FJsonObject> ClassParam = MakeShared<FJsonObject>();
		ClassParam->SetStringField(TEXT("type"), TEXT("string"));
		ClassParam->SetStringField(TEXT("description"), TEXT("Optional class name to filter by"));
		Tool.Parameters->SetObjectField(TEXT("class_filter"), ClassParam);
		
		TSharedPtr<FJsonObject> MaxParam = MakeShared<FJsonObject>();
		MaxParam->SetStringField(TEXT("type"), TEXT("number"));
		MaxParam->SetStringField(TEXT("description"), TEXT("Maximum number of actors to return (default: 1000)"));
		Tool.Parameters->SetObjectField(TEXT("max_results"), MaxParam);
		
		Tools.Add(Tool);
	}
	
	// get_actor
	{
		FMCPToolInfo Tool;
		Tool.Name = TEXT("get_actor");
		Tool.Description = TEXT("Get detailed information about a specific actor by name.");
		
		TSharedPtr<FJsonObject> NameParam = MakeShared<FJsonObject>();
		NameParam->SetStringField(TEXT("type"), TEXT("string"));
		NameParam->SetStringField(TEXT("description"), TEXT("The actor label/name to look up"));
		Tool.Parameters->SetObjectField(TEXT("actor_name"), NameParam);
		Tool.RequiredParams.Add(TEXT("actor_name"));
		
		Tools.Add(Tool);
	}
	
	// find_actors_by_tag
	{
		FMCPToolInfo Tool;
		Tool.Name = TEXT("find_actors_by_tag");
		Tool.Description = TEXT("Find all actors with a specific tag.");
		
		TSharedPtr<FJsonObject> TagParam = MakeShared<FJsonObject>();
		TagParam->SetStringField(TEXT("type"), TEXT("string"));
		TagParam->SetStringField(TEXT("description"), TEXT("The tag to search for"));
		Tool.Parameters->SetObjectField(TEXT("tag"), TagParam);
		Tool.RequiredParams.Add(TEXT("tag"));
		
		Tools.Add(Tool);
	}
	
	// spawn_actor
	{
		FMCPToolInfo Tool;
		Tool.Name = TEXT("spawn_actor");
		Tool.Description = TEXT("Spawn an actor at a location. IMPORTANT: Place ONE at a time, then screenshot to verify. Location is where the mesh ORIGIN/PIVOT goes (may not be mesh center). Use assets/get_bounds first to understand pivot offset. Use trace_from_screen normal to calculate proper rotation for surface alignment.");
		
		TSharedPtr<FJsonObject> ClassParam = MakeShared<FJsonObject>();
		ClassParam->SetStringField(TEXT("type"), TEXT("string"));
		ClassParam->SetStringField(TEXT("description"), TEXT("Asset path (e.g., /Game/Meshes/Rock.Rock for StaticMesh, /Game/BP/MyActor.MyActor for Blueprint) or class name"));
		Tool.Parameters->SetObjectField(TEXT("actor_class"), ClassParam);
		Tool.RequiredParams.Add(TEXT("actor_class"));
		
		TSharedPtr<FJsonObject> LocParam = MakeShared<FJsonObject>();
		LocParam->SetStringField(TEXT("type"), TEXT("array"));
		LocParam->SetStringField(TEXT("description"), TEXT("Spawn location as [X, Y, Z]"));
		Tool.Parameters->SetObjectField(TEXT("location"), LocParam);
		Tool.RequiredParams.Add(TEXT("location"));
		
		TSharedPtr<FJsonObject> RotParam = MakeShared<FJsonObject>();
		RotParam->SetStringField(TEXT("type"), TEXT("array"));
		RotParam->SetStringField(TEXT("description"), TEXT("Optional rotation as [Pitch, Yaw, Roll] in degrees"));
		Tool.Parameters->SetObjectField(TEXT("rotation"), RotParam);
		
		TSharedPtr<FJsonObject> ScaleParam = MakeShared<FJsonObject>();
		ScaleParam->SetStringField(TEXT("type"), TEXT("array"));
		ScaleParam->SetStringField(TEXT("description"), TEXT("Optional scale as [X, Y, Z]"));
		Tool.Parameters->SetObjectField(TEXT("scale"), ScaleParam);
		
		Tools.Add(Tool);
	}
	
	// delete_actor
	{
		FMCPToolInfo Tool;
		Tool.Name = TEXT("delete_actor");
		Tool.Description = TEXT("Delete an actor from the level by name.");
		
		TSharedPtr<FJsonObject> NameParam = MakeShared<FJsonObject>();
		NameParam->SetStringField(TEXT("type"), TEXT("string"));
		NameParam->SetStringField(TEXT("description"), TEXT("The actor name to delete"));
		Tool.Parameters->SetObjectField(TEXT("actor_name"), NameParam);
		Tool.RequiredParams.Add(TEXT("actor_name"));
		
		Tools.Add(Tool);
	}
	
	// set_actor_location
	{
		FMCPToolInfo Tool;
		Tool.Name = TEXT("set_actor_location");
		Tool.Description = TEXT("Move an actor to a new location.");
		
		TSharedPtr<FJsonObject> NameParam = MakeShared<FJsonObject>();
		NameParam->SetStringField(TEXT("type"), TEXT("string"));
		NameParam->SetStringField(TEXT("description"), TEXT("The actor name to move"));
		Tool.Parameters->SetObjectField(TEXT("actor_name"), NameParam);
		Tool.RequiredParams.Add(TEXT("actor_name"));
		
		TSharedPtr<FJsonObject> LocParam = MakeShared<FJsonObject>();
		LocParam->SetStringField(TEXT("type"), TEXT("array"));
		LocParam->SetStringField(TEXT("description"), TEXT("New location as [X, Y, Z]"));
		Tool.Parameters->SetObjectField(TEXT("location"), LocParam);
		Tool.RequiredParams.Add(TEXT("location"));
		
		Tools.Add(Tool);
	}
	
	// set_actor_rotation
	{
		FMCPToolInfo Tool;
		Tool.Name = TEXT("set_actor_rotation");
		Tool.Description = TEXT("Set an actor's rotation.");
		
		TSharedPtr<FJsonObject> NameParam = MakeShared<FJsonObject>();
		NameParam->SetStringField(TEXT("type"), TEXT("string"));
		NameParam->SetStringField(TEXT("description"), TEXT("The actor name to rotate"));
		Tool.Parameters->SetObjectField(TEXT("actor_name"), NameParam);
		Tool.RequiredParams.Add(TEXT("actor_name"));
		
		TSharedPtr<FJsonObject> RotParam = MakeShared<FJsonObject>();
		RotParam->SetStringField(TEXT("type"), TEXT("array"));
		RotParam->SetStringField(TEXT("description"), TEXT("New rotation as [Pitch, Yaw, Roll]"));
		Tool.Parameters->SetObjectField(TEXT("rotation"), RotParam);
		Tool.RequiredParams.Add(TEXT("rotation"));
		
		Tools.Add(Tool);
	}
	
	// set_actor_scale
	{
		FMCPToolInfo Tool;
		Tool.Name = TEXT("set_actor_scale");
		Tool.Description = TEXT("Set an actor's scale.");
		
		TSharedPtr<FJsonObject> NameParam = MakeShared<FJsonObject>();
		NameParam->SetStringField(TEXT("type"), TEXT("string"));
		NameParam->SetStringField(TEXT("description"), TEXT("The actor name to scale"));
		Tool.Parameters->SetObjectField(TEXT("actor_name"), NameParam);
		Tool.RequiredParams.Add(TEXT("actor_name"));
		
		TSharedPtr<FJsonObject> ScaleParam = MakeShared<FJsonObject>();
		ScaleParam->SetStringField(TEXT("type"), TEXT("array"));
		ScaleParam->SetStringField(TEXT("description"), TEXT("New scale as [X, Y, Z]"));
		Tool.Parameters->SetObjectField(TEXT("scale"), ScaleParam);
		Tool.RequiredParams.Add(TEXT("scale"));
		
		Tools.Add(Tool);
	}
	
	// duplicate_actor
	{
		FMCPToolInfo Tool;
		Tool.Name = TEXT("duplicate_actor");
		Tool.Description = TEXT("Duplicate an existing actor.");
		
		TSharedPtr<FJsonObject> NameParam = MakeShared<FJsonObject>();
		NameParam->SetStringField(TEXT("type"), TEXT("string"));
		NameParam->SetStringField(TEXT("description"), TEXT("The actor name to duplicate"));
		Tool.Parameters->SetObjectField(TEXT("actor_name"), NameParam);
		Tool.RequiredParams.Add(TEXT("actor_name"));
		
		TSharedPtr<FJsonObject> LocParam = MakeShared<FJsonObject>();
		LocParam->SetStringField(TEXT("type"), TEXT("array"));
		LocParam->SetStringField(TEXT("description"), TEXT("Optional new location for the duplicate"));
		Tool.Parameters->SetObjectField(TEXT("new_location"), LocParam);
		
		Tools.Add(Tool);
	}
	
	// find_actors_in_radius
	{
		FMCPToolInfo Tool;
		Tool.Name = TEXT("find_actors_in_radius");
		Tool.Description = TEXT("Find all actors within a radius of a point.");
		
		TSharedPtr<FJsonObject> CenterParam = MakeShared<FJsonObject>();
		CenterParam->SetStringField(TEXT("type"), TEXT("array"));
		CenterParam->SetStringField(TEXT("description"), TEXT("Center point as [X, Y, Z]"));
		Tool.Parameters->SetObjectField(TEXT("center"), CenterParam);
		Tool.RequiredParams.Add(TEXT("center"));
		
		TSharedPtr<FJsonObject> RadiusParam = MakeShared<FJsonObject>();
		RadiusParam->SetStringField(TEXT("type"), TEXT("number"));
		RadiusParam->SetStringField(TEXT("description"), TEXT("Search radius in units"));
		Tool.Parameters->SetObjectField(TEXT("radius"), RadiusParam);
		Tool.RequiredParams.Add(TEXT("radius"));
		
		Tools.Add(Tool);
	}
	
	// get_level_info
	{
		FMCPToolInfo Tool;
		Tool.Name = TEXT("get_level_info");
		Tool.Description = TEXT("Get information about the current level.");
		Tools.Add(Tool);
	}
	
	return Tools;
}
