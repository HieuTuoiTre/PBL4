# Manager Main & Network Dispatch

> 42 nodes · cohesion 0.07

## Key Concepts

- **TcpClient** (20 connections) — `Shared/Network.h`
- **Network.cpp** (18 connections) — `Shared/Network.cpp`
- **TcpServer** (15 connections) — `Shared/Network.h`
- **WinMain()** (7 connections) — `Manager/Core/ManagerMain.cpp`
- **WinMain()** (5 connections) — `Client/Core/ClientMain.cpp`
- **Network** (4 connections) — `Shared/Network.h`
- **.Cleanup()** (4 connections) — `Shared/Network.h`
- **.Initialize()** (4 connections) — `Shared/Network.h`
- **ConnectAll()** (3 connections) — `Manager/Core/ManagerMain.cpp`
- **mutex** (3 connections)
- **.Connect()** (3 connections) — `Shared/Network.h`
- **.SendPacket()** (3 connections) — `Shared/Network.h`
- **.SendPacket()** (3 connections) — `Shared/Network.h`
- **Initialize()** (2 connections) — `Client/Core/ClientChatWindow.cpp`
- **ReceiveAudioLoop()** (2 connections) — `Manager/Core/DataReceiver.cpp`
- **ReceiveControlLoop()** (2 connections) — `Manager/Core/DataReceiver.cpp`
- **ReceiveVideoLoop()** (2 connections) — `Manager/Core/DataReceiver.cpp`
- **Start()** (2 connections) — `Manager/Core/DataReceiver.cpp`
- **CloseAll()** (2 connections) — `Manager/Core/ManagerMain.cpp`
- **.Close()** (2 connections) — `Shared/Network.h`
- **.ReceiveExact()** (2 connections) — `Shared/Network.h`
- **.TcpClient()** (2 connections) — `Shared/Network.h`
- **.AcceptConnection()** (2 connections) — `Shared/Network.h`
- **.CanSendWithoutBlocking()** (2 connections) — `Shared/Network.h`
- **.Close()** (2 connections) — `Shared/Network.h`
- *... and 17 more nodes in this community*

## Relationships

- [Client Core & Session Management](Client_Core_&_Session_Management.md) (14 shared connections)
- [Manager In-Session Chat](Manager_In-Session_Chat.md) (2 shared connections)
- [Communication Network Protocols](Communication_Network_Protocols.md) (2 shared connections)
- [Client In-Session Chat](Client_In-Session_Chat.md) (1 shared connections)
- [Audio Format Conversion & Testing](Audio_Format_Conversion_&_Testing.md) (1 shared connections)
- [File Transfer Management](File_Transfer_Management.md) (1 shared connections)
- [Remote Input Event Handler](Remote_Input_Event_Handler.md) (1 shared connections)
- [Remote Desktop Viewer Toolbar](Remote_Desktop_Viewer_Toolbar.md) (1 shared connections)
- [Remote Task Manager Interface](Remote_Task_Manager_Interface.md) (1 shared connections)

## Source Files

- `Client/Core/ClientChatWindow.cpp`
- `Client/Core/ClientMain.cpp`
- `Manager/Core/DataReceiver.cpp`
- `Manager/Core/ManagerMain.cpp`
- `Shared/Network.cpp`
- `Shared/Network.h`

## Audit Trail

- EXTRACTED: 81 (100%)
- INFERRED: 0 (0%)
- AMBIGUOUS: 0 (0%)

---

*Part of the graphify knowledge wiki. See [index](index.md) to navigate.*