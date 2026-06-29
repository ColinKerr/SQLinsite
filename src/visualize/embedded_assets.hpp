#pragma once

#include <cstddef>
#include <string>

// A static asset compiled into the binary (see cmake/embed_assets.cmake).
struct EmbeddedAsset {
    const char* data;
    std::size_t size;
};

// Returns the asset registered under `name` (its file name), or nullptr.
const EmbeddedAsset* findEmbeddedAsset(const std::string& name);
