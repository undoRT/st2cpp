/**
 * @file LibraryLoader.cpp
 * @brief LibraryLoader implementation: JSON -> LibraryDescriptor (validated)
 * @author Salvatore Bamundo
 * @date 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#include "library/LibraryLoader.h"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <limits>
#include <sstream>
#include <unordered_set>

namespace st2cpp::library {

namespace {

/**
 * @brief Supported schema version of the Library Descriptor format.
 */
// 1.0 is the initial schema. 1.1 adds the function block member/method
// description; those sections are simply absent from a 1.0 descriptor, so both
// versions are accepted and read into the same model.
constexpr const char* kSchemaVersion10 = "1.0";
constexpr const char* kSchemaVersion11 = "1.1";

using json::JsonType;
using json::JsonValue;

/**
 * @brief Map an IEC 61131-3 elementary type name onto the AST BaseType.
 * @param name Case-insensitive type name ("INT", "REAL", "TIME", ...)
 * @param out Receives the mapped BaseType
 * @return true when the name is a known elementary type
 */
bool parseBaseType(const std::string& name, BaseType& out)
{
   std::string key;
   key.reserve(name.size());
   for (unsigned char c : name) {
      key.push_back(static_cast<char>(std::toupper(c)));
   }
   if (key == "BOOL") {
      out = BaseType::BOOL;
   } else if (key == "SINT") {
      out = BaseType::SINT;
   } else if (key == "INT") {
      out = BaseType::INT;
   } else if (key == "DINT") {
      out = BaseType::DINT;
   } else if (key == "LINT") {
      out = BaseType::LINT;
   } else if (key == "USINT") {
      out = BaseType::USINT;
   } else if (key == "UINT") {
      out = BaseType::UINT;
   } else if (key == "UDINT") {
      out = BaseType::UDINT;
   } else if (key == "ULINT") {
      out = BaseType::ULINT;
   } else if (key == "REAL") {
      out = BaseType::REAL;
   } else if (key == "LREAL") {
      out = BaseType::LREAL;
   } else if (key == "BYTE") {
      out = BaseType::BYTE;
   } else if (key == "WORD") {
      out = BaseType::WORD;
   } else if (key == "DWORD") {
      out = BaseType::DWORD;
   } else if (key == "LWORD") {
      out = BaseType::LWORD;
   } else if (key == "STRING") {
      out = BaseType::STRING;
   } else if (key == "WSTRING") {
      out = BaseType::WSTRING;
   } else if (key == "TIME") {
      out = BaseType::TIME;
   } else if (key == "DATE") {
      out = BaseType::DATE;
   } else if (key == "TOD") {
      out = BaseType::TOD;
   } else if (key == "DT") {
      out = BaseType::DT;
   } else {
      return false;
   }
   return true;
}

/**
 * @brief Classification of a type reference used for structural validation
 * of initializers.
 */
enum class TargetCat {
   Primitive,
   Enum,
   Struct,
   Array,
   Opaque // external reference: internal layout unknown
};

struct TargetType
{
   TargetCat cat = TargetCat::Opaque;
   BaseType base = BaseType::VOID;
   std::string name; // type name (enum/struct)
   const EnumTypeDef* en = nullptr;
   const StructTypeDef* st = nullptr;
   std::shared_ptr<TypeRef> elem; // array element
};

/**
 * @brief Builds and validates a LibraryDescriptor from a parsed JSON value.
 * @details Deserialization is split in two phases:
 * - Phase 1 (parse*): structural deserialization with per-field checks that do
 *   not depend on other entities (required members, primitive names, array
 *   bounds ordering, keyword spelling).
 * - Phase 2 (validate): cross-entity validation (name uniqueness, type
 *   resolution, dependency coherence, binding semantics, initializer
 *   structural compatibility, constraint parsing).
 * Phase 2 runs after every name list is known so that descriptors are
 * order-independent (a type may reference a type declared later).
 */
class Builder
{
public:
   explicit Builder(const JsonValue& root) : root_{&root} {}

   LibraryLoadResult build()
   {
      parseRoot();
      if (errors_.empty()) {
         validate();
      }
      LibraryLoadResult result;
      if (errors_.empty()) {
         result.descriptor = std::move(desc_);
      }
      result.errors = std::move(errors_);
      return result;
   }

private:
   const JsonValue* root_;
   LibraryDescriptor desc_;
   std::vector<LibraryLoadError> errors_;

   // ---- error helpers ----
   void error(const std::string& path, const std::string& message) { errors_.push_back({path, message}); }

   // ---- generic member readers (report type mismatches) ----
   bool getString(const JsonValue& obj, const std::string& key, const std::string& path, std::string& out)
   {
      const JsonValue* v = obj.find(key);
      if (!v) {
         return false;
      }
      if (!v->isString()) {
         error(path + "." + key, "expected a string");
         return false;
      }
      out = v->text;
      return true;
   }

   bool requireString(const JsonValue& obj, const std::string& key, const std::string& path, std::string& out)
   {
      const JsonValue* v = obj.find(key);
      if (!v) {
         error(path, "missing required member '" + key + "'");
         return false;
      }
      if (!v->isString()) {
         error(path + "." + key, "expected a string");
         return false;
      }
      out = v->text;
      return true;
   }

   bool getBool(const JsonValue& obj, const std::string& key, const std::string& path, bool& out)
   {
      const JsonValue* v = obj.find(key);
      if (!v) {
         return false;
      }
      if (!v->isBool()) {
         error(path + "." + key, "expected a boolean");
         return false;
      }
      out = v->boolean;
      return true;
   }

   bool getInt(const JsonValue& obj, const std::string& key, const std::string& path, int& out)
   {
      const JsonValue* v = obj.find(key);
      if (!v) {
         return false;
      }
      const std::optional<int64_t> value = v->asInt64();
      if (!value.has_value()) {
         error(path + "." + key, "expected an integer");
         return false;
      }
      if (*value < std::numeric_limits<int>::min() || *value > std::numeric_limits<int>::max()) {
         error(path + "." + key, "integer out of range");
         return false;
      }
      out = static_cast<int>(*value);
      return true;
   }

