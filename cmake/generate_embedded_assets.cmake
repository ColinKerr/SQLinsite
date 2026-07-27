# Generates a C++ source embedding every file in IN_DIR as a byte array and
# exposing findEmbeddedAsset(name) (see src/visualize/embedded_assets.hpp).
# Run at build time (after the front-end is built) so it sees the actual output:
#   cmake -DIN_DIR=<dir> -DOUT=<file.cpp> -P generate_embedded_assets.cmake

file(GLOB assets "${IN_DIR}/*")
set(decls "")
set(inits "")
foreach(asset IN LISTS assets)
  if(IS_DIRECTORY "${asset}")
    continue()
  endif()
  get_filename_component(name "${asset}" NAME)
  string(MAKE_C_IDENTIFIER "${name}" ident)
  file(READ "${asset}" hex HEX)
  string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
  string(APPEND decls
    "static const unsigned char asset_${ident}[] = {${bytes}};\n")
  string(APPEND inits
    "  {\"${name}\", {reinterpret_cast<const char*>(asset_${ident}), sizeof(asset_${ident})}},\n")
endforeach()

file(WRITE "${OUT}"
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
