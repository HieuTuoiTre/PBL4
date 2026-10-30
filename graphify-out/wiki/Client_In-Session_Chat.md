# Client In-Session Chat

> 24 nodes · cohesion 0.14

## Key Concepts

- **ClientChatWindow.cpp** (22 connections) — `Client/Core/ClientChatWindow.cpp`
- **ClientChatWndProc()** (9 connections) — `Client/Core/ClientChatWindow.cpp`
- **ClientInputEditSubclassProc()** (6 connections) — `Client/Core/ClientChatWindow.cpp`
- **ChatMessagePayload** (6 connections) — `Shared/Protocol.h`
- **SendCurrentMessage()** (5 connections) — `Client/Core/ClientChatWindow.cpp`
- **AppendMessageToHistory()** (4 connections) — `Client/Core/ClientChatWindow.cpp`
- **GetCurrentTimeString()** (4 connections) — `Client/Core/ClientChatWindow.cpp`
- **OnMessageReceived()** (4 connections) — `Client/Core/ClientChatWindow.cpp`
- **Toggle()** (4 connections) — `Client/Core/ClientChatWindow.cpp`
- **HWND** (3 connections)
- **GetDpiScale()** (3 connections) — `Client/Core/ClientChatWindow.cpp`
- **LPARAM** (2 connections)
- **LRESULT** (2 connections)
- **UINT** (2 connections)
- **WPARAM** (2 connections)
- **wstring** (2 connections)
- **Hide()** (2 connections) — `Client/Core/ClientChatWindow.cpp`
- **IsOpen()** (2 connections) — `Client/Core/ClientChatWindow.cpp`
- **Show()** (2 connections) — `Client/Core/ClientChatWindow.cpp`
- **ChatUIThreadProc()** (1 connections) — `Client/Core/ClientChatWindow.cpp`
- **Close()** (1 connections) — `Client/Core/ClientChatWindow.cpp`
- **message** (1 connections) — `Shared/Protocol.h`
- **sender** (1 connections) — `Shared/Protocol.h`
- **timestamp** (1 connections) — `Shared/Protocol.h`

## Relationships

- [Client Core & Session Management](Client_Core_&_Session_Management.md) (6 shared connections)
- [File Transfer Management](File_Transfer_Management.md) (3 shared connections)
- [Manager In-Session Chat](Manager_In-Session_Chat.md) (2 shared connections)
- [Audio Capture & Resampling Engine](Audio_Capture_&_Resampling_Engine.md) (1 shared connections)
- [Manager Main & Network Dispatch](Manager_Main_&_Network_Dispatch.md) (1 shared connections)

## Source Files

- `Client/Core/ClientChatWindow.cpp`
- `Shared/Protocol.h`

## Audit Trail

- EXTRACTED: 52 (100%)
- INFERRED: 0 (0%)
- AMBIGUOUS: 0 (0%)

---

*Part of the graphify knowledge wiki. See [index](index.md) to navigate.*