   // ---- TypeRef parsing ----
   TypeRef parseTypeRef(const JsonValue& obj, const std::string& path)
   {
      TypeRef ref;
      std::string kind;
      if (!requireString(obj, "kind", path, kind)) {
         ref.kind = TypeRefKind::Named;
         return ref;
      }
      const std::string key = LibraryDescriptor::makeKey(kind);
      if (key == "PRIMITIVE") {
         ref.kind = TypeRefKind::Primitive;
         std::string name;
         if (requireString(obj, "name", path, name)) {
            if (!parseBaseType(name, ref.primitive)) {
               error(path + ".name", "unknown primitive type '" + name + "'");
            }
         }
      } else if (key == "NAMED") {
         ref.kind = TypeRefKind::Named;
         requireString(obj, "name", path, ref.name);
         getString(obj, "library", path, ref.library);
      } else if (key == "ARRAY") {
         ref.kind = TypeRefKind::Array;
         if (!getInt(obj, "lowerBound", path, ref.lowerBound)) {
            error(path, "missing required member 'lowerBound'");
         }
         if (!getInt(obj, "upperBound", path, ref.upperBound)) {
            error(path, "missing required member 'upperBound'");
         }
         if (ref.lowerBound > ref.upperBound) {
            error(path,
                  "invalid array bounds: lowerBound (" + std::to_string(ref.lowerBound) + ") must be <= upperBound ("
                     + std::to_string(ref.upperBound) + ")");
         }
         const JsonValue* elem = obj.find("elementType");
         if (!elem || !elem->isObject()) {
            error(path, "missing required member 'elementType'");
         } else {
            ref.elementType = std::make_shared<TypeRef>(parseTypeRef(*elem, path + ".elementType"));
         }
      } else {
         error(path + ".kind", "unsupported type ref kind '" + kind + "'");
         ref.kind = TypeRefKind::Named;
      }
      return ref;
   }

   // ---- InitValue parsing ----
   /**
     * @brief Parse an initializer node.
     * @details Accepts both the full object form
     *   {"kind":"scalar","value": 0.0}
     * and a bare scalar (number/string/bool) which is treated as
     * {"kind":"scalar","value": <scalar>}. The bare form is used by the v1.0
     * sample for sparse entries ("value": 100, "default": 0) and list values.
     */
   InitValue parseInitValue(const JsonValue& node, const std::string& path)
   {
      if (!node.isObject()) {
         return scalarValue(node, path);
      }
      InitValue init;
      std::string kind;
      if (!requireString(node, "kind", path, kind)) {
         return init;
      }
      const std::string key = LibraryDescriptor::makeKey(kind);
      if (key == "SCALAR") {
         const JsonValue* value = node.find("value");
         if (!value) {
            error(path, "missing required member 'value' for scalar initializer");
            return init;
         }
         init.kind = InitKind::Scalar;
         init.scalar = value->asString();
      } else if (key == "DEFAULT") {
         init.kind = InitKind::Default;
      } else if (key == "LIST") {
         const JsonValue* values = node.find("values");
         if (!values || !values->isArray()) {
            error(path, "missing required array member 'values' for list initializer");
            return init;
         }
         init.kind = InitKind::List;
         for (size_t i = 0; i < values->array.size(); ++i) {
            init.list.push_back(parseInitValue(values->array[i], path + ".values[" + std::to_string(i) + "]"));
         }
      } else if (key == "REPEAT") {
         init.kind = InitKind::Repeat;
         if (!getInt(node, "count", path, init.count)) {
            error(path, "missing required member 'count' for repeat initializer");
         }
         const JsonValue* value = node.find("value");
         if (!value) {
            error(path, "missing required member 'value' for repeat initializer");
         } else {
            init.repeatValue = std::make_shared<InitValue>(parseInitValue(*value, path + ".value"));
         }
      } else if (key == "SPARSE") {
         const JsonValue* entries = node.find("entries");
         if (!entries || !entries->isArray()) {
            error(path, "missing required array member 'entries' for sparse initializer");
            return init;
         }
         init.kind = InitKind::Sparse;
         for (size_t i = 0; i < entries->array.size(); ++i) {
            const JsonValue& e = entries->array[i];
            const std::string epath = path + ".entries[" + std::to_string(i) + "]";
            if (!e.isObject()) {
               error(epath, "expected an object");
               continue;
            }
            InitValue::SparseEntry entry;
            const JsonValue* range = e.find("range");
            if (range && range->isObject()) {
               getInt(*range, "lower", epath + ".range", entry.lower);
               getInt(*range, "upper", epath + ".range", entry.upper);
            } else {
               getInt(e, "lower", epath, entry.lower);
               getInt(e, "upper", epath, entry.upper);
            }
            const JsonValue* value = e.find("value");
            if (!value) {
               error(epath, "missing required member 'value' for sparse entry");
               continue;
            }
            entry.value = std::make_shared<InitValue>(parseInitValue(*value, epath + ".value"));
            init.entries.push_back(std::move(entry));
         }
         const JsonValue* dflt = node.find("default");
         if (dflt) {
            init.defaultValue = std::make_shared<InitValue>(parseInitValue(*dflt, path + ".default"));
         }
      } else if (key == "STRUCT") {
         const JsonValue* values = node.find("values");
         if (!values || !values->isArray()) {
            error(path, "missing required array member 'values' for struct initializer");
            return init;
         }
         init.kind = InitKind::Struct;
         for (size_t i = 0; i < values->array.size(); ++i) {
            const JsonValue& e = values->array[i];
            const std::string epath = path + ".values[" + std::to_string(i) + "]";
            if (!e.isObject()) {
               error(epath, "expected an object");
               continue;
            }
            InitValue::StructEntry entry;
            requireString(e, "member", epath, entry.member);
            const JsonValue* value = e.find("value");
            if (!value) {
               error(epath, "missing required member 'value' for struct entry");
               continue;
            }
            entry.value = std::make_shared<InitValue>(parseInitValue(*value, epath + ".value"));
            init.members.push_back(std::move(entry));
         }
      } else {
         error(path + ".kind", "unsupported initializer kind '" + kind + "'");
      }
      return init;
   }

   // Scalar only (JSON -> InitValue), used by constants' top-level 'value'.
   InitValue scalarValue(const JsonValue& value, const std::string& path)
   {
      InitValue init;
      if (value.isObject() || value.isArray()) {
         error(path + ".value", "scalar value expected, got an object/array");
         return init;
      }
      init.kind = InitKind::Scalar;
      init.scalar = value.asString();
      return init;
   }

   // ---- C++ bindings ----
   void parseSymbolBinding(const JsonValue& obj, const std::string& path, SymbolCppBinding& out)
   {
      std::string symbol;
      if (requireString(obj, "symbol", path, symbol)) {
         out.symbol = symbol;
      }
   }

   void parseFunctionBinding(const JsonValue& obj, const std::string& path, FunctionCppBinding& out)
   {
      out = FunctionCppBinding{};
      std::string kind;
      requireString(obj, "kind", path, kind);
      const std::string key = LibraryDescriptor::makeKey(kind);
      if (key == "STATICMETHOD" || key == "STATIC_METHOD") {
         out.kind = FunctionBindingKind::StaticMethod;
      } else if (key == "FREEFUNCTION" || key == "FREE_FUNCTION") {
         out.kind = FunctionBindingKind::FreeFunction;
      } else if (!kind.empty()) {
         error(path + ".kind", "unsupported function binding kind '" + kind + "'");
      }
      requireString(obj, "symbol", path, out.symbol);
      getString(obj, "owner", path, out.owner);
   }

