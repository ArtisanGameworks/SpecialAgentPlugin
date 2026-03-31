# SpecialAgent UE 5.7 Compatibility Fixes

These fixes were identified and applied during a live UE 5.7.3 build session.
The plugin compiled and ran successfully after these changes.

## Summary of Changes (5 files)

### 1. `Source/SpecialAgent/Private/SpecialAgentModule.cpp`

**Problem:** `FToolMenuSection::FindOrAddSection()` was removed in UE 5.7.

**Fix:** Replace with `AddSection()` + `FindSection()` pattern:

```cpp
// BEFORE (UE 5.5/5.6):
FToolMenuSection& Section = StatusBarMenu->FindOrAddSection(TEXT("SpecialAgent"));
Section.AddEntry(...);

// AFTER (UE 5.7):
const FName SectionName = TEXT("SpecialAgent");
FToolMenuSection* Section = StatusBarMenu->FindSection(SectionName);
if (!Section)
{
    StatusBarMenu->AddSection(SectionName, LOCTEXT("SpecialAgentSection", "SpecialAgent"));
    Section = StatusBarMenu->FindSection(SectionName);
}
if (Section)
{
    Section->AddEntry(...);
}
```

**Problem 2:** `GGameIni` is deprecated in UE 5.7.

**Fix:** Use explicit config file path:

```cpp
// BEFORE:
GConfig->GetBool(TEXT("..."), TEXT("ServerEnabled"), bAutoStart, GGameIni);
GConfig->GetInt(TEXT("..."), TEXT("ServerPort"), ServerPort, GGameIni);

// AFTER:
const FString ConfigPath = FPaths::ProjectConfigDir() / TEXT("DefaultSpecialAgent.ini");
GConfig->GetBool(TEXT("..."), TEXT("ServerEnabled"), bAutoStart, ConfigPath);
GConfig->GetInt(TEXT("..."), TEXT("ServerPort"), ServerPort, ConfigPath);
```

---

### 2. `Source/SpecialAgent/Private/MCPServer.cpp`

**Problem:** `StartAllListeners()` was called BEFORE routes were bound, causing the server
to start accepting connections before any handlers were registered — resulting in 404s on all requests.

**Fix:** Move `StartAllListeners()` to AFTER all `BindRoute()` calls:

```cpp
// BEFORE (broken ordering):
HttpServerModule.StartAllListeners();   // ← too early
HttpRouter = HttpServerModule.GetHttpRouter(ServerPort);
HttpRouter->BindRoute(...);  // routes bound after server started

// AFTER (correct):
HttpRouter = HttpServerModule.GetHttpRouter(ServerPort);
HttpRouter->BindRoute(...);  // bind all routes first
HttpRouter->BindRoute(...);
// ... all routes ...
HttpServerModule.StartAllListeners();   // ← start AFTER routes are registered
```

---

### 3. `Source/SpecialAgent/Private/Services/WorldService.cpp`

**Problem:** `#include "EditorLevelLibrary.h"` — `EditorLevelLibrary` is fully deprecated
and removed in UE 5.7. Causes compile error.

**Fix:**
```cpp
// BEFORE:
#include "EditorLevelLibrary.h"

// AFTER:
// UE 5.7: EditorLevelLibrary is deprecated; use subsystems instead
#include "Subsystems/EditorActorSubsystem.h"
#include "Subsystems/UnrealEditorSubsystem.h"
```

---

### 4. `Source/SpecialAgent/Private/MCPRequestRouter.cpp`

**Fix:** Updated documentation string to reference the correct UE 5.7 API:

```cpp
// BEFORE:
"2. Use unreal.EditorLevelLibrary or unreal.EditorAssetLibrary as needed\n"

// AFTER:
"2. Use unreal.get_editor_subsystem(unreal.EditorActorSubsystem) for actor ops, unreal.EditorAssetLibrary for assets\n"
```

---

### 5. `Source/SpecialAgent/Private/Services/PythonService.cpp`

**Fix:** Updated tool description to reference the correct UE 5.7 Python API:

```cpp
// BEFORE:
Tool.Description = TEXT("Execute Python with full UE5 API. Use for: spawning actors (unreal.EditorLevelLibrary)...");

// AFTER:
Tool.Description = TEXT("Execute Python with full UE5 API. Use for: spawning actors (unreal.get_editor_subsystem(unreal.EditorActorSubsystem))...");
```

---

## How to Apply

These changes are already committed in the `Catalyst Rising/SpecialAgent_Plugin/` folder.
You can copy the 5 modified `.cpp` files directly into your cloned fork and commit them,
or run `git diff HEAD` from that folder to see the exact patch.

## Tested On
- Unreal Engine 5.7.3 (build `50162420+++UE5+Release-5.7`)
- Windows 11
- Plugin compiled successfully, MCP server running on port 8767
