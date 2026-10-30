# Graph Report - PBL4  (2026-10-30)

## Corpus Check
- Corpus is ~32,248 words - fits in a single context window. You may not need a graph.

## Summary
- 578 nodes · 1047 edges · 24 communities (23 shown, 1 thin omitted)
- Extraction: 100% EXTRACTED · 0% INFERRED · 0% AMBIGUOUS · INFERRED: 5 edges (avg confidence: 0.85)
- Token cost: 150 input · 80 output

## Community Hubs (Navigation)
- Client Core & Session Management
- File Transfer Management
- Connection Alert & Popups
- Connect Window & Host Cards
- Remote Task Manager Interface
- Remote Desktop Viewer Toolbar
- Manager Main & Network Dispatch
- Audio Format Conversion & Testing
- Audio Capture & Resampling Engine
- Communication Network Protocols
- Client In-Session Chat
- Manager In-Session Chat
- DirectX 11 Screen Capture Engine
- Client File Worker & I/O
- System Monitoring & Process Control
- Input Injection Engine
- System Tray Notification Icon
- Remote Input Event Handler
- Discovered Host Tracking
- UDP LAN Discovery Packets
- Discovery Message Types
- Mouse Button Definitions
- Mouse Action Events
- PBL4 Project Overview

## God Nodes (most connected - your core abstractions)
1. `MessageType` - 25 edges
2. `StatusOverlay` - 21 edges
3. `TcpClient` - 20 edges
4. `ConnectWndProc()` - 17 edges
5. `FileWndProc()` - 16 edges
6. `TcpServer` - 15 edges
7. `ProcessDisplayRow` - 13 edges
8. `TaskWndProc()` - 13 edges
9. `SimulatedPlayer` - 13 edges
10. `FileWorker` - 11 edges

## Surprising Connections (you probably didn't know these)
- `ReceiveControlLoop()` --references--> `ProcessInfoPayload`  [INFERRED]
  Client/Core/SessionManager.cpp → Shared/Protocol.h
- `Initialize()` --references--> `TcpClient`  [EXTRACTED]
  Manager/UI/InputHandler.cpp → Shared/Network.h
- `Initialize()` --references--> `TcpServer`  [EXTRACTED]
  Client/Core/ClientChatWindow.cpp → Shared/Network.h
- `FillPacket()` --references--> `DiscoveryPacket`  [EXTRACTED]
  Client/Core/DiscoveryResponder.cpp → Shared/Protocol.h
- `InjectMouse()` --references--> `MousePayload`  [EXTRACTED]
  Client/System/InputInjector.cpp → Shared/Protocol.h

## Import Cycles
- None detected.

## Communities (24 total, 1 thin omitted)

### Community 0 - "Client Core & Session Management"
Cohesion: 0.10
Nodes (10): FillPacket(), ListenLoop(), SendBroadcast(), SetBusy(), Start(), Stop(), ClientUpdateCallback, ListenLoop() (+2 more)

### Community 1 - "File Transfer Management"
Cohesion: 0.07
Nodes (22): FileManager, fileStream, FileManager::OpenFileForWrite(), AddRoundRectPath(), FileWndProc(), FormatBytes(), GetDpiScale(), HeaderSubclassProc() (+14 more)

### Community 2 - "Connection Alert & Popups"
Cohesion: 0.06
Nodes (23): ConnectionAlert, POPUP_HEIGHT, POPUP_WIDTH, TIMER_DISMISS, StartServer(), ConnectionAlert::Draw(), ConnectionAlert::Show(), ConnectionAlert::WndProc() (+15 more)

### Community 3 - "Connect Window & Host Cards"
Cohesion: 0.08
Nodes (29): AddRoundRectPath(), ClientDisplayItem, computerName, ip, isBusy, isOnline, lastSeenTick, userName (+21 more)

### Community 4 - "Remote Task Manager Interface"
Cohesion: 0.08
Nodes (29): AddRoundRectPath(), GetDpiScale(), HeaderSubclassProc(), PaintHeaderCards(), ProcessDisplayRow, allPids, appDisplayName, diskDisplay (+21 more)

### Community 5 - "Remote Desktop Viewer Toolbar"
Cohesion: 0.06
Nodes (31): AddRoundRectPath(), BuildToolbarButtons(), Create(), GetDpiScale(), GetFrameHeight(), GetFrameWidth(), GetToolbarHeight(), HitTestToolbar() (+23 more)

### Community 6 - "Manager Main & Network Dispatch"
Cohesion: 0.07
Nodes (17): Initialize(), WinMain(), ReceiveAudioLoop(), ReceiveControlLoop(), ReceiveVideoLoop(), Start(), CloseAll(), ConnectAll() (+9 more)

### Community 7 - "Audio Format Conversion & Testing"
Cohesion: 0.08
Nodes (22): AudioFormatConverter, inBitsPerSample, inChannels, inIsFloat, inSampleRate, lastInMono, resamplePos, resampleRatio (+14 more)

### Community 8 - "Audio Capture & Resampling Engine"
Cohesion: 0.08
Nodes (11): AudioFormatConverter, inBitsPerSample, inChannels, inIsFloat, inSampleRate, lastInMono, resamplePos, resampleRatio (+3 more)