   // ---- Parameters ----
   FunParam parseFunParam(const JsonValue& obj, const std::string& path)
   {
      FunParam param;
      requireString(obj, "name", path, param.name);
      const JsonValue* type = obj.find("type");
      if (!type || !type->isObject()) {
         error(path, "missing required member 'type'");
      } else {
         param.type = parseTypeRef(*type, path + ".type");
      }
      std::string direction;
      if (requireString(obj, "direction", path, direction)) {
         const std::string dkey = LibraryDescriptor::makeKey(direction);
         if (dkey == "IN") {
            param.direction = ParamDirection::In;
         } else if (dkey == "OUT") {
            param.direction = ParamDirection::Out;
         } else if (dkey == "IN_OUT") {
            param.direction = ParamDirection::InOut;
         } else {
            error(path + ".direction", "invalid parameter direction '" + direction + "' (expected IN, OUT or IN_OUT)");
         }
      }
      const JsonValue* init = obj.find("initValue");
      if (init) {
         param.initValue = parseInitValue(*init, path + ".initValue");
      }
      getString(obj, "documentation", path, param.documentation);
      getBool(obj, "inherited", path, param.inherited);
      getString(obj, "declaredIn", path, param.declaredIn);
      return param;
   }

   // ---- Top level ----
   void parseRoot()
   {
      if (!root_ || !root_->isObject()) {
         error("", "the library descriptor root must be a JSON object");
         return;
      }
      const JsonValue& root = *root_;

      if (!requireString(root, "$schemaVersion", "$schemaVersion", desc_.schemaVersion)) {
         return;
      }
      if (desc_.schemaVersion != kSchemaVersion10 && desc_.schemaVersion != kSchemaVersion11) {
         error("$schemaVersion",
               "unsupported schemaVersion '" + desc_.schemaVersion + "'; supported versions are '"
                   + std::string(kSchemaVersion10) + "' and '" + kSchemaVersion11 + "'");
      }

      requireString(root, "id", "id", desc_.id);
      requireString(root, "name", "name", desc_.name);
      std::string version;
      requireString(root, "version", "version", version);
      desc_.version = version;
      getString(root, "description", "description", desc_.description);

      const JsonValue* deps = root.find("dependencies");
      if (deps) {
         if (deps->isArray()) {
            for (size_t i = 0; i < deps->array.size(); ++i) {
               const JsonValue& d = deps->array[i];
               const std::string path = "dependencies[" + std::to_string(i) + "]";
               if (!d.isObject()) {
                  error(path, "expected an object");
                  continue;
               }
               Dependency dep;
               requireString(d, "id", path, dep.id);
               std::string constraint;
               if (requireString(d, "version", path, constraint)) {
                  dep.version = VersionConstraint::parse(constraint);
               }
               desc_.dependencies.push_back(std::move(dep));
            }
         } else {
            error("dependencies", "expected an array");
         }
      }

      const JsonValue* binding = root.find("cppBinding");
      if (binding) {
         if (!binding->isObject()) {
            error("cppBinding", "expected an object");
         } else {
            getString(*binding, "include", "cppBinding", desc_.cppBinding.include);
            getString(*binding, "namespace", "cppBinding", desc_.cppBinding.ns);
         }
      }

      parseConstants(root);
      parseEnums(root);
      parseTypes(root);
      parseGlobals(root);
      parseFunctions(root);
      parseFunctionBlocks(root);
   }

   void parseConstants(const JsonValue& root)
   {
      const JsonValue* list = root.find("constants");
      if (!list) {
         return;
      }
      if (!list->isArray()) {
         error("constants", "expected an array");
         return;
      }
      for (size_t i = 0; i < list->array.size(); ++i) {
         const JsonValue& o = list->array[i];
         const std::string path = "constants[" + std::to_string(i) + "]";
         if (!o.isObject()) {
            error(path, "expected an object");
            continue;
         }
         Constant c;
         requireString(o, "name", path, c.name);
         const JsonValue* type = o.find("type");
         if (!type || !type->isObject()) {
            error(path, "missing required member 'type'");
         } else {
            c.type = parseTypeRef(*type, path + ".type");
         }
         const JsonValue* value = o.find("value");
         if (!value) {
            error(path, "missing required member 'value'");
         } else {
            c.value = scalarValue(*value, path);
         }
         const JsonValue* cb = o.find("cppBinding");
         if (cb) {
            if (!cb->isObject()) {
               error(path + ".cppBinding", "expected an object");
            } else {
               c.hasCppBinding = true;
               parseSymbolBinding(*cb, path + ".cppBinding", c.cppBinding);
            }
         }
         getString(o, "documentation", path, c.documentation);
         desc_.constants.push_back(std::move(c));
      }
   }

   void parseEnums(const JsonValue& root)
   {
      const JsonValue* list = root.find("enums");
      if (!list) {
         return;
      }
      if (!list->isArray()) {
         error("enums", "expected an array");
         return;
      }
      for (size_t i = 0; i < list->array.size(); ++i) {
         const JsonValue& o = list->array[i];
         const std::string path = "enums[" + std::to_string(i) + "]";
         if (!o.isObject()) {
            error(path, "expected an object");
            continue;
         }
         EnumTypeDef e;
         requireString(o, "name", path, e.name);
         const JsonValue* bt = o.find("baseType");
         if (!bt || !bt->isObject()) {
            error(path, "missing required member 'baseType'");
         } else {
            e.baseType = parseTypeRef(*bt, path + ".baseType");
         }
         const JsonValue* cb = o.find("cppBinding");
         if (cb) {
            if (!cb->isObject()) {
               error(path + ".cppBinding", "expected an object");
            } else {
               e.hasCppBinding = true;
               parseSymbolBinding(*cb, path + ".cppBinding", e.cppBinding);
            }
         }
         const JsonValue* members = o.find("members");
         if (!members || !members->isArray()) {
            error(path, "missing required array member 'members'");
         } else {
            for (size_t m = 0; m < members->array.size(); ++m) {
               const JsonValue& mo = members->array[m];
               const std::string mpath = path + ".members[" + std::to_string(m) + "]";
               if (!mo.isObject()) {
                  error(mpath, "expected an object");
                  continue;
               }
               EnumMember member;
               requireString(mo, "name", mpath, member.name);
               if (!getInt(mo, "value", mpath, member.value)) {
                  error(mpath, "missing required member 'value'");
                  continue;
               }
               e.members.push_back(std::move(member));
            }
         }
         const JsonValue* init = o.find("initValue");
         if (init) {
            e.initValue = parseInitValue(*init, path + ".initValue");
         }
         getString(o, "documentation", path, e.documentation);
         desc_.enums.push_back(std::move(e));
      }
   }

