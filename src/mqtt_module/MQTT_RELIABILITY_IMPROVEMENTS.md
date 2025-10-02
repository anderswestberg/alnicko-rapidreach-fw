# MQTT Reliability Improvements

## Overview

This document outlines a proposed architecture to improve MQTT reliability without requiring C++. The solution uses pure C with proper threading and encapsulation.

## Current Problems

1. **Blocking Operations**: The current implementation blocks the MQTT thread during:
   - Large payload reads (`mqtt_read_publish_payload_blocking`)
   - File I/O operations
   - Message processing
   
2. **Thread Safety Issues**: 
   - MQTT client accessed from multiple contexts without proper synchronization
   - Shared state not properly protected

3. **Poor Separation of Concerns**:
   - Protocol handling mixed with application logic
   - Direct callbacks from MQTT thread
   - File I/O in event handlers

## Proposed Architecture

### 1. Thread Separation

```
┌─────────────────┐     ┌──────────────────┐     ┌─────────────────┐
│ Protocol Thread │────▶│  Message Queue   │────▶│ Worker Thread   │
│                 │     └──────────────────┘     │                 │
│ - mqtt_input()  │                              │ - File I/O      │
│ - mqtt_live()   │     ┌──────────────────┐     │ - Audio decode  │
│ - Reconnection  │◀────│  Publish Queue   │     │ - Callbacks     │
└─────────────────┘     └──────────────────┘     └─────────────────┘
```

### 2. Key Components

#### Protocol Thread
- Dedicated to MQTT protocol operations
- Never blocks on application logic
- Handles:
  - Incoming packet processing (`mqtt_input`)
  - Keepalive transmission (`mqtt_live`)
  - Automatic reconnection with backoff
  - Queued message transmission

#### Message Queues
- **Receive Queue**: Decouples message reception from processing
- **Publish Queue**: Allows non-blocking publishes
- Fixed-size entries with dynamic payload allocation

#### Worker Thread
- Processes received messages
- Safe to perform blocking operations:
  - File I/O
  - Audio processing
  - Database operations
- Calls application callbacks

### 3. API Design

```c
// Simple, thread-safe API
mqtt_handle_t client = mqtt_client_init(&config);
mqtt_client_connect(client, connection_callback, user_data);
mqtt_client_subscribe(client, &subscription_config);
mqtt_client_publish(client, topic, payload, len, qos, retain);
```

### 4. Benefits

1. **Non-blocking**: All API calls return immediately
2. **Thread-safe**: Proper mutex protection for all shared state
3. **Reliable**: Protocol thread never blocked by application logic
4. **Scalable**: Easy to add more worker threads if needed
5. **Clean**: Clear separation between protocol and application

### 5. Memory Considerations

- No dynamic memory allocation in critical paths
- Fixed-size message queues
- Payload memory allocated from heap with limits
- No C++ overhead (no vtables, RTTI, exceptions)

### 6. Implementation Strategy

1. **Phase 1**: Implement wrapper alongside existing code
2. **Phase 2**: Migrate components to use new API
3. **Phase 3**: Remove old implementation

### 7. Testing

- Unit tests for queue operations
- Integration tests for reconnection scenarios
- Stress tests with large payloads
- Network failure simulation

## Alternative: Minimal C++ Usage

If C++ is desired, we could enable it with minimal features:

```conf
# Enable C++ but disable expensive features
CONFIG_CPP=y
CONFIG_STD_CPP98=y      # Use older, smaller standard
CONFIG_EXCEPTIONS=n     # No exception handling
CONFIG_RTTI=n          # No runtime type information
```

Benefits of C++ approach:
- Better encapsulation with classes
- RAII for resource management
- Type-safe callbacks with templates

However, the pure C approach is recommended for embedded systems due to:
- Smaller code size
- Predictable memory usage
- Better toolchain support
- Easier debugging

## Conclusion

The proposed architecture addresses all identified reliability issues without requiring C++. It provides clean separation of concerns, proper threading, and non-blocking operation while maintaining the simplicity and efficiency required for embedded systems.
