# System Monitoring & Process Control

> 16 nodes · cohesion 0.15

## Key Concepts

- **SysMonitor.cpp** (10 connections) — `Client/System/SysMonitor.cpp`
- **SysInfoPayload** (6 connections) — `Shared/Protocol.h`
- **GetSystemInfo()** (4 connections) — `Client/System/SysMonitor.cpp`
- **FileTimeToInt64()** (3 connections) — `Client/System/SysMonitor.cpp`
- **GetProcessList()** (3 connections) — `Client/System/SysMonitor.cpp`
- **Initialize()** (3 connections) — `Client/System/SysMonitor.cpp`
- **EnableDebugPrivilege()** (2 connections) — `Client/System/SysMonitor.cpp`
- **UpdateSysInfo()** (2 connections) — `Manager/UI/TaskManagerWindow.cpp`
- **vector** (1 connections)
- **KillProcess()** (1 connections) — `Client/System/SysMonitor.cpp`
- **FILETIME** (1 connections)
- **psapi** (1 connections)
- **cpuUsage** (1 connections) — `Shared/Protocol.h`
- **diskUsage** (1 connections) — `Shared/Protocol.h`
- **ramUsage** (1 connections) — `Shared/Protocol.h`
- **tlhelp32** (1 connections)

## Relationships

- [Client Core & Session Management](Client_Core_&_Session_Management.md) (3 shared connections)
- [Remote Task Manager Interface](Remote_Task_Manager_Interface.md) (2 shared connections)

## Source Files

- `Client/System/SysMonitor.cpp`
- `Manager/UI/TaskManagerWindow.cpp`
- `Shared/Protocol.h`

## Audit Trail

- EXTRACTED: 23 (100%)
- INFERRED: 0 (0%)
- AMBIGUOUS: 0 (0%)

---

*Part of the graphify knowledge wiki. See [index](index.md) to navigate.*