   void parseTypes(const JsonValue& root)
   {
      const JsonValue* list = root.find("types");
      if (!list) {
         return;
      }
      if (!list->isArray()) {
         error("types", "expected an array");
         return;
      }
      for (size_t i = 0; i < list->array.size(); ++i) {
         const JsonValue& o = list->array[i];
         const std::string path = "types[" + std::to_string(i) + "]";
         if (!o.isObject()) {
            error(path, "expected an object");
            continue;
         }
         StructTypeDef t;
         requireString(o, "name", path, t.name);
         std::string kind;
         getString(o, "kind", path, kind);
         const std::string kkey = LibraryDescriptor::makeKey(kind);
         if (!kind.empty() && kkey != "STRUCT") {
            error(path + ".kind", "unsupported type kind '" + kind + "' (supported: \"struct\")");
         }
         const JsonValue* cb = o.find("cppBinding");
         if (cb) {
            if (!cb->isObject()) {
               error(path + ".cppBinding", "expected an object");
            } else {
               t.hasCppBinding = true;
               parseSymbolBinding(*cb, path + ".cppBinding", t.cppBinding);
            }
         }
         const JsonValue* fields = o.find("fields");
         if (!fields || !fields->isArray()) {
            error(path, "missing required array member 'fields'");
         } else {
            for (size_t f = 0; f < fields->array.size(); ++f) {
               const JsonValue& fo = fields->array[f];
               const std::string fpath = path + ".fields[" + std::to_string(f) + "]";
               if (!fo.isObject()) {
                  error(fpath, "expected an object");
                  continue;
               }
               StructField field;
               requireString(fo, "name", fpath, field.name);
               const JsonValue* type = fo.find("type");
               if (!type || !type->isObject()) {
                  error(fpath, "missing required member 'type'");
               } else {
                  field.type = parseTypeRef(*type, fpath + ".type");
               }
               const JsonValue* init = fo.find("initValue");
               if (init) {
                  field.initValue = parseInitValue(*init, fpath + ".initValue");
               }
               getString(fo, "documentation", fpath, field.documentation);
               t.fields.push_back(std::move(field));
            }
         }
         getString(o, "documentation", path, t.documentation);
         desc_.types.push_back(std::move(t));
      }
   }

   void parseGlobals(const JsonValue& root)
   {
      const JsonValue* list = root.find("globalVariables");
      if (!list) {
         return;
      }
      if (!list->isArray()) {
         error("globalVariables", "expected an array");
         return;
      }
      for (size_t i = 0; i < list->array.size(); ++i) {
         const JsonValue& o = list->array[i];
         const std::string path = "globalVariables[" + std::to_string(i) + "]";
         if (!o.isObject()) {
            error(path, "expected an object");
            continue;
         }
         GlobalVariable g;
         requireString(o, "name", path, g.name);
         const JsonValue* type = o.find("type");
         if (!type || !type->isObject()) {
            error(path, "missing required member 'type'");
         } else {
            g.type = parseTypeRef(*type, path + ".type");
         }
         requireString(o, "scope", path, g.scope);
         if (!g.scope.empty() && LibraryDescriptor::makeKey(g.scope) != "GLOBAL") {
            error(path + ".scope", "unsupported scope '" + g.scope + "' (supported: \"global\")");
         }
         getBool(o, "constant", path, g.constant);
         const JsonValue* cb = o.find("cppBinding");
         if (cb) {
            if (!cb->isObject()) {
               error(path + ".cppBinding", "expected an object");
            } else {
               g.hasCppBinding = true;
               parseSymbolBinding(*cb, path + ".cppBinding", g.cppBinding);
            }
         }
         const JsonValue* init = o.find("initValue");
         if (init) {
            g.initValue = parseInitValue(*init, path + ".initValue");
         }
         getString(o, "documentation", path, g.documentation);
         desc_.globalVariables.push_back(std::move(g));
      }
   }

   void parseFunctions(const JsonValue& root)
   {
      const JsonValue* list = root.find("functions");
      if (!list) {
         return;
      }
      if (!list->isArray()) {
         error("functions", "expected an array");
         return;
      }
      for (size_t i = 0; i < list->array.size(); ++i) {
         const JsonValue& o = list->array[i];
         const std::string path = "functions[" + std::to_string(i) + "]";
         if (!o.isObject()) {
            error(path, "expected an object");
            continue;
         }
          FunctionDef f;
          requireString(o, "name", path, f.name);
          const JsonValue* cb = o.find("cppBinding");
          if (cb) {
             if (!cb->isObject()) {
                error(path + ".cppBinding", "expected an object");
             } else {
                f.hasCppBinding = true;
                parseFunctionBinding(*cb, path + ".cppBinding", f.cppBinding);
             }
          }
         const JsonValue* rt = o.find("returnType");
         if (!rt || !rt->isObject()) {
            error(path, "missing required member 'returnType'");
         } else {
            f.returnType = parseTypeRef(*rt, path + ".returnType");
         }
         const JsonValue* params = o.find("parameters");
         if (params) {
            if (!params->isArray()) {
               error(path + ".parameters", "expected an array");
            } else {
               for (size_t p = 0; p < params->array.size(); ++p) {
                  const JsonValue& po = params->array[p];
                  const std::string ppath = path + ".parameters[" + std::to_string(p) + "]";
                  if (!po.isObject()) {
                     error(ppath, "expected an object");
                     continue;
                  }
                  f.parameters.push_back(parseFunParam(po, ppath));
               }
            }
         }
         getString(o, "documentation", path, f.documentation);
         desc_.functions.push_back(std::move(f));
      }
   }

