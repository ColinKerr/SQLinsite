# Generates a C++ source that embeds the given asset files as byte arrays and
# exposes findEmbeddedAsset(name) (see src/embedded_assets.hpp). The generated
# source path is returned in OUT_SOURCE.
function(sqlinsite_embed_assets OUT_SOURCE)
  set(generated "${CMAKE_CURRENT_BINARY_DIR}/embedded_assets_generated.cpp")
  set(decls "")
  set(inits "")
  foreach(asset IN LISTS ARGN)
    get_filename_component(name "${asset}" NAME)
    string(MAKE_C_IDENTIFIER "${name}" ident)
    file(READ "${asset}" hex HEX)
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
    string(APPEND decls
      "static const unsigned char asset_${ident}[] = {${bytes}};\n")
    string(APPEND inits
      "  {\"${name}\", {reinterpret_cast<const char*>(asset_${ident}), sizeof(asset_${ident})}},\n")
    # Re-run CMake (and regenerate) when an asset changes.
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${asset}")
  endforeach()

  file(WRITE "${generated}"
"#include \"visualize/embedded_assets.hpp\"

#include <unordered_map>

namespace {
${decls}}  // namespace

const EmbeddedAsset* findEmbeddedAsset(const std::string& name) {
  static const std::unordered_map<std::string, EmbeddedAsset> kAssets = {
${inits}  };
  const auto it = kAssets.find(name);
  return it == kAssets.end() ? nullptr : &it->second;
}
")
  set(${OUT_SOURCE} "${generated}" PARENT_SCOPE)
endfunction()
