# Audio Capture & Resampling Engine

> 31 nodes · cohesion 0.08

## Key Concepts

- **AudioPlayer.cpp** (16 connections) — `Manager/Media/AudioPlayer.cpp`
- **AudioCapture.cpp** (13 connections) — `Client/Media/AudioCapture.cpp`
- **AudioFormatConverter** (10 connections) — `Client/Media/AudioCapture.cpp`
- **algorithm** (7 connections)
- **cmath** (7 connections)
- **CaptureLoop()** (5 connections) — `Client/Media/AudioCapture.cpp`
- **.Configure()** (3 connections) — `Client/Media/AudioCapture.cpp`
- **.Process()** (3 connections) — `Client/Media/AudioCapture.cpp`
- **Start()** (3 connections) — `Client/Media/AudioCapture.cpp`
- **vector** (2 connections)
- **Stop()** (2 connections) — `Client/Media/AudioCapture.cpp`
- **audioclient** (1 connections)
- **inBitsPerSample** (1 connections) — `Client/Media/AudioCapture.cpp`
- **inChannels** (1 connections) — `Client/Media/AudioCapture.cpp`
- **inIsFloat** (1 connections) — `Client/Media/AudioCapture.cpp`
- **inSampleRate** (1 connections) — `Client/Media/AudioCapture.cpp`
- **lastInMono** (1 connections) — `Client/Media/AudioCapture.cpp`
- **resamplePos** (1 connections) — `Client/Media/AudioCapture.cpp`
- **resampleRatio** (1 connections) — `Client/Media/AudioCapture.cpp`
- **function** (1 connections)
- **Cleanup()** (1 connections) — `Manager/Media/AudioPlayer.cpp`
- **DecreaseVolume()** (1 connections) — `Manager/Media/AudioPlayer.cpp`
- **GetVolume()** (1 connections) — `Manager/Media/AudioPlayer.cpp`
- **IncreaseVolume()** (1 connections) — `Manager/Media/AudioPlayer.cpp`
- **Initialize()** (1 connections) — `Manager/Media/AudioPlayer.cpp`
- *... and 6 more nodes in this community*

## Relationships

- [Client Core & Session Management](Client_Core_&_Session_Management.md) (11 shared connections)
- [Remote Task Manager Interface](Remote_Task_Manager_Interface.md) (2 shared connections)
- [Audio Format Conversion & Testing](Audio_Format_Conversion_&_Testing.md) (2 shared connections)
- [DirectX 11 Screen Capture Engine](DirectX_11_Screen_Capture_Engine.md) (1 shared connections)
- [Connect Window & Host Cards](Connect_Window_&_Host_Cards.md) (1 shared connections)
- [Client In-Session Chat](Client_In-Session_Chat.md) (1 shared connections)
- [File Transfer Management](File_Transfer_Management.md) (1 shared connections)

## Source Files

- `Client/Media/AudioCapture.cpp`
- `Manager/Media/AudioPlayer.cpp`

## Audit Trail

- EXTRACTED: 55 (100%)
- INFERRED: 0 (0%)
- AMBIGUOUS: 0 (0%)

---

*Part of the graphify knowledge wiki. See [index](index.md) to navigate.*