   void parseFunctionBlocks(const JsonValue& root)
   {
      const JsonValue* list = root.find("functionBlocks");
      if (!list) {
         return;
      }
      if (!list->isArray()) {
         error("functionBlocks", "expected an array");
         return;
      }
      for (size_t i = 0; i < list->array.size(); ++i) {
         const JsonValue& o = list->array[i];
         const std::string path = "functionBlocks[" + std::to_string(i) + "]";
         if (!o.isObject()) {
            error(path, "expected an object");
            continue;
         }
          FunctionBlockDef fb;
          requireString(o, "name", path, fb.name);
          const JsonValue* cb = o.find("cppBinding");
          if (cb) {
             if (!cb->isObject()) {
                error(path + ".cppBinding", "expected an object");
             } else {
                fb.hasCppBinding = true;
                fb.cppBinding = FbCppBinding{};
                requireString(*cb, "instanceType", path + ".cppBinding", fb.cppBinding.instanceType);
                requireString(*cb, "call", path + ".cppBinding", fb.cppBinding.call);
             }
          }
         const JsonValue* params = o.find("parameters");
         if (params) {
            if (!params->isArray()) {
               error(path + ".parameters", "expected an array");
            } else {
               for (size_t p = 0; p < params->array.size(); ++p) {
                  const JsonValue& po = params->array[p];
                  const std::string ppath = path + ".parameters[" + std::to_string(p) + "]";
                  if (!po.isObject()) {
                     error(ppath, "expected an object");
                     continue;
                  }
                  fb.parameters.push_back(parseFunParam(po, ppath));
               }
            }
         }
         const JsonValue* baseType = o.find("baseType");
         if (baseType) {
            getString(o, "baseType", path, fb.baseType);
         }
         const JsonValue* ifaces = o.find("interfaces");
         if (ifaces) {
            if (!ifaces->isArray()) {
               error(path + ".interfaces", "expected an array");
            } else {
               for (size_t i = 0; i < ifaces->array.size(); ++i) {
                  std::string iface;
                  if (ifaces->array[i].isString()) {
                     iface = ifaces->array[i].text;
                  } else {
                     error(path + ".interfaces[" + std::to_string(i) + "]", "expected a string");
                  }
                  if (!iface.empty()) {
                     fb.interfaces.push_back(iface);
                  }
               }
            }
         }
         getBool(o, "isAbstract", path, fb.isAbstract);
         getBool(o, "isFinal", path, fb.isFinal);

         const JsonValue* members = o.find("members");
         if (members) {
            if (!members->isArray()) {
               error(path + ".members", "expected an array");
            } else {
               for (size_t m = 0; m < members->array.size(); ++m) {
                  const JsonValue& mo = members->array[m];
                  const std::string mpath = path + ".members[" + std::to_string(m) + "]";
                  if (!mo.isObject()) {
                     error(mpath, "expected an object");
                     continue;
                  }
                  FbMember member;
                  requireString(mo, "name", mpath, member.name);
                  const JsonValue* type = mo.find("type");
                  if (type) {
                     member.type = parseTypeRef(*type, mpath + ".type");
                  }
                  std::string storage;
                  if (getString(mo, "storage", mpath, storage)) {
                     if (storage == "VAR_TEMP") {
                        member.storage = FbMemberStorage::Temp;
                     } else if (storage == "VAR RETAIN") {
                        member.storage = FbMemberStorage::Retain;
                     } else if (storage == "VAR CONSTANT") {
                        member.storage = FbMemberStorage::Constant;
                     } else {
                        member.storage = FbMemberStorage::Var;
                     }
                  }
                  const JsonValue* init = mo.find("initValue");
                  if (init) {
                     member.initValue = parseInitValue(*init, mpath + ".initValue");
                  }
                  getString(mo, "documentation", mpath, member.documentation);
                  getBool(mo, "inherited", mpath, member.inherited);
                  getString(mo, "declaredIn", mpath, member.declaredIn);
                  fb.members.push_back(std::move(member));
               }
            }
         }

         const JsonValue* methods = o.find("methods");
         if (methods) {
            if (!methods->isArray()) {
               error(path + ".methods", "expected an array");
            } else {
               for (size_t m = 0; m < methods->array.size(); ++m) {
                  const JsonValue& mo = methods->array[m];
                  const std::string mpath = path + ".methods[" + std::to_string(m) + "]";
                  if (!mo.isObject()) {
                     error(mpath, "expected an object");
                     continue;
                  }
                  FbMethodDef method;
                  requireString(mo, "name", mpath, method.name);
                  const JsonValue* ret = mo.find("returnType");
                  if (ret) {
                     method.returnType = parseTypeRef(*ret, mpath + ".returnType");
                  }
                  const JsonValue* mparams = mo.find("parameters");
                  if (mparams) {
                     if (!mparams->isArray()) {
                        error(mpath + ".parameters", "expected an array");
                     } else {
                        for (size_t p = 0; p < mparams->array.size(); ++p) {
                           const JsonValue& po = mparams->array[p];
                           const std::string ppath = mpath + ".parameters[" + std::to_string(p) + "]";
                           if (!po.isObject()) {
                              error(ppath, "expected an object");
                              continue;
                           }
                           method.parameters.push_back(parseFunParam(po, ppath));
                        }
                     }
                  }
                  std::string visibility;
                  if (getString(mo, "visibility", mpath, visibility)) {
                     if (visibility == "private") {
                        method.visibility = FbMethodVisibility::Private;
                     } else if (visibility == "protected") {
                        method.visibility = FbMethodVisibility::Protected;
                     } else {
                        method.visibility = FbMethodVisibility::Public;
                     }
                  }
                  getBool(mo, "isAbstract", mpath, method.isAbstract);
                  getBool(mo, "isFinal", mpath, method.isFinal);
                  getBool(mo, "isOverride", mpath, method.isOverride);
                  getBool(mo, "inherited", mpath, method.inherited);
                  getString(mo, "declaredIn", mpath, method.declaredIn);
                  getString(mo, "documentation", mpath, method.documentation);
                  fb.methods.push_back(std::move(method));
               }
            }
         }

         getString(o, "documentation", path, fb.documentation);
         desc_.functionBlocks.push_back(std::move(fb));
      }
   }

   // ========================================================================
   // Phase 2: cross-entity validation
   // ========================================================================

   bool isEmptyName(const std::string& name) const { return name.empty(); }

   void validate()
   {
      validateIdentity();
      validateDependencies();
      validateDuplicates();
      validateTypeResolution();
      validateBindings();
      validateInitializers();
   }

   void validateIdentity()
   {
      if (isEmptyName(desc_.id)) {
         error("id", "library id must not be empty");
      }
      if (isEmptyName(desc_.name)) {
         error("name", "library name must not be empty");
      }
      if (isEmptyName(desc_.version)) {
         error("version", "missing version");
      } else {
         const Version v = Version::parse(desc_.version);
         if (!v.valid) {
            error("version", "invalid semantic version '" + desc_.version + "' (expected MAJOR.MINOR.PATCH, e.g. 1.0.0)");
         }
      }
   }

   void validateDependencies()
   {
      std::unordered_set<std::string> seen;
      for (size_t i = 0; i < desc_.dependencies.size(); ++i) {
         const Dependency& dep = desc_.dependencies[i];
         const std::string path = "dependencies[" + std::to_string(i) + "]";
         if (isEmptyName(dep.id)) {
            error(path + ".id", "dependency id must not be empty");
         }
         if (!dep.version.raw.empty() && !dep.version.valid) {
            error(path + ".version", "invalid version constraint '" + dep.version.raw + "'");
         }
         const std::string key = LibraryDescriptor::makeKey(dep.id);
         if (!seen.insert(key).second) {
            error(path + ".id", "duplicate dependency id '" + dep.id + "'");
         }
      }
   }

