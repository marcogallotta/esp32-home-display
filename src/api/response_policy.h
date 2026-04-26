#pragma once

#include "../network.h"

namespace api {

enum class ApiResponseKind {
    Accepted,
    TransportNetworkError,
    TransportTimeout,
    TransportTlsError,
    TransportInternalError,
    HttpClientError,
    HttpRetryableServerError,
    HttpPermanentServerError,
    HttpUnexpectedStatus,
};

inline ApiResponseKind classifyApiResponse(const network::HttpResponse& response) {
    if (response.transport == network::TransportResult::NetworkError) {
        return ApiResponseKind::TransportNetworkError;
    }

    if (response.transport == network::TransportResult::Timeout) {
        return ApiResponseKind::TransportTimeout;
    }

    if (response.transport == network::TransportResult::TlsError) {
        return ApiResponseKind::TransportTlsError;
    }

    if (response.transport == network::TransportResult::InternalError) {
        return ApiResponseKind::TransportInternalError;
    }

    if (response.statusCode >= 200 && response.statusCode < 300) {
        return ApiResponseKind::Accepted;
    }

    if (response.statusCode >= 400 && response.statusCode <= 499) {
        return ApiResponseKind::HttpClientError;
    }

    if (response.statusCode >= 502 && response.statusCode <= 504) {
        return ApiResponseKind::HttpRetryableServerError;
    }

    if (response.statusCode == 500 || response.statusCode >= 505) {
        return ApiResponseKind::HttpPermanentServerError;
    }

    return ApiResponseKind::HttpUnexpectedStatus;
}

} // namespace api
