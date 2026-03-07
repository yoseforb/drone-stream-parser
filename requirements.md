# C++ Embedded Developer – Homework Assignment (Version B)

## Objective - Streaming Parser with Resynchronization

Implement a multi-threaded C++ application that simulates the
communication layer of a counter-drone embedded unit. The system must
handle continuous binary streams, corrupted packets, and partial data
arrival as in real embedded/RF systems.

## Technical Requirements

\- Use C++17 or C++20

\- Target Linux (Ubuntu preferred)

\- Use CMake for build configuration

\- Must compile and run from command line

\- Proper multi-threading implementation required

\- Clean shutdown handling (graceful termination)

\- No memory leaks (RAII expected)

\- Provide a simple client to simulate sending test messages

\- Provide clear README explaining architecture and design decisions

## Estimated time to complete

24-36 hours

## System Behavior

### 1. TCP Server

\- The application must listen on a TCP port.

\- It must accept incoming client connections.

\- Data arrives as a continuous binary stream (not guaranteed full
packets).

### 2. Packet Format

Each message must follow the format:\
\
\[HEADER\]\[LENGTH\]\[PAYLOAD\]\[CRC\]\
\
HEADER: 0xAA55 (2 bytes)\
LENGTH: uint16_t – size of payload\
PAYLOAD: Telemetry data\
CRC: CRC16 calculated over HEADER + LENGTH + PAYLOAD

### 3. Telemetry Structure (Inside Payload)

struct Telemetry {\
std::string drone_id;\
double latitude;\
double longitude;\
double altitude;\
double speed;\
uint64_t timestamp;\
};

## Streaming & Parser Requirements

Incoming data must be treated as a continuous byte stream. The
implementation must correctly handle the following scenarios:

\- Packets arriving fragmented across multiple reads

\- Multiple packets arriving in a single buffer

\- Random corrupted bytes within the stream

\- CRC validation failures

\- Loss of synchronization with packet boundaries

### State Machine Parser

You must implement a state-machine based parser that:

\- Searches for valid HEADER (0xAA55)

\- Reads LENGTH safely

\- Buffers data until full packet is available

\- Validates CRC before processing

\- Resynchronizes automatically if CRC validation fails

\- Does not crash on malformed input

## Multi-threading Model

Minimum required threads:

\- Thread 1: Network listener (receives raw bytes)

\- Thread 2: Streaming parser

\- Thread 3+: Business logic processing

Ensure:

\- Thread-safe queues between layers

\- No data races

\- Proper synchronization mechanisms

\- Clean shutdown without deadlocks

## Business Logic

\- Maintain in-memory table of active drones

\- Update drone state on valid packet

\- Print alert if altitude \> 120m or speed \> 50 m/s

\- Log malformed packets and count CRC failures

## Performance Requirement

System should be capable of handling at least 1000 packets per second
(simulation acceptable).

## Deliverables

\- Public Git repository link

\- CMake build instructions

\- README explaining architecture and threading model

\- Explanation of parsing logic and resynchronization approach

\- Description of improvements for production deployment
