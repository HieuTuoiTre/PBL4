# Graph Report - PBL4  (2026-10-30)

## Corpus Check
- Corpus is ~31,969 words - fits in a single context window. You may not need a graph.

## Summary
- 577 nodes · 1041 edges · 22 communities (21 shown, 1 thin omitted)
- Extraction: 100% EXTRACTED · 0% INFERRED · 0% AMBIGUOUS · INFERRED: 5 edges (avg confidence: 0.85)
- Token cost: 150 input · 120 output

## Community Hubs (Navigation)
- Client Lifecycle & Discovery
- File Transfer Management
- Manager UI Controls & Drawing
- Connection Alert Notifications
- Client Device List View
- Remote Task Manager UI
- Manager Entry & Stream Receivers
- Audio System Tests
- Audio Capture & Conversion
- Protocol Message Definitions
- Client Chat Interface
- Manager Chat Interface
- Screen Capture & Compression
- Remote File Worker
- System & Process Monitoring
- System Tray Icon
- Remote Input Control
- Discovery Packet Header
- Discovery Message Types
- Mouse Button Constants
- Mouse Event Constants
- Project Documentation & Build

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

## Communities (22 total, 1 thin omitted)

### Community 0 - "Client Lifecycle & Discovery"
Cohesion: 0.06
Nodes (27): FillPacket(), ListenLoop(), SendBroadcast(), SetBusy(), Start(), Stop(), InjectKeyboard(), InjectMouse() (+19 more)

### Community 1 - "File Transfer Management"
Cohesion: 0.07
Nodes (22): FileManager, fileStream, FileManager::OpenFileForWrite(), AddRoundRectPath(), FileWndProc(), FormatBytes(), GetDpiScale(), HeaderSubclassProc() (+14 more)

### Community 2 - "Manager UI Controls & Drawing"
Cohesion: 0.07
Nodes (31): AddRoundRectPath(), BuildToolbarButtons(), Create(), GetDpiScale(), GetFrameHeight(), GetFrameWidth(), GetToolbarHeight(), HitTestToolbar() (+23 more)

### Community 3 - "Connection Alert Notifications"
Cohesion: 0.06
Nodes (23): ConnectionAlert, POPUP_HEIGHT, POPUP_WIDTH, TIMER_DISMISS, StartServer(), ConnectionAlert::Draw(), ConnectionAlert::Show(), ConnectionAlert::WndProc() (+15 more)

### Community 4 - "Client Device List View"
Cohesion: 0.08
Nodes (29): AddRoundRectPath(), ClientDisplayItem, computerName, ip, isBusy, isOnline, lastSeenTick, userName (+21 more)

### Community 5 - "Remote Task Manager UI"
Cohesion: 0.08
Nodes (29): AddRoundRectPath(), GetDpiScale(), HeaderSubclassProc(), PaintHeaderCards(), ProcessDisplayRow, allPids, appDisplayName, diskDisplay (+21 more)

### Community 6 - "Manager Entry & Stream Receivers"
Cohesion: 0.07
Nodes (17): Initialize(), WinMain(), ReceiveAudioLoop(), ReceiveControlLoop(), ReceiveVideoLoop(), Start(), CloseAll(), ConnectAll() (+9 more)

### Community 7 - "Audio System Tests"
Cohesion: 0.08
Nodes (21): AudioFormatConverter, inBitsPerSample, inChannels, inIsFloat, inSampleRate, lastInMono, resamplePos, resampleRatio (+13 more)

### Community 8 - "Audio Capture & Conversion"
Cohesion: 0.08
Nodes (11): AudioFormatConverter, inBitsPerSample, inChannels, inIsFloat, inSampleRate, lastInMono, resamplePos, resampleRatio (+3 more)

### Community 9 - "Protocol Message Definitions"
Cohesion: 0.08
Nodes (25): MessageType, MSG_AUDIO_CHUNK, MSG_CHAT_MESSAGE, MSG_DIR_REQUEST, MSG_DIR_RESPONSE, MSG_DISCONNECT, MSG_DRIVE_LIST_REQUEST, MSG_DRIVE_LIST_RESPONSE (+17 more)

