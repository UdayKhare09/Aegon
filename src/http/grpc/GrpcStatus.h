#pragma once

#include <cstdint>
#include <string_view>

namespace aegon::http::grpc {

/**
 * @brief Standard gRPC status codes as defined in the gRPC wire specification.
 */
enum class StatusCode : uint32_t {
    Ok = 0,
    Cancelled = 1,
    Unknown = 2,
    InvalidArgument = 3,
    DeadlineExceeded = 4,
    NotFound = 5,
    AlreadyExists = 6,
    PermissionDenied = 7,
    ResourceExhausted = 8,
    FailedPrecondition = 9,
    Aborted = 10,
    OutOfRange = 11,
    Unimplemented = 12,
    Internal = 13,
    Unavailable = 14,
    DataLoss = 15,
    Unauthenticated = 16
};

constexpr std::string_view status_code_to_string(StatusCode code) noexcept {
    switch (code) {
        case StatusCode::Ok: return "0";
        case StatusCode::Cancelled: return "1";
        case StatusCode::Unknown: return "2";
        case StatusCode::InvalidArgument: return "3";
        case StatusCode::DeadlineExceeded: return "4";
        case StatusCode::NotFound: return "5";
        case StatusCode::AlreadyExists: return "6";
        case StatusCode::PermissionDenied: return "7";
        case StatusCode::ResourceExhausted: return "8";
        case StatusCode::FailedPrecondition: return "9";
        case StatusCode::Aborted: return "10";
        case StatusCode::OutOfRange: return "11";
        case StatusCode::Unimplemented: return "12";
        case StatusCode::Internal: return "13";
        case StatusCode::Unavailable: return "14";
        case StatusCode::DataLoss: return "15";
        case StatusCode::Unauthenticated: return "16";
    }
    return "2"; // Unknown
}

constexpr std::string_view status_code_name(StatusCode code) noexcept {
    switch (code) {
        case StatusCode::Ok: return "OK";
        case StatusCode::Cancelled: return "CANCELLED";
        case StatusCode::Unknown: return "UNKNOWN";
        case StatusCode::InvalidArgument: return "INVALID_ARGUMENT";
        case StatusCode::DeadlineExceeded: return "DEADLINE_EXCEEDED";
        case StatusCode::NotFound: return "NOT_FOUND";
        case StatusCode::AlreadyExists: return "ALREADY_EXISTS";
        case StatusCode::PermissionDenied: return "PERMISSION_DENIED";
        case StatusCode::ResourceExhausted: return "RESOURCE_EXHAUSTED";
        case StatusCode::FailedPrecondition: return "FAILED_PRECONDITION";
        case StatusCode::Aborted: return "ABORTED";
        case StatusCode::OutOfRange: return "OUT_OF_RANGE";
        case StatusCode::Unimplemented: return "UNIMPLEMENTED";
        case StatusCode::Internal: return "INTERNAL";
        case StatusCode::Unavailable: return "UNAVAILABLE";
        case StatusCode::DataLoss: return "DATA_LOSS";
        case StatusCode::Unauthenticated: return "UNAUTHENTICATED";
    }
    return "UNKNOWN";
}

} // namespace aegon::http::grpc
