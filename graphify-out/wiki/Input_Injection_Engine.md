# Input Injection Engine

> 14 nodes · cohesion 0.14

## Key Concepts

- **MousePayload** (7 connections) — `Shared/Protocol.h`
- **InputInjector.cpp** (5 connections) — `Client/System/InputInjector.cpp`
- **InputInjector.h** (4 connections) — `Client/System/InputInjector.h`
- **KeyboardPayload** (4 connections) — `Shared/Protocol.h`
- **InjectKeyboard()** (2 connections) — `Client/System/InputInjector.cpp`
- **InjectMouse()** (2 connections) — `Client/System/InputInjector.cpp`
- **ReleaseAllModifiers()** (1 connections) — `Client/System/InputInjector.cpp`
- **isDown** (1 connections) — `Shared/Protocol.h`
- **vkCode** (1 connections) — `Shared/Protocol.h`
- **actionType** (1 connections) — `Shared/Protocol.h`
- **buttonType** (1 connections) — `Shared/Protocol.h`
- **wheelDelta** (1 connections) — `Shared/Protocol.h`
- **x** (1 connections) — `Shared/Protocol.h`
- **y** (1 connections) — `Shared/Protocol.h`

## Relationships

- [Client Core & Session Management](Client_Core_&_Session_Management.md) (6 shared connections)

## Source Files

- `Client/System/InputInjector.cpp`
- `Client/System/InputInjector.h`
- `Shared/Protocol.h`

## Audit Trail

- EXTRACTED: 19 (100%)
- INFERRED: 0 (0%)
- AMBIGUOUS: 0 (0%)

---

*Part of the graphify knowledge wiki. See [index](index.md) to navigate.*