   void validateDuplicates()
   {
      // duplicates inside a single category
      checkDuplicateNames(desc_.constants, "constants", &Constant::name);
      checkDuplicateNames(desc_.enums, "enums", &EnumTypeDef::name);
      checkDuplicateNames(desc_.types, "types", &StructTypeDef::name);
      checkDuplicateNames(desc_.globalVariables, "globalVariables", &GlobalVariable::name);
      checkDuplicateNames(desc_.functions, "functions", &FunctionDef::name);
      checkDuplicateNames(desc_.functionBlocks, "functionBlocks", &FunctionBlockDef::name);

      // duplicate across the type/entity namespace (enums, structs, FBs, functions)
      {
         std::unordered_set<std::string> seen;
         checkInto(desc_.enums, "enums", &EnumTypeDef::name, seen);
         checkInto(desc_.types, "types", &StructTypeDef::name, seen);
         checkInto(desc_.functionBlocks, "functionBlocks", &FunctionBlockDef::name, seen);
         checkInto(desc_.functions, "functions", &FunctionDef::name, seen);
      }
      // duplicate across the global variable namespace (constants + globals)
      {
         std::unordered_set<std::string> seen;
         checkInto(desc_.constants, "constants", &Constant::name, seen);
         checkInto(desc_.globalVariables, "globalVariables", &GlobalVariable::name, seen);
      }

      // nested duplicates
      for (size_t i = 0; i < desc_.enums.size(); ++i) {
         checkDuplicateNames(desc_.enums[i].members, "enums[" + std::to_string(i) + "].members", &EnumMember::name);
      }
      for (size_t i = 0; i < desc_.types.size(); ++i) {
         checkDuplicateNames(desc_.types[i].fields, "types[" + std::to_string(i) + "].fields", &StructField::name);
      }
      for (size_t i = 0; i < desc_.functions.size(); ++i) {
         checkDuplicateNames(desc_.functions[i].parameters, "functions[" + std::to_string(i) + "].parameters", &FunParam::name);
      }
      for (size_t i = 0; i < desc_.functionBlocks.size(); ++i) {
         checkDuplicateNames(desc_.functionBlocks[i].parameters, "functionBlocks[" + std::to_string(i) + "].parameters", &FunParam::name);
      }
   }

   // Generic duplicate checker for a single list.
   template<typename T>
   void checkDuplicateNames(const std::vector<T>& items, const std::string& listPath, const std::string T::* nameMember)
   {
      std::unordered_set<std::string> seen;
      for (size_t i = 0; i < items.size(); ++i) {
         const std::string key = LibraryDescriptor::makeKey(items[i].*nameMember);
         if (!seen.insert(key).second) {
            error(listPath + "[" + std::to_string(i) + "].name", "duplicate name '" + items[i].*nameMember + "'");
         }
      }
   }

   // Generic cross-category duplicate checker.
   template<typename T>
   void checkInto(const std::vector<T>& items,
                  const std::string& listName,
                  const std::string T::* nameMember,
                  std::unordered_set<std::string>& seen)
   {
      for (size_t i = 0; i < items.size(); ++i) {
         const std::string key = LibraryDescriptor::makeKey(items[i].*nameMember);
         if (!seen.insert(key).second) {
            error(listName + "[" + std::to_string(i) + "].name",
                  "duplicate name '" + items[i].*nameMember
                     + "' clashes across entity categories; names must be unique in the global type/variable namespace");
         }
      }
   }

   void validateTypeResolution()
   {
      for (size_t i = 0; i < desc_.constants.size(); ++i) {
         validateTypeRefRecursive(desc_.constants[i].type, "constants[" + std::to_string(i) + "].type");
      }
      for (size_t i = 0; i < desc_.enums.size(); ++i) {
         const EnumTypeDef& e = desc_.enums[i];
         const std::string path = "enums[" + std::to_string(i) + "]";
         if (e.baseType.kind != TypeRefKind::Primitive) {
            error(path + ".baseType", "enum base type must be a primitive integer type (got '" + typeRefName(e.baseType) + "')");
         } else {
            const std::string key = LibraryDescriptor::makeKey(baseTypeName(e.baseType.primitive));
            static const char* const kIntegerBases[]
               = {"BOOL", "SINT", "INT", "DINT", "LINT", "USINT", "UINT", "UDINT", "ULINT", "BYTE", "WORD", "DWORD", "LWORD"};
            bool ok = false;
            for (const char* b : kIntegerBases) {
               if (key == b) {
                  ok = true;
                  break;
               }
            }
            if (!ok) {
               error(path + ".baseType",
                     "enum base type '" + baseTypeName(e.baseType.primitive) + "' is not valid (expected an integer or bitstring type)");
            }
         }
         validateTypeRefRecursive(e.baseType, path + ".baseType");
      }
      for (size_t i = 0; i < desc_.types.size(); ++i) {
         const StructTypeDef& t = desc_.types[i];
         for (size_t f = 0; f < t.fields.size(); ++f) {
            validateTypeRefRecursive(t.fields[f].type, "types[" + std::to_string(i) + "].fields[" + std::to_string(f) + "].type");
         }
      }
      for (size_t i = 0; i < desc_.globalVariables.size(); ++i) {
         validateTypeRefRecursive(desc_.globalVariables[i].type, "globalVariables[" + std::to_string(i) + "].type");
      }
      for (size_t i = 0; i < desc_.functions.size(); ++i) {
         const FunctionDef& f = desc_.functions[i];
         validateTypeRefRecursive(f.returnType, "functions[" + std::to_string(i) + "].returnType");
         for (size_t p = 0; p < f.parameters.size(); ++p) {
            validateTypeRefRecursive(f.parameters[p].type,
                                     "functions[" + std::to_string(i) + "].parameters[" + std::to_string(p) + "].type");
         }
      }
      for (size_t i = 0; i < desc_.functionBlocks.size(); ++i) {
         const FunctionBlockDef& fb = desc_.functionBlocks[i];
         for (size_t p = 0; p < fb.parameters.size(); ++p) {
            validateTypeRefRecursive(fb.parameters[p].type,
                                     "functionBlocks[" + std::to_string(i) + "].parameters[" + std::to_string(p) + "].type");
         }
      }
   }

   /**
     * @brief Validate one type reference and its children.
     * @details Named local references must resolve to a declared enum/struct;
     * named external references must point to a declared dependency (and must
     * not reference the library itself).
     */
   void validateTypeRefRecursive(const TypeRef& ref, const std::string& path)
   {
      switch (ref.kind) {
      case TypeRefKind::Primitive:
         break;
      case TypeRefKind::Named:
         if (!ref.library.empty()) {
            const std::string key = LibraryDescriptor::makeKey(ref.library);
            if (LibraryDescriptor::makeKey(desc_.id) == key) {
               error(path + ".library", "external reference cannot reference the library itself");
            } else if (desc_.findDependency(ref.library) == nullptr) {
               error(path + ".library", "external type '" + ref.name + "' references undeclared library '" + ref.library + "'");
            }
         } else if (!desc_.hasType(ref.name)) {
            error(path + ".name", "unknown named type '" + ref.name + "'");
         }
         break;
      case TypeRefKind::Array:
         if (ref.elementType) {
            validateTypeRefRecursive(*ref.elementType, path + ".elementType");
         }
         break;
      }
   }

