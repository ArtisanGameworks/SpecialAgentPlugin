# SpecialAgent

**Connect AI to Unreal Engine 5**

Full Python API access • 71+ level design tools • Visual feedback loop

---

## What is SpecialAgent?

SpecialAgent bridges AI assistants and Unreal Engine 5 through the **Model Context Protocol (MCP)**. Connect Claude, GPT, or any MCP-compatible LLM directly to your editor and control it through natural language.

At its core, SpecialAgent provides **unrestricted Python execution** with full access to UE5's Python API—meaning your AI assistant can do anything the editor can do. On top of that foundation, **71+ purpose-built tools** handle common level design tasks without writing a single line of code.

Native HTTP/SSE transport. No external bridges or dependencies.

---

## Features

### Two Layers of Power

#### Full Python Access

Execute arbitrary Python with complete `unreal` module access. Your AI assistant can:

- Import and process assets
- Create and modify Blueprints  
- Generate materials and textures
- Automate project configuration
- Build custom editor utilities
- Run validation and QA checks
- Anything the UE5 Python API supports

This is the unlimited foundation. If you can script it, AI can do it.

#### Level Design Toolkit

71+ specialized tools for world-building workflows:

| Category | Capabilities |
|----------|-------------|
| **Actors** | Spawn, transform, duplicate, delete, batch operations |
| **Patterns** | Grid, circular, spline, and scatter placement |
| **Landscape** | Sculpt height, flatten, smooth, paint material layers |
| **Foliage** | Paint vegetation with density control |
| **Lighting** | Spawn and configure lights, build lightmaps |
| **Streaming** | Manage sub-levels for open worlds |
| **Navigation** | Rebuild NavMesh, test pathfinding |
| **Performance** | Analyze statistics, detect overlaps |
| **Organization** | Folders, tags, labels, selection management |

### Visual Feedback Loop

Capture viewport screenshots and return them to vision-enabled LLMs. Your AI assistant can see what it built, evaluate the results, and refine its approach.

```
Describe intent → Execute → Screenshot → AI analyzes → Iterate
```

---

## Installation

### Requirements

- Unreal Engine 5.6 or later (includes UE 5.7 compatibility fixes)
- Windows, Mac, or Linux
- MCP-compatible client (Claude Code, Claude Desktop, Cursor, etc.)

### Setup

1. **Clone or download** this repository into your project's `Plugins` folder:
   ```
   YourProject/
   └── Plugins/
       └── SpecialAgent/
   ```

2. **Enable required plugins** in your `.uproject` file (or via Edit → Plugins):
   - `PythonScriptPlugin` — required for Python execution
   - `EditorScriptingUtilities` — required for editor scripting API

   To enable via `.uproject`, add these entries to the `"Plugins"` array:
   ```json
   {
       "Name": "PythonScriptPlugin",
       "Enabled": true
   },
   {
       "Name": "EditorScriptingUtilities",
       "Enabled": true
   }
   ```

3. **Regenerate project files** (right-click `.uproject` → Generate Visual Studio/Xcode project files)

4. **Build and launch** your project

5. **Enable SpecialAgent** in Edit → Plugins → Search "SpecialAgent"

6. **Restart** the editor

### UE 5.7 Notes

If you are using **Unreal Engine 5.7+**, be aware of these API changes that affect Python scripts:

- **`EditorLevelLibrary` is removed.** Use subsystems instead:
  ```python
  # UE 5.6 (deprecated):
  unreal.EditorLevelLibrary.spawn_actor_from_class(...)

  # UE 5.7+:
  subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
  subsystem.spawn_actor_from_class(...)
  ```
- All 5.7-specific fixes are already applied in this branch. See [UE57_FIXES.md](UE57_FIXES.md) for details.

---

## Quick Start

### 1. Verify the Server

Once the editor launches, check the Output Log for:

```
LogSpecialAgent: MCP Server started on port 8767
```

Or test with curl:

```bash
curl http://localhost:8767/health
```

### 2. Configure Your MCP Client

Add SpecialAgent to your MCP client configuration:

```json
{
  "mcpServers": {
    "SpecialAgent": {
      "url": "http://localhost:8767/sse",
      "transport": "sse"
    }
  }
}
```

### 3. Connect and Build

Your AI assistant now has access to:
- Python execution with full UE5 API
- 71+ level design tools
- Viewport screenshot capture
- Editor utilities (save, undo, redo)

---

## Service Categories

