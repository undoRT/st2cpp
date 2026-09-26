/**
 * @file LibrarySerializer.cpp
 * @brief LibrarySerializer implementation
 * @author Salvatore Bamundo
 * @date 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#include "library/LibrarySerializer.h"
#include <cctype>
#include <cstdlib>

namespace st2cpp::library {

using json::JsonType;
using json::JsonValue;

namespace {

std::string baseTypeName(BaseType base); // forward declaration

void addString(JsonValue& obj, const std::string& key, const std::string& value)
{
   if (value.empty()) {
      return;
   }
   JsonValue v;
   v.type = JsonType::String;
   v.text = value;
   obj.members.emplace_back(key, std::move(v));
}

void addMember(JsonValue& obj, const std::string& key, JsonValue value)
{
   obj.members.emplace_back(key, std::move(value));
}

void addBool(JsonValue& obj, const std::string& key, bool value)
{
   if (!value) {
      return;
   }
   JsonValue v;
   v.type = JsonType::Bool;
   v.boolean = value;
   obj.members.emplace_back(key, std::move(v));
}

const char* fbMemberStorageName(FbMemberStorage storage)
{
   switch (storage) {
      case FbMemberStorage::Var: return "VAR";
      case FbMemberStorage::Temp: return "VAR_TEMP";
      case FbMemberStorage::Retain: return "VAR RETAIN";
      case FbMemberStorage::Constant: return "VAR CONSTANT";
   }
   return "VAR";
}

const char* fbMethodVisibilityName(FbMethodVisibility visibility)
{
   switch (visibility) {
      case FbMethodVisibility::Private: return "private";
      case FbMethodVisibility::Protected: return "protected";
      case FbMethodVisibility::Public: return "public";
   }
   return "public";
}

JsonValue makeString(const std::string& value)
{
   JsonValue v;
   v.type = JsonType::String;
   v.text = value;
   return v;
}

JsonValue makeNumber(int value)
{
   JsonValue v;
   v.type = JsonType::Number;
   v.number = static_cast<double>(value);
   v.numberRaw = std::to_string(value);
   return v;
}

JsonValue makeBool(bool value)
{
   JsonValue v;
   v.type = JsonType::Bool;
   v.boolean = value;
   return v;
}

/**
 * @brief Emit a scalar string as the most natural JSON scalar
 * (number for numeric-looking text, bool for true/false, string otherwise).
 */
JsonValue scalarToJson(const std::string& scalar)
{
   if (scalar == "true" || scalar == "TRUE") {
      return makeBool(true);
   }
   if (scalar == "false" || scalar == "FALSE") {
      return makeBool(false);
   }
   if (!scalar.empty()) {
      const char* start = scalar.c_str();
      char* end = nullptr;
      const double value = std::strtod(start, &end);
      if (end != start && end[0] == '\0') {
         JsonValue v;
         v.type = JsonType::Number;
         v.number = value;
         v.numberRaw = scalar;
         return v;
      }
   }
   return makeString(scalar);
}

JsonValue initToJson(const InitValue& init)
{
   if (init.kind == InitKind::None) {
      return JsonValue{}; // not emitted
   }
   JsonValue obj;
   obj.type = JsonType::Object;
   switch (init.kind) {
   case InitKind::None:
      break;
   case InitKind::Default:
      addString(obj, "kind", "default");
      break;
   case InitKind::Scalar:
      addString(obj, "kind", "scalar");
      addMember(obj, "value", scalarToJson(init.scalar));
      break;
   case InitKind::List: {
      addString(obj, "kind", "list");
      JsonValue values;
      values.type = JsonType::Array;
      for (const InitValue& item : init.list) {
         values.array.push_back(initToJson(item));
      }
      addMember(obj, "values", std::move(values));
      break;
   }
   case InitKind::Repeat:
      addString(obj, "kind", "repeat");
      addMember(obj, "count", makeNumber(init.count));
      if (init.repeatValue) {
         addMember(obj, "value", initToJson(*init.repeatValue));
      }
      break;
   case InitKind::Sparse: {
      addString(obj, "kind", "sparse");
      JsonValue entries;
      entries.type = JsonType::Array;
      for (const InitValue::SparseEntry& entry : init.entries) {
         JsonValue e;
         e.type = JsonType::Object;
         JsonValue range;
         range.type = JsonType::Object;
         addMember(range, "lower", makeNumber(entry.lower));
         addMember(range, "upper", makeNumber(entry.upper));
         addMember(e, "range", std::move(range));
         if (entry.value) {
            addMember(e, "value", initToJson(*entry.value));
         }
         entries.array.push_back(std::move(e));
      }
      addMember(obj, "entries", std::move(entries));
      if (init.defaultValue) {
         addMember(obj, "default", initToJson(*init.defaultValue));
      }
      break;
   }
   case InitKind::Struct: {
      addString(obj, "kind", "struct");
      JsonValue values;
      values.type = JsonType::Array;
      for (const InitValue::StructEntry& entry : init.members) {
         JsonValue e;
         e.type = JsonType::Object;
         addString(e, "member", entry.member);
         if (entry.value) {
            addMember(e, "value", initToJson(*entry.value));
         }
         values.array.push_back(std::move(e));
      }
      addMember(obj, "values", std::move(values));
      break;
   }
   }
   return obj;
}

