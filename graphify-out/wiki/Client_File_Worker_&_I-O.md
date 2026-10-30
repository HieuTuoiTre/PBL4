# Client File Worker & I/O

> 17 nodes · cohesion 0.18

## Key Concepts

- **FileWorker.cpp** (11 connections) — `Client/FileTransfer/FileWorker.cpp`
- **FileWorker** (11 connections) — `Client/FileTransfer/FileWorker.h`
- **.GetDirectoryContent()** (6 connections) — `Client/FileTransfer/FileWorker.h`
- **.GetDrives()** (5 connections) — `Client/FileTransfer/FileWorker.h`
- **ReceiveControlLoop()** (4 connections) — `Client/Core/SessionManager.cpp`
- **.OpenFileForRead()** (4 connections) — `Client/FileTransfer/FileWorker.h`
- **string** (3 connections)
- **.CloseFile()** (2 connections) — `Client/FileTransfer/FileWorker.h`
- **.IsEOF()** (2 connections) — `Client/FileTransfer/FileWorker.h`
- **.ReadNextChunk()** (2 connections) — `Client/FileTransfer/FileWorker.h`
- **sstream** (2 connections)
- **vector** (1 connections)
- **fileSize** (1 connections) — `Client/FileTransfer/FileWorker.h`
- **fileStream** (1 connections) — `Client/FileTransfer/FileWorker.h`
- **.GetFileSize()** (1 connections) — `Client/FileTransfer/FileWorker.h`
- **filesystem** (1 connections)
- **ifstream** (1 connections)

## Relationships

- [File Transfer Management](File_Transfer_Management.md) (5 shared connections)
- [Client Core & Session Management](Client_Core_&_Session_Management.md) (4 shared connections)
- [Remote Task Manager Interface](Remote_Task_Manager_Interface.md) (1 shared connections)

## Source Files

- `Client/Core/SessionManager.cpp`
- `Client/FileTransfer/FileWorker.cpp`
- `Client/FileTransfer/FileWorker.h`

## Audit Trail

- EXTRACTED: 33 (97%)
- INFERRED: 1 (3%)
- AMBIGUOUS: 0 (0%)

---

*Part of the graphify knowledge wiki. See [index](index.md) to navigate.*