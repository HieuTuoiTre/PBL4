# Audio Format Conversion & Testing

> 32 nodes · cohesion 0.08

## Key Concepts

- **SimulatedPlayer** (13 connections) — `tests/test_audio_system.cpp`
- **test_audio_system.cpp** (12 connections) — `tests/test_audio_system.cpp`
- **AudioFormatConverter** (10 connections) — `tests/test_audio_system.cpp`
- **test_chat_system.cpp** (6 connections) — `tests/test_chat_system.cpp`
- **main()** (5 connections) — `tests/test_audio_system.cpp`
- **MockHdr** (5 connections) — `tests/test_audio_system.cpp`
- **iostream** (4 connections)
- **.Process()** (3 connections) — `tests/test_audio_system.cpp`
- **vector** (3 connections)
- **.Reset()** (3 connections) — `tests/test_audio_system.cpp`
- **cassert** (2 connections)
- **.Configure()** (2 connections) — `tests/test_audio_system.cpp`
- **.PlayChunk()** (2 connections) — `tests/test_audio_system.cpp`
- **main()** (2 connections) — `tests/test_chat_system.cpp`
- **inBitsPerSample** (1 connections) — `tests/test_audio_system.cpp`
- **inChannels** (1 connections) — `tests/test_audio_system.cpp`
- **inIsFloat** (1 connections) — `tests/test_audio_system.cpp`
- **inSampleRate** (1 connections) — `tests/test_audio_system.cpp`
- **lastInMono** (1 connections) — `tests/test_audio_system.cpp`
- **resamplePos** (1 connections) — `tests/test_audio_system.cpp`
- **resampleRatio** (1 connections) — `tests/test_audio_system.cpp`
- **DWORD** (1 connections)
- **dwFlags** (1 connections) — `tests/test_audio_system.cpp`
- **isPrepared** (1 connections) — `tests/test_audio_system.cpp`
- **audioIdx** (1 connections) — `tests/test_audio_system.cpp`
- *... and 7 more nodes in this community*

## Relationships

- [Client Core & Session Management](Client_Core_&_Session_Management.md) (6 shared connections)
- [Audio Capture & Resampling Engine](Audio_Capture_&_Resampling_Engine.md) (2 shared connections)
- [File Transfer Management](File_Transfer_Management.md) (2 shared connections)
- [DirectX 11 Screen Capture Engine](DirectX_11_Screen_Capture_Engine.md) (1 shared connections)
- [Manager Main & Network Dispatch](Manager_Main_&_Network_Dispatch.md) (1 shared connections)

## Source Files

- `tests/test_audio_system.cpp`
- `tests/test_chat_system.cpp`

## Audit Trail

- EXTRACTED: 51 (100%)
- INFERRED: 0 (0%)
- AMBIGUOUS: 0 (0%)

---

*Part of the graphify knowledge wiki. See [index](index.md) to navigate.*