### Community 10 - "Client Chat Interface"
Cohesion: 0.14
Nodes (15): AppendMessageToHistory(), ClientChatWndProc(), ClientInputEditSubclassProc(), GetCurrentTimeString(), GetDpiScale(), Hide(), IsOpen(), OnMessageReceived() (+7 more)

### Community 11 - "Manager Chat Interface"
Cohesion: 0.17
Nodes (11): AppendMessageToHistory(), ChatWndProc(), GetCurrentTimeString(), GetDpiScale(), Hide(), InputEditSubclassProc(), IsOpen(), OnMessageReceived() (+3 more)

### Community 12 - "Screen Capture & Compression"
Cohesion: 0.19
Nodes (14): CaptureDxgiFrame(), CaptureFrame(), CaptureGdiFrame(), Cleanup(), CleanupDxgi(), CleanupGdi(), CompressionConfig, quality (+6 more)

### Community 13 - "Remote File Worker"
Cohesion: 0.18
Nodes (4): ReceiveControlLoop(), FileWorker, fileSize, fileStream

### Community 14 - "System & Process Monitoring"
Cohesion: 0.15
Nodes (10): EnableDebugPrivilege(), FileTimeToInt64(), GetProcessList(), GetSystemInfo(), Initialize(), UpdateSysInfo(), SysInfoPayload, cpuUsage (+2 more)

### Community 15 - "System Tray Icon"
Cohesion: 0.18
Nodes (3): Initialize(), ShowContextMenu(), TrayWindowProc()

### Community 16 - "Remote Input Control"
Cohesion: 0.20
Nodes (4): Initialize(), ReleaseModifiers(), SendKey(), SendMouse()

### Community 17 - "Discovery Packet Header"
Cohesion: 0.33
Nodes (6): DiscoveryPacket, computerName, isBusy, magic, type, userName

### Community 18 - "Discovery Message Types"
Cohesion: 0.40
Nodes (5): DiscoveryType, DISCOVERY_BYE, DISCOVERY_HELLO, DISCOVERY_QUERY, DISCOVERY_RESPONSE

### Community 19 - "Mouse Button Constants"
Cohesion: 0.40
Nodes (5): MouseButtonType, BUTTON_LEFT, BUTTON_MIDDLE, BUTTON_NONE, BUTTON_RIGHT

### Community 20 - "Mouse Event Constants"
Cohesion: 0.40
Nodes (5): MouseEventType, MOUSE_ACTION_MOVE, MOUSE_ACTION_PRESS, MOUSE_ACTION_RELEASE, MOUSE_ACTION_WHEEL

## Knowledge Gaps
- **154 isolated node(s):** `TIMER_DISMISS`, `POPUP_WIDTH`, `POPUP_HEIGHT`, `Disconnected`, `ConnectedViewing` (+149 more)
  These have ≤1 connection - possible missing edges or undocumented components. (Counts symbols only; 251 node(s) total have ≤1 connection when file, concept and rationale nodes are included.)
- **1 thin communities (<3 nodes) omitted from report** — run `graphify query` to explore isolated nodes.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `MessageType` connect `Protocol Message Definitions` to `Client Lifecycle & Discovery`, `Manager Entry & Stream Receivers`?**
  _High betweenness centrality (0.073) - this node is a cross-community bridge._
- **What connects `TIMER_DISMISS`, `POPUP_WIDTH`, `POPUP_HEIGHT` to the rest of the system?**
  _154 weakly-connected nodes found - possible documentation gaps or missing edges._
- **Should `Client Lifecycle & Discovery` be split into smaller, more focused modules?**
  _Cohesion score 0.06 - nodes in this community are weakly interconnected._
- **Why does `StatusOverlay` connect `Connection Alert Notifications` to `Client Lifecycle & Discovery`?**
  _High betweenness centrality (0.064) - this node is a cross-community bridge._
- **Should `File Transfer Management` be split into smaller, more focused modules?**
  _Cohesion score 0.06745098039215686 - nodes in this community are weakly interconnected._
- **Should `Manager UI Controls & Drawing` be split into smaller, more focused modules?**
  _Cohesion score 0.06767676767676768 - nodes in this community are weakly interconnected._
- **Should `Connection Alert Notifications` be split into smaller, more focused modules?**
  _Cohesion score 0.06236786469344609 - nodes in this community are weakly interconnected._