JsonValue typeRefToJson(const TypeRef& ref)
{
   JsonValue obj;
   obj.type = JsonType::Object;
   switch (ref.kind) {
   case TypeRefKind::Primitive:
      addString(obj, "kind", "primitive");
      addString(obj, "name", baseTypeName(ref.primitive));
      break;
   case TypeRefKind::Named:
      addString(obj, "kind", "named");
      addString(obj, "name", ref.name);
      addString(obj, "library", ref.library);
      break;
   case TypeRefKind::Array:
      addString(obj, "kind", "array");
      addMember(obj, "lowerBound", makeNumber(ref.lowerBound));
      addMember(obj, "upperBound", makeNumber(ref.upperBound));
      if (ref.elementType) {
         addMember(obj, "elementType", typeRefToJson(*ref.elementType));
      }
      break;
   }
   return obj;
}

std::string baseTypeName(BaseType base)
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

std::string directionToJson(ParamDirection direction)
{
   switch (direction) {
   case ParamDirection::In:
      return "IN";
   case ParamDirection::Out:
      return "OUT";
   case ParamDirection::InOut:
      return "IN_OUT";
   }
   return "IN";
}

void addSymbolBinding(JsonValue& obj, const SymbolCppBinding& binding)
{
   if (binding.symbol.empty()) {
      return;
   }
   JsonValue cb;
   cb.type = JsonType::Object;
   addString(cb, "symbol", binding.symbol);
   addMember(obj, "cppBinding", std::move(cb));
}

JsonValue paramToJson(const FunParam& p)
{
   JsonValue obj;
   obj.type = JsonType::Object;
   addString(obj, "name", p.name);
   addMember(obj, "type", typeRefToJson(p.type));
   addString(obj, "direction", directionToJson(p.direction));
   if (p.initValue.kind != InitKind::None) {
      addMember(obj, "initValue", initToJson(p.initValue));
   }
   addString(obj, "documentation", p.documentation);
   addBool(obj, "inherited", p.inherited);
   addString(obj, "declaredIn", p.declaredIn);
   return obj;
}

} // namespace