   void validateBindings()
   {
      for (size_t i = 0; i < desc_.constants.size(); ++i) {
         validateSymbolBinding(desc_.constants[i].cppBinding,
                               desc_.constants[i].hasCppBinding,
                               "constants[" + std::to_string(i) + "].cppBinding");
      }
      for (size_t i = 0; i < desc_.enums.size(); ++i) {
         validateSymbolBinding(desc_.enums[i].cppBinding, desc_.enums[i].hasCppBinding, "enums[" + std::to_string(i) + "].cppBinding");
      }
      for (size_t i = 0; i < desc_.types.size(); ++i) {
         validateSymbolBinding(desc_.types[i].cppBinding, desc_.types[i].hasCppBinding, "types[" + std::to_string(i) + "].cppBinding");
      }
      for (size_t i = 0; i < desc_.globalVariables.size(); ++i) {
         validateSymbolBinding(desc_.globalVariables[i].cppBinding,
                               desc_.globalVariables[i].hasCppBinding,
                               "globalVariables[" + std::to_string(i) + "].cppBinding");
      }
      for (size_t i = 0; i < desc_.functions.size(); ++i) {
         const FunctionDef& f = desc_.functions[i];
         if (!f.hasCppBinding) {
            continue; // binding is optional (semantic-only descriptors)
         }
         const std::string path = "functions[" + std::to_string(i) + "].cppBinding";
         if (f.cppBinding.symbol.empty()) {
            error(path + ".symbol", "function binding requires a non-empty symbol");
         }
         if (f.cppBinding.kind == FunctionBindingKind::StaticMethod && f.cppBinding.owner.empty()) {
            error(path + ".owner", "staticMethod binding requires a non-empty owner");
         }
      }
      for (size_t i = 0; i < desc_.functionBlocks.size(); ++i) {
         const FunctionBlockDef& fb = desc_.functionBlocks[i];
         if (!fb.hasCppBinding) {
            continue; // binding is optional (semantic-only descriptors)
         }
         const std::string path = "functionBlocks[" + std::to_string(i) + "].cppBinding";
         if (fb.cppBinding.instanceType.empty()) {
            error(path + ".instanceType", "function block binding requires a non-empty instanceType");
         }
         if (fb.cppBinding.call.empty()) {
            error(path + ".call", "function block binding requires a non-empty call method");
         }
      }
   }

   void validateSymbolBinding(const SymbolCppBinding& binding, bool present, const std::string& path)
   {
      if (!present) {
         return; // binding is optional for this entity kind
      }
      if (binding.symbol.empty()) {
         error(path + ".symbol", "C++ symbol binding requires a non-empty symbol");
      }
   }

   void validateInitializers()
   {
      for (size_t i = 0; i < desc_.constants.size(); ++i) {
         const Constant& c = desc_.constants[i];
         if (c.value.kind != InitKind::None && c.value.kind != InitKind::Scalar) {
            error("constants[" + std::to_string(i) + "].value", "constant value must be a scalar");
         }
         validateInit(c.type, c.value, "constants[" + std::to_string(i) + "].value");
      }
      for (size_t i = 0; i < desc_.enums.size(); ++i) {
         const EnumTypeDef& e = desc_.enums[i];
         const InitValue& iv = e.initValue;
         const std::string path = "enums[" + std::to_string(i) + "].initValue";
         if (iv.kind == InitKind::None) {
            continue;
         }
         if (iv.kind == InitKind::Scalar) {
            bool found = false;
            for (const EnumMember& m : e.members) {
               if (LibraryDescriptor::makeKey(m.name) == LibraryDescriptor::makeKey(iv.scalar)) {
                  found = true;
                  break;
               }
            }
            if (!found) {
               error(path + ".value", "unknown enumerator '" + iv.scalar + "' for enum '" + e.name + "'");
            }
         } else if (iv.kind != InitKind::Default) {
            error(path, "enum initializer must be a scalar (enumerator) or default");
         }
      }
      for (size_t i = 0; i < desc_.types.size(); ++i) {
         const StructTypeDef& t = desc_.types[i];
         for (size_t f = 0; f < t.fields.size(); ++f) {
            validateInit(t.fields[f].type,
                         t.fields[f].initValue,
                         "types[" + std::to_string(i) + "].fields[" + std::to_string(f) + "].initValue");
         }
      }
      for (size_t i = 0; i < desc_.globalVariables.size(); ++i) {
         validateInit(desc_.globalVariables[i].type,
                      desc_.globalVariables[i].initValue,
                      "globalVariables[" + std::to_string(i) + "].initValue");
      }
      for (size_t i = 0; i < desc_.functions.size(); ++i) {
         const std::vector<FunParam>& params = desc_.functions[i].parameters;
         for (size_t p = 0; p < params.size(); ++p) {
            validateInit(params[p].type,
                         params[p].initValue,
                         "functions[" + std::to_string(i) + "].parameters[" + std::to_string(p) + "].initValue");
         }
      }
      for (size_t i = 0; i < desc_.functionBlocks.size(); ++i) {
         const std::vector<FunParam>& params = desc_.functionBlocks[i].parameters;
         for (size_t p = 0; p < params.size(); ++p) {
            validateInit(params[p].type,
                         params[p].initValue,
                         "functionBlocks[" + std::to_string(i) + "].parameters[" + std::to_string(p) + "].initValue");
         }
      }
   }

   /**
     * @brief Classify a type reference for structural initializer validation.
     */
   TargetType classify(const TypeRef& ref) const
   {
      TargetType t;
      switch (ref.kind) {
      case TypeRefKind::Primitive:
         t.cat = TargetCat::Primitive;
         t.base = ref.primitive;
         break;
      case TypeRefKind::Array:
         t.cat = TargetCat::Array;
         t.elem = ref.elementType;
         break;
      case TypeRefKind::Named:
         if (!ref.library.empty()) {
            t.cat = TargetCat::Opaque; // external: layout unknown
         } else if (const EnumTypeDef* en = desc_.findEnum(ref.name)) {
            t.cat = TargetCat::Enum;
            t.name = ref.name;
            t.en = en;
         } else if (const StructTypeDef* st = desc_.findStruct(ref.name)) {
            t.cat = TargetCat::Struct;
            t.name = ref.name;
            t.st = st;
         } else {
            t.cat = TargetCat::Opaque; // unresolved: reported elsewhere
         }
         break;
      }
      return t;
   }

   bool initPresent(const InitValue& init) const { return init.kind != InitKind::None; }

