#ifndef TELEMETRY_PARSER_HPP
#define TELEMETRY_PARSER_HPP

#include <functional>

#include "stream_parser.hpp"
#include "telemetry.hpp"

/// Creates a StreamParser pre-wired with PacketDeserializer.
///
/// The returned parser accepts raw TCP bytes via feed() and invokes the
/// callback with deserialized Telemetry structs. Payloads that fail
/// deserialization are silently dropped.
auto makeTelemetryParser(std::function<void(Telemetry)> on_telemetry)
    -> StreamParser;

#endif // TELEMETRY_PARSER_HPP