JsonValue LibrarySerializer::toJsonValue(const LibraryDescriptor& desc)
{
   JsonValue root;
   root.type = JsonType::Object;
   addString(root, "$schemaVersion", desc.schemaVersion);
   addString(root, "id", desc.id);
   addString(root, "name", desc.name);
   addString(root, "version", desc.version);
   addString(root, "description", desc.description);

   if (!desc.dependencies.empty()) {
      JsonValue deps;
      deps.type = JsonType::Array;
      for (const Dependency& dep : desc.dependencies) {
         JsonValue d;
         d.type = JsonType::Object;
         addString(d, "id", dep.id);
         addString(d, "version", dep.version.raw);
         deps.array.push_back(std::move(d));
      }
      addMember(root, "dependencies", std::move(deps));
   }

   if (!desc.cppBinding.include.empty() || !desc.cppBinding.ns.empty()) {
      JsonValue binding;
      binding.type = JsonType::Object;
      addString(binding, "include", desc.cppBinding.include);
      addString(binding, "namespace", desc.cppBinding.ns);
      addMember(root, "cppBinding", std::move(binding));
   }

   if (!desc.constants.empty()) {
      JsonValue items;
      items.type = JsonType::Array;
      for (const Constant& c : desc.constants) {
         JsonValue o;
         o.type = JsonType::Object;
         addString(o, "name", c.name);
         addMember(o, "type", typeRefToJson(c.type));
         if (c.value.kind == InitKind::Scalar) {
            addMember(o, "value", scalarToJson(c.value.scalar));
         } else if (c.value.kind != InitKind::None) {
            addMember(o, "value", initToJson(c.value));
         }
         addSymbolBinding(o, c.cppBinding);
         addString(o, "documentation", c.documentation);
         items.array.push_back(std::move(o));
      }
      addMember(root, "constants", std::move(items));
   }

   if (!desc.enums.empty()) {
      JsonValue items;
      items.type = JsonType::Array;
      for (const EnumTypeDef& e : desc.enums) {
         JsonValue o;
         o.type = JsonType::Object;
         addString(o, "name", e.name);
         addSymbolBinding(o, e.cppBinding);
         addMember(o, "baseType", typeRefToJson(e.baseType));
         JsonValue members;
         members.type = JsonType::Array;
         for (const EnumMember& m : e.members) {
            JsonValue mo;
            mo.type = JsonType::Object;
            addString(mo, "name", m.name);
            addMember(mo, "value", makeNumber(m.value));
            members.array.push_back(std::move(mo));
         }
         addMember(o, "members", std::move(members));
         if (e.initValue.kind != InitKind::None) {
            addMember(o, "initValue", initToJson(e.initValue));
         }
         addString(o, "documentation", e.documentation);
         items.array.push_back(std::move(o));
      }
      addMember(root, "enums", std::move(items));
   }

   if (!desc.types.empty()) {
      JsonValue items;
      items.type = JsonType::Array;
      for (const StructTypeDef& t : desc.types) {
         JsonValue o;
         o.type = JsonType::Object;
         addString(o, "name", t.name);
         addString(o, "kind", "struct");
         addSymbolBinding(o, t.cppBinding);
         JsonValue fields;
         fields.type = JsonType::Array;
         for (const StructField& f : t.fields) {
            JsonValue fo;
            fo.type = JsonType::Object;
            addString(fo, "name", f.name);
            addMember(fo, "type", typeRefToJson(f.type));
            if (f.initValue.kind != InitKind::None) {
               addMember(fo, "initValue", initToJson(f.initValue));
            }
            addString(fo, "documentation", f.documentation);
            fields.array.push_back(std::move(fo));
         }
         addMember(o, "fields", std::move(fields));
         addString(o, "documentation", t.documentation);
         items.array.push_back(std::move(o));
      }
      addMember(root, "types", std::move(items));
   }

   if (!desc.globalVariables.empty()) {
      JsonValue items;
      items.type = JsonType::Array;
      for (const GlobalVariable& g : desc.globalVariables) {
         JsonValue o;
         o.type = JsonType::Object;
         addString(o, "name", g.name);
         addMember(o, "type", typeRefToJson(g.type));
         addString(o, "scope", "global");
         addMember(o, "constant", makeBool(g.constant));
         addSymbolBinding(o, g.cppBinding);
         if (g.initValue.kind != InitKind::None) {
            addMember(o, "initValue", initToJson(g.initValue));
         }
         addString(o, "documentation", g.documentation);
         items.array.push_back(std::move(o));
      }
      addMember(root, "globalVariables", std::move(items));
   }

   if (!desc.functions.empty()) {
      JsonValue items;
      items.type = JsonType::Array;
      for (const FunctionDef& f : desc.functions) {
         JsonValue o;
         o.type = JsonType::Object;
          addString(o, "name", f.name);
          if (f.hasCppBinding) {
             JsonValue cb;
             cb.type = JsonType::Object;
             addString(cb, "symbol", f.cppBinding.symbol);
             addString(cb, "kind", f.cppBinding.kind == FunctionBindingKind::StaticMethod ? "staticMethod" : "freeFunction");
             if (f.cppBinding.kind == FunctionBindingKind::StaticMethod) {
                addString(cb, "owner", f.cppBinding.owner);
             }
             addMember(o, "cppBinding", std::move(cb));
          }
         addMember(o, "returnType", typeRefToJson(f.returnType));
         JsonValue params;
         params.type = JsonType::Array;
         for (const FunParam& p : f.parameters) {
            params.array.push_back(paramToJson(p));
         }
         addMember(o, "parameters", std::move(params));
         addString(o, "documentation", f.documentation);
         items.array.push_back(std::move(o));
      }
      addMember(root, "functions", std::move(items));
   }

   if (!desc.functionBlocks.empty()) {
      JsonValue items;
      items.type = JsonType::Array;
      for (const FunctionBlockDef& fb : desc.functionBlocks) {
         JsonValue o;
         o.type = JsonType::Object;
          addString(o, "name", fb.name);
          if (fb.hasCppBinding) {
             JsonValue cb;
             cb.type = JsonType::Object;
             addString(cb, "instanceType", fb.cppBinding.instanceType);
             addString(cb, "call", fb.cppBinding.call);
             addMember(o, "cppBinding", std::move(cb));
          }
         JsonValue params;
         params.type = JsonType::Array;
         for (const FunParam& p : fb.parameters) {
            params.array.push_back(paramToJson(p));
         }
         addMember(o, "parameters", std::move(params));

         // Inheritance: referenced by name, resolved against the same
         // descriptor by the consumer.
         addString(o, "baseType", fb.baseType);
         if (!fb.interfaces.empty()) {
            JsonValue ifaces;
            ifaces.type = JsonType::Array;
            for (const std::string& iface : fb.interfaces) {
               ifaces.array.push_back(makeString(iface));
            }
            addMember(o, "interfaces", std::move(ifaces));
         }
         addBool(o, "isAbstract", fb.isAbstract);
         addBool(o, "isFinal", fb.isFinal);

         // Internal state. Members inherited from a base block are part of the
         // list, flagged, so that laying out an instance needs no base walk.
         if (!fb.members.empty()) {
            JsonValue members;
            members.type = JsonType::Array;
            for (const FbMember& m : fb.members) {
               JsonValue mo;
               mo.type = JsonType::Object;
               addString(mo, "name", m.name);
               addMember(mo, "type", typeRefToJson(m.type));
               addString(mo, "storage", fbMemberStorageName(m.storage));
               if (m.initValue.kind != InitKind::None) {
                  addMember(mo, "initValue", initToJson(m.initValue));
               }
               addString(mo, "documentation", m.documentation);
               addBool(mo, "inherited", m.inherited);
               addString(mo, "declaredIn", m.declaredIn);
               members.array.push_back(std::move(mo));
            }
            addMember(o, "members", std::move(members));
         }

         if (!fb.methods.empty()) {
            JsonValue methods;
            methods.type = JsonType::Array;
            for (const FbMethodDef& m : fb.methods) {
               JsonValue mo;
               mo.type = JsonType::Object;
               addString(mo, "name", m.name);
               addMember(mo, "returnType", typeRefToJson(m.returnType));
               JsonValue mparams;
               mparams.type = JsonType::Array;
               for (const FunParam& p : m.parameters) {
                  mparams.array.push_back(paramToJson(p));
               }
               addMember(mo, "parameters", std::move(mparams));
               addString(mo, "visibility", fbMethodVisibilityName(m.visibility));
               addBool(mo, "isAbstract", m.isAbstract);
               addBool(mo, "isFinal", m.isFinal);
               addBool(mo, "isOverride", m.isOverride);
               addBool(mo, "inherited", m.inherited);
               addString(mo, "declaredIn", m.declaredIn);
               addString(mo, "documentation", m.documentation);
               methods.array.push_back(std::move(mo));
            }
            addMember(o, "methods", std::move(methods));
         }

         addString(o, "documentation", fb.documentation);
         items.array.push_back(std::move(o));
      }
      addMember(root, "functionBlocks", std::move(items));
   }

   return root;
}

std::string LibrarySerializer::toJson(const LibraryDescriptor& desc, int indent)
{
   return json::dump(toJsonValue(desc), indent);
}

} // namespace st2cpp::library