   void validateInit(const TypeRef& ref, const InitValue& init, const std::string& path)
   {
      if (!initPresent(init)) {
         return;
      }
      const TargetType target = classify(ref);
      switch (target.cat) {
      case TargetCat::Primitive:
         validateScalarInit(target, init, path);
         break;
      case TargetCat::Enum:
         if (init.kind == InitKind::Scalar) {
            const EnumTypeDef* en = target.en;
            bool found = false;
            for (const EnumMember& m : en->members) {
               if (LibraryDescriptor::makeKey(m.name) == LibraryDescriptor::makeKey(init.scalar)) {
                  found = true;
                  break;
               }
            }
            if (!found) {
               error(path + ".value", "unknown enumerator '" + init.scalar + "' for enum '" + target.name + "'");
            }
         } else if (init.kind != InitKind::Default && init.kind != InitKind::None) {
            error(path, "initializer of enum '" + target.name + "' must be a scalar or default");
         }
         break;
      case TargetCat::Struct:
         if (init.kind == InitKind::Struct) {
            const StructTypeDef* st = target.st;
            std::unordered_set<std::string> seen;
            for (const InitValue::StructEntry& entry : init.members) {
               const StructField* field = nullptr;
               for (const StructField& f : st->fields) {
                  if (LibraryDescriptor::makeKey(f.name) == LibraryDescriptor::makeKey(entry.member)) {
                     field = &f;
                     break;
                  }
               }
               const std::string epath = path + ".values";
               if (!field) {
                  error(epath, "unknown member '" + entry.member + "' in struct '" + target.name + "'");
                  continue;
               }
               if (!seen.insert(LibraryDescriptor::makeKey(entry.member)).second) {
                  error(epath, "duplicate member '" + entry.member + "' in struct initializer");
               }
               if (entry.value) {
                  validateInit(field->type, *entry.value, epath);
               }
            }
         } else if (init.kind != InitKind::Default && init.kind != InitKind::None) {
            error(path, "initializer of struct '" + target.name + "' must use kind 'struct' or 'default'");
         }
         break;
      case TargetCat::Array: {
         const TypeRef& elem = target.elem ? *target.elem : TypeRef{};
         const int capacity = ref.upperBound - ref.lowerBound + 1;
         switch (init.kind) {
         case InitKind::Default:
            break;
         case InitKind::List:
            if (capacity > 0 && static_cast<int>(init.list.size()) > capacity) {
               error(path + ".values",
                     "list initializer has " + std::to_string(init.list.size()) + " values but array capacity is "
                        + std::to_string(capacity));
            }
            for (const InitValue& item : init.list) {
               validateInit(elem, item, path + ".values");
            }
            break;
         case InitKind::Repeat:
            if (init.count < 0) {
               error(path + ".count", "repeat count must be non-negative");
            } else if (capacity > 0 && init.count > capacity) {
               error(path + ".count",
                     "repeat count " + std::to_string(init.count) + " exceeds array capacity " + std::to_string(capacity));
            }
            if (init.repeatValue) {
               validateInit(elem, *init.repeatValue, path + ".value");
            }
            break;
         case InitKind::Sparse:
            for (size_t i = 0; i < init.entries.size(); ++i) {
               const InitValue::SparseEntry& entry = init.entries[i];
               const std::string epath = path + ".entries[" + std::to_string(i) + "]";
               if (entry.lower > entry.upper) {
                  error(epath,
                        "sparse entry range lower (" + std::to_string(entry.lower) + ") must be <= upper (" + std::to_string(entry.upper)
                           + ")");
               }
               if (entry.lower < ref.lowerBound || entry.upper > ref.upperBound) {
                  error(epath,
                        "sparse entry range [" + std::to_string(entry.lower) + ".." + std::to_string(entry.upper)
                           + "] outside array bounds [" + std::to_string(ref.lowerBound) + ".." + std::to_string(ref.upperBound) + "]");
               }
               if (entry.value) {
                  validateInit(elem, *entry.value, epath + ".value");
               }
            }
            if (init.defaultValue) {
               validateInit(elem, *init.defaultValue, path + ".default");
            }
            break;
         default:
            error(path, "initializer of array type must use list, repeat, sparse or default");
            break;
         }
         break;
      }
      case TargetCat::Opaque:
         // External/unresolved target: only structural sanity is possible.
         break;
      }
   }

   void validateScalarInit(const TargetType& target, const InitValue& init, const std::string& path)
   {
      if (init.kind != InitKind::Scalar && init.kind != InitKind::Default && init.kind != InitKind::None) {
         error(path, "initializer of primitive type must be a scalar or default");
         return;
      }
      if (target.base == BaseType::BOOL && init.kind == InitKind::Scalar) {
         const std::string s = init.scalar;
         if (s != "true" && s != "false" && s != "0" && s != "1" && s != "TRUE" && s != "FALSE") {
            error(path + ".value", "invalid BOOL literal '" + s + "'");
         }
      }
   }

   // ---- small helpers duplicated from semantic-free utilities ----
   std::string baseTypeName(BaseType base) const
   {
      switch (base) {
      case BaseType::BOOL:
         return "BOOL";
      case BaseType::SINT:
         return "SINT";
      case BaseType::INT:
         return "INT";
      case BaseType::DINT:
         return "DINT";
      case BaseType::LINT:
         return "LINT";
      case BaseType::USINT:
         return "USINT";
      case BaseType::UINT:
         return "UINT";
      case BaseType::UDINT:
         return "UDINT";
      case BaseType::ULINT:
         return "ULINT";
      case BaseType::REAL:
         return "REAL";
      case BaseType::LREAL:
         return "LREAL";
      case BaseType::BYTE:
         return "BYTE";
      case BaseType::WORD:
         return "WORD";
      case BaseType::DWORD:
         return "DWORD";
      case BaseType::LWORD:
         return "LWORD";
      case BaseType::STRING:
         return "STRING";
      case BaseType::WSTRING:
         return "WSTRING";
      case BaseType::TIME:
         return "TIME";
      case BaseType::DATE:
         return "DATE";
      case BaseType::DT:
         return "DT";
      case BaseType::TOD:
         return "TOD";
      default:
         return "VOID";
      }
   }

   std::string typeRefName(const TypeRef& ref) const
   {
      switch (ref.kind) {
      case TypeRefKind::Primitive:
         return baseTypeName(ref.primitive);
      case TypeRefKind::Named:
         return ref.name;
      case TypeRefKind::Array:
         return "ARRAY";
      }
      return std::string();
   }
};

} // namespace

LibraryLoadResult LibraryLoader::fromJson(const JsonValue& root)
{
   Builder builder(root);
   return builder.build();
}

LibraryLoadResult LibraryLoader::fromString(const std::string& jsonText)
{
   JsonValue root;
   try {
      root = json::parse(jsonText);
   } catch (const json::JsonParseError& e) {
      LibraryLoadResult result;
      result.errors.push_back({"", std::string(e.what())});
      return result;
   }
   return fromJson(root);
}

LibraryLoadResult LibraryLoader::fromFile(const std::string& path)
{
   std::ifstream file(path);
   if (!file.is_open()) {
      LibraryLoadResult result;
      result.errors.push_back({"", "cannot open file '" + path + "'"});
      return result;
   }
   std::ostringstream buffer;
   buffer << file.rdbuf();
   return fromString(buffer.str());
}

} // namespace st2cpp::library