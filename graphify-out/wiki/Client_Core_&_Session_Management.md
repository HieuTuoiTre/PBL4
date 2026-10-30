# Client Core & Session Management

> 56 nodes · cohesion 0.10

## Key Concepts

- **Protocol.h** (31 connections) — `Shared/Protocol.h`
- **RemoteWindow.cpp** (27 connections) — `Manager/UI/RemoteWindow.cpp`
- **SessionManager.cpp** (21 connections) — `Client/Core/SessionManager.cpp`
- **string** (21 connections)
- **windows** (21 connections)
- **Network.h** (19 connections) — `Shared/Network.h`
- **DiscoveryResponder.cpp** (17 connections) — `Client/Core/DiscoveryResponder.cpp`
- **DataReceiver.cpp** (16 connections) — `Manager/Core/DataReceiver.cpp`
- **DiscoveryScanner.cpp** (14 connections) — `Manager/Core/DiscoveryScanner.cpp`
- **ManagerMain.cpp** (14 connections) — `Manager/Core/ManagerMain.cpp`
- **vector** (12 connections)
- **ClientMain.cpp** (11 connections) — `Client/Core/ClientMain.cpp`
- **StatusOverlay.h** (10 connections) — `Client/Core/StatusOverlay.h`
- **cstring** (10 connections)
- **thread** (10 connections)
- **atomic** (9 connections)
- **DiscoveryScanner.h** (8 connections) — `Manager/Core/DiscoveryScanner.h`
- **FileWorker.h** (7 connections) — `Client/FileTransfer/FileWorker.h`
- **gdiplus** (7 connections)
- **ChatWindow.h** (7 connections) — `Manager/UI/ChatWindow.h`
- **ConnectWindow.h** (7 connections) — `Manager/UI/ConnectWindow.h`
- **FileExplorerWindow.h** (7 connections) — `Manager/UI/FileExplorerWindow.h`
- **ClientChatWindow.h** (6 connections) — `Client/Core/ClientChatWindow.h`
- **ConnectionAlert.h** (6 connections) — `Client/Core/ConnectionAlert.h`
- **InputHandler.h** (6 connections) — `Manager/UI/InputHandler.h`
- *... and 31 more nodes in this community*

## Relationships

- [Remote Desktop Viewer Toolbar](Remote_Desktop_Viewer_Toolbar.md) (15 shared connections)
- [Manager Main & Network Dispatch](Manager_Main_&_Network_Dispatch.md) (14 shared connections)
- [File Transfer Management](File_Transfer_Management.md) (13 shared connections)
- [Audio Capture & Resampling Engine](Audio_Capture_&_Resampling_Engine.md) (11 shared connections)
- [Connection Alert & Popups](Connection_Alert_&_Popups.md) (8 shared connections)
- [Connect Window & Host Cards](Connect_Window_&_Host_Cards.md) (7 shared connections)
- [Client In-Session Chat](Client_In-Session_Chat.md) (6 shared connections)
- [Input Injection Engine](Input_Injection_Engine.md) (6 shared connections)
- [Audio Format Conversion & Testing](Audio_Format_Conversion_&_Testing.md) (6 shared connections)
- [DirectX 11 Screen Capture Engine](DirectX_11_Screen_Capture_Engine.md) (4 shared connections)
- [Client File Worker & I/O](Client_File_Worker_&_I-O.md) (4 shared connections)
- [Manager In-Session Chat](Manager_In-Session_Chat.md) (4 shared connections)

## Source Files

- `Client/Core/ClientChatWindow.h`
- `Client/Core/ClientMain.cpp`
- `Client/Core/ConnectionAlert.h`
- `Client/Core/DiscoveryResponder.cpp`
- `Client/Core/DiscoveryResponder.h`
- `Client/Core/SessionManager.cpp`
- `Client/Core/SessionManager.h`
- `Client/Core/StatusOverlay.h`
- `Client/FileTransfer/FileWorker.h`
- `Client/Media/AudioCapture.h`
- `Client/Media/ScreenCapture.h`
- `Client/System/SysMonitor.h`
- `Manager/Core/DataReceiver.cpp`
- `Manager/Core/DataReceiver.h`
- `Manager/Core/DiscoveryScanner.cpp`
- `Manager/Core/DiscoveryScanner.h`
- `Manager/Core/ManagerMain.cpp`
- `Manager/Media/AudioPlayer.h`
- `Manager/UI/ChatWindow.h`
- `Manager/UI/ConnectWindow.h`

## Audit Trail

- EXTRACTED: 270 (100%)
- INFERRED: 0 (0%)
- AMBIGUOUS: 0 (0%)

---

*Part of the graphify knowledge wiki. See [index](index.md) to navigate.*