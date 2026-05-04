/*
 * Copyright (C) EdgeTX
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 */

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#include "definitions.h"
#include "storage/yaml/yaml_node.h"
#include "storage/yaml/yaml_parser.h"
#include "storage/yaml/yaml_tree_walker.h"

struct FuzzStruct {
  uint8_t foo;
  uint8_t bar;
  uint16_t count;
  char name[16];
};

static const YamlNode struct_FuzzStruct[] = {
  YAML_UNSIGNED("foo", 8),
  YAML_UNSIGNED("bar", 8),
  YAML_UNSIGNED("count", 16),
  YAML_STRING("name", 16),
  YAML_END,
};

static const YamlNode struct_fuzz[] = {
  YAML_STRUCT("testStruct", sizeof(FuzzStruct) * 8, struct_FuzzStruct, nullptr),
  YAML_END,
};

static const YamlNode root_node = YAML_ROOT(struct_fuzz);

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
  FuzzStruct target = {};
  YamlTreeWalker tree;
  YamlParser parser;

  tree.reset(&root_node, reinterpret_cast<uint8_t *>(&target));
  parser.init(YamlTreeWalker::get_parser_calls(), &tree);

  if (size > 0) {
    parser.parse(reinterpret_cast<const char *>(data), size);
  }

  parser.set_eof();
  const std::string newline = "\n";
  parser.parse(newline.data(), newline.size());

  return 0;
}