| Service | Methods | Description |
|---------|:-------:|-------------|
| **Python** | 3 | Execute scripts, run files, list modules |
| **Screenshot** | 2 | Capture viewport for AI vision |
| **World** | 30+ | Actor manipulation and spatial queries |
| **Assets** | 4 | Content Browser search and inspection |
| **Landscape** | 5 | Terrain sculpting and layer painting |
| **Foliage** | 3 | Vegetation painting and removal |
| **Lighting** | 4 | Light spawning and configuration |
| **Streaming** | 4 | Sub-level loading and visibility |
| **Performance** | 3 | Statistics and overlap analysis |
| **Navigation** | 2 | NavMesh building and path testing |
| **Viewport** | 4 | Camera control and actor focus |
| **Utility** | 5 | Save, undo, redo, selection tools |
| **Gameplay** | 2 | Trigger volumes and player starts |

---

## Example Workflows

### Populate a Forest (via Tools)

```
1. assets/search → Find tree and rock assets
2. world/scatter_in_area → Place 500 trees with randomization
3. foliage/paint_in_area → Add grass and ground cover
4. screenshot/capture → Get visual for AI analysis
5. Iterate based on feedback
```

---

## Configuration

Edit `Config/DefaultSpecialAgent.ini` to customize:

```ini
[/Script/SpecialAgent.SpecialAgentSettings]
; Server port (change if 8767 is in use)
ServerPort=8767

; Auto-start server when editor launches
bAutoStart=true

; Enable verbose logging
bVerboseLogging=false
```

---

## Architecture

```
┌─────────────────────────────────────────┐
│        MCP Client (Claude, etc.)        │
└──────────────┬──────────────────────────┘
               │ HTTP/SSE + JSON-RPC 2.0
┌──────────────▼──────────────────────────┐
│       SpecialAgent MCP Server           │
│                                         │
│  ┌─────────────────────────────────┐    │
│  │   Python Service (Primary)      │    │
│  │   Full unreal module access     │    │
│  └─────────────────────────────────┘    │
│                                         │
│  ┌─────────────────────────────────┐    │
│  │   14 Services (71+ Tools)       │    │
│  │   Level design & utilities      │    │
│  └─────────────────────────────────┘    │
│                                         │
│  ┌─────────────────────────────────┐    │
│  │   Game Thread Dispatcher        │    │
│  │   Thread-safe API access        │    │
│  └─────────────────────────────────┘    │
└──────────────┬──────────────────────────┘
               │
┌──────────────▼──────────────────────────┐
│        Unreal Engine 5 Editor           │
└─────────────────────────────────────────┘
```

---

## Documentation

| Document | Description |
|----------|-------------|
| [QUICKSTART.md](QUICKSTART.md) | Step-by-step setup guide |
| [STRUCTURE.md](STRUCTURE.md) | Plugin architecture and file layout |

---

## Design Philosophy

The 71+ tools exist for convenience and discoverability. Python execution is the real power.

When your AI assistant sees `world/place_in_circle`, it learns circular placement is possible. But for custom logic—density falloff, terrain-aware positioning, asset variation based on rules—it writes Python.

Both layers work together: quick tools for common tasks, unlimited scripting for everything else.

---

## Troubleshooting

### Server Won't Start

- Check if port 8767 is in use: `netstat -an | grep 8767`
- Change port in `DefaultSpecialAgent.ini`
- Verify plugin is enabled in Edit → Plugins

### Connection Refused

- Ensure Unreal Editor is running
- Check Output Log for server startup messages
- Verify firewall isn't blocking localhost

### Tools Not Appearing

- Call `tools/list` to verify registration
- Check for errors in Output Log
- Restart the editor

### Client not connecting

- Some IDEs like Cursor may need to be started after your Unreal Engine editor as the connection attempt only occurs on startup.

---

## Technical Details

| Specification | Value |
|--------------|-------|
| Engine Version | UE 5.6–5.7+ |
| Platforms | Windows, Mac, Linux |
| Module Type | Editor |
| Transport | HTTP/SSE (native) |
| Protocol | JSON-RPC 2.0 / MCP |
| Default Port | 8767 |

### Dependencies

- `PythonScriptPlugin` (included with UE5)
- `EditorScriptingUtilities` (included with UE5)

---

## Contributing

Contributions are welcome! Please read the architecture documentation before submitting PRs.

---

## License

MIT License - See LICENSE file for details.

---

*Give your AI assistant the keys to Unreal Engine.*
