#pragma once

#include <string>

#include "../network.h"

namespace api {

class ApiPoster {
public:
    virtual ~ApiPoster() = default;

    virtual network::HttpResponse postJson(
        const std::string& path,
        const std::string& body
    ) const = 0;
};

} // namespace api