### Community 9 - "Communication Network Protocols"
Cohesion: 0.08
Nodes (25): MessageType, MSG_AUDIO_CHUNK, MSG_CHAT_MESSAGE, MSG_DIR_REQUEST, MSG_DIR_RESPONSE, MSG_DISCONNECT, MSG_DRIVE_LIST_REQUEST, MSG_DRIVE_LIST_RESPONSE (+17 more)

### Community 10 - "Client In-Session Chat"
Cohesion: 0.14
Nodes (15): AppendMessageToHistory(), ClientChatWndProc(), ClientInputEditSubclassProc(), GetCurrentTimeString(), GetDpiScale(), Hide(), IsOpen(), OnMessageReceived() (+7 more)

### Community 11 - "Manager In-Session Chat"
Cohesion: 0.17
Nodes (11): AppendMessageToHistory(), ChatWndProc(), GetCurrentTimeString(), GetDpiScale(), Hide(), InputEditSubclassProc(), IsOpen(), OnMessageReceived() (+3 more)

### Community 12 - "DirectX 11 Screen Capture Engine"
Cohesion: 0.19
Nodes (14): CaptureDxgiFrame(), CaptureFrame(), CaptureGdiFrame(), Cleanup(), CleanupDxgi(), CleanupGdi(), CompressionConfig, quality (+6 more)

### Community 13 - "Client File Worker & I/O"
Cohesion: 0.18
Nodes (4): ReceiveControlLoop(), FileWorker, fileSize, fileStream

### Community 14 - "System Monitoring & Process Control"
Cohesion: 0.15
Nodes (10): EnableDebugPrivilege(), FileTimeToInt64(), GetProcessList(), GetSystemInfo(), Initialize(), UpdateSysInfo(), SysInfoPayload, cpuUsage (+2 more)

### Community 15 - "Input Injection Engine"
Cohesion: 0.14
Nodes (11): InjectKeyboard(), InjectMouse(), KeyboardPayload, isDown, vkCode, MousePayload, actionType, buttonType (+3 more)

### Community 16 - "System Tray Notification Icon"
Cohesion: 0.18
Nodes (3): Initialize(), ShowContextMenu(), TrayWindowProc()

### Community 17 - "Remote Input Event Handler"
Cohesion: 0.20
Nodes (4): Initialize(), ReleaseModifiers(), SendKey(), SendMouse()

### Community 18 - "Discovered Host Tracking"
Cohesion: 0.25
Nodes (6): DiscoveredClient, computerName, ip, isBusy, lastSeenTick, userName

### Community 19 - "UDP LAN Discovery Packets"
Cohesion: 0.33
Nodes (6): DiscoveryPacket, computerName, isBusy, magic, type, userName

### Community 20 - "Discovery Message Types"
Cohesion: 0.40
Nodes (5): DiscoveryType, DISCOVERY_BYE, DISCOVERY_HELLO, DISCOVERY_QUERY, DISCOVERY_RESPONSE

### Community 21 - "Mouse Button Definitions"
Cohesion: 0.40
Nodes (5): MouseButtonType, BUTTON_LEFT, BUTTON_MIDDLE, BUTTON_NONE, BUTTON_RIGHT

### Community 22 - "Mouse Action Events"
Cohesion: 0.40
Nodes (5): MouseEventType, MOUSE_ACTION_MOVE, MOUSE_ACTION_PRESS, MOUSE_ACTION_RELEASE, MOUSE_ACTION_WHEEL

## Knowledge Gaps
- **153 isolated node(s):** `TIMER_DISMISS`, `POPUP_WIDTH`, `POPUP_HEIGHT`, `Disconnected`, `ConnectedViewing` (+148 more)
  These have ≤1 connection - possible missing edges or undocumented components. (Counts symbols only; 249 node(s) total have ≤1 connection when file, concept and rationale nodes are included.)
- **1 thin communities (<3 nodes) omitted from report** — run `graphify query` to explore isolated nodes.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `MessageType` connect `Communication Network Protocols` to `Client Core & Session Management`, `Manager Main & Network Dispatch`?**
  _High betweenness centrality (0.073) - this node is a cross-community bridge._
- **What connects `TIMER_DISMISS`, `POPUP_WIDTH`, `POPUP_HEIGHT` to the rest of the system?**
  _153 weakly-connected nodes found - possible documentation gaps or missing edges._
- **Should `Client Core & Session Management` be split into smaller, more focused modules?**
  _Cohesion score 0.09935064935064936 - nodes in this community are weakly interconnected._
- **Why does `StatusOverlay` connect `Connection Alert & Popups` to `Client Core & Session Management`?**
  _High betweenness centrality (0.064) - this node is a cross-community bridge._
- **Should `File Transfer Management` be split into smaller, more focused modules?**
  _Cohesion score 0.06745098039215686 - nodes in this community are weakly interconnected._
- **Should `Connection Alert & Popups` be split into smaller, more focused modules?**
  _Cohesion score 0.06236786469344609 - nodes in this community are weakly interconnected._
- **Should `Connect Window & Host Cards` be split into smaller, more focused modules?**
  _Cohesion score 0.0824524312896406 - nodes in this community are weakly interconnected._