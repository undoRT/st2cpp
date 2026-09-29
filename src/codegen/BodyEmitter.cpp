/**
 * @file BodyEmitter.cpp
 * @brief Expression and statement emission
 * @author Salvatore Bamundo
 * @date 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#include "codegen/BodyEmitter.h"
#include "semantic/IecTime.h"
#include <algorithm>
#include <queue>

namespace st2cpp::codegen {

static std::string extractArrayElementType(const std::string& arrayType)
{
   const std::string prefix = "STArray<";
   if (arrayType.rfind(prefix, 0) != 0) {
      return arrayType;
   }
   size_t start = prefix.length();
   size_t comma = arrayType.find(',', start);
   if (comma == std::string::npos) {
      return arrayType;
   }
   std::string elem = arrayType.substr(start, comma - start);
   // Trim spaces
   elem.erase(0, elem.find_first_not_of(" \t"));
   elem.erase(elem.find_last_not_of(" \t") + 1);
   return elem;
}

static int getBinaryPrecFromOp(const std::string& op)
{
   if (op == "||" || op == "OR") {
      return 1;
   }
   if (op == "&&" || op == "AND") {
      return 2;
   }
   if (op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=") {
      return 3;
   }
   if (op == "+" || op == "-") {
      return 4;
   }
   if (op == "*" || op == "/" || op == "%" || op == "MOD") {
      return 5;
   }
   if (op == "**") {
      return 6;
   }
   return 0;
}

void BodyEmitter::genStmt(const Stmt& stmt)
{
   std::visit(
      [&](const auto& s) {
         using T = std::decay_t<decltype(s)>;

         if constexpr (std::is_same_v<T, AssignStmt>) {
            std::string chainValue;
            if (!s.additionalTargets.empty()) {
               chainValue = "_st2cpp_chain_" + std::to_string(m_ctx.m_scope.getNextTempCounter("chain_assignment"));
               m_ctx.m_src << m_ctx.ind() << "auto " << chainValue << " = " << genExpr(*s.rhs) << ";\n";
            }

            auto emitAssignment = [&](const std::shared_ptr<Expr>& lhsExpr) {
               if (!lhsExpr) {
                  return;
               }

               std::string lhsStr = genExpr(*lhsExpr);
               std::string rhsStr = chainValue.empty() ? genExpr(*s.rhs) : chainValue;

               std::string varName;
               if (lhsStr.rfind("getPi_", 0) == 0 && lhsStr.length() > 6 && lhsStr.back() == ')') {
                  varName = lhsStr.substr(6, lhsStr.length() - 8);
                  auto atOpt = m_ctx.m_scope.lookupATAddress(varName);
                  if (atOpt) {
                     m_ctx.m_src << m_ctx.ind() << "setPi_" << varName << "(" << rhsStr << ");\n";
                     return;
                  }
               }

               if (auto* addr = std::get_if<AddressExpr>(&lhsExpr->node)) {
                  std::string writeAccess = generateAddressWrite(*addr, rhsStr);
                  m_ctx.m_src << m_ctx.ind() << writeAccess << ";\n";
                  return;
               }

               if (lhsStr == m_ctx.m_currentFunctionName) {
                  lhsStr = m_ctx.m_currentFunctionName + "_ret";
               }
               rhsStr = m_ctx.applySemanticAssignmentCast(*lhsExpr, *s.rhs, rhsStr);
               m_ctx.m_src << m_ctx.ind() << lhsStr << " = " << rhsStr << ";\n";
            };

            emitAssignment(s.lhs);
            for (const auto& target : s.additionalTargets) {
               emitAssignment(target);
            }
         } else if constexpr (std::is_same_v<T, ExprStmt>) {
            // Expression statement (often a function call)
            if (auto* call = std::get_if<CallExpr>(&s.expr->node)) {
               std::string calleeName = genExpr(*call->callee);

               // Determine the type of the callee (for FB instance resolution)
               std::string calleeType;
               auto typeOpt = m_ctx.m_scope.lookupVariable(calleeName);
               if (typeOpt) {
                  calleeType = *typeOpt;
               } else {
                  // Check if it's an array access (e.g., PROCESSORS[1])
                  std::string baseName = m_ctx.getBaseFBName(calleeName);
                  auto baseOpt = m_ctx.m_scope.lookupVariable(baseName);
                  if (baseOpt) {
                     calleeType = *baseOpt;
                  } else {
                     calleeType = calleeName;
                  }
               }

               // If calleeType is an STArray, extract the element type (the FB type)
               std::string elementType = extractArrayElementType(calleeType);

               // Look up function signature using the element type
               std::string calleeKey = m_ctx.normalizeIdent(elementType);
               auto sigIt = m_ctx.m_signatures.find(calleeKey);
               if (sigIt == m_ctx.m_signatures.end() && !elementType.empty()) {
                  std::string upperName = elementType;
                  upperName[0] = std::toupper(upperName[0]);
                  sigIt = m_ctx.m_signatures.find(upperName);
               }

               bool isFunctionBlock = (sigIt != m_ctx.m_signatures.end() && sigIt->second.returnType.base == BaseType::VOID);

               // Check if this is a method call (contains dot) vs direct FB call
               bool isMethodCall = (std::get_if<MemberExpr>(&call->callee->node) != nullptr);

               // External-library FB invocation: recover the descriptor binding
               // (cppBinding.call). Project-local FBs return isFb=false so the
               // legacy binary path (set_IN/get_Q/plain "callee()") is preserved.
               ExternalFbCallInfo extFb;
               if (!isFunctionBlock && !isMethodCall) {
                  extFb = m_ctx.semanticFbCallInfo(*call);
                  if (extFb.isFb) {
                     isFunctionBlock = true;
                  }
               }

               // CASE 1: Direct Function Block call (e.g., myFB(10, false))
               if (isFunctionBlock && !call->args.empty() && !isMethodCall) {
                  // Get the base FB name (without array indices)
                  std::string baseFBName = m_ctx.getBaseFBName(calleeName);

                  // FB signature: canonical from semantics when the external FB
                  // has no collected project signature, else the collected one.
                  const FunctionSignature* fbSig = (sigIt != m_ctx.m_signatures.end()) ? &sigIt->second : nullptr;
                  std::optional<FunctionSignature> extFbSig;
                  if (fbSig == nullptr && extFb.isFb) {
                     extFbSig = m_ctx.semanticSignatureForCall(*call);
                     if (extFbSig.has_value()) {
                        fbSig = &*extFbSig;
                     }
                  }

                  // Handle positional arguments (inputs) -> setters
                  if (!call->args.empty() && !call->args[0].named) {
                     size_t idx = 0;
                     if (fbSig != nullptr) {
                        for (const auto& param : fbSig->parameters) {
                           if (param.isInput && idx < call->args.size()) {
                              if (call->args[idx].name != "") {
                                 std::ostringstream oss;
                                 oss << "Error at line " << call->args[idx].line << ":" << call->args[idx].col
                                     << ": Mixed reference and positional parameters in call to function block '" << calleeName << "'";
                                 throw std::runtime_error(oss.str());
                              }
                              std::string value = genExpr(*call->args[idx].value);
                              // Use calleeName (includes array index) for the actual call
                              m_ctx.m_src << m_ctx.ind() << calleeName << ".set_" << m_ctx.normalizeIdent(param.name) << "(" << value
                                          << ");\n";
                              idx++;
                           }
                        }
                     }
                  }
                  // Handle named arguments
                  else {
                     for (const auto& arg : call->args) {
                        std::string value = genExpr(*arg.value);

                        if (!arg.isOutput) {
                           if (arg.name == "") {
                              std::ostringstream oss;
                              oss << "Error at line " << arg.line << ":" << arg.col
                                  << ": Mixed reference and positional parameters in call to function block '" << calleeName << "'";
                              throw std::runtime_error(oss.str());
                           }
                           m_ctx.m_src << m_ctx.ind() << calleeName << ".set_" << m_ctx.normalizeIdent(arg.name) << "(" << value
                                       << ");\n";
                        }
                     }
                  }

                  // Execute the FB: external FBs invoke the descriptor step
                  // (cppBinding.call, e.g. "timer.process()"); local FBs stay binary.
                  if (extFb.isFb && !extFb.step.empty()) {
                     m_ctx.m_src << m_ctx.ind() << calleeName << "." << extFb.step << "();\n";
                  } else {
                     m_ctx.m_src << m_ctx.ind() << calleeName << "();\n";
                  }

                  // Handle output bindings -> getters
                  if (!call->args.empty() && !call->args[0].named) {
                     size_t idx = 0;
                     if (fbSig != nullptr) {
                        for (const auto& param : fbSig->parameters) {
                           if (!param.isInput && idx < call->args.size()) {
                              std::ostringstream oss;
                              oss << "Error at line " << call->args[idx].line << ":" << call->args[idx].col
                                  << ": Called VAR_OUTPUT or VAR_IN_OUT parameter without naming it in call to function block '"
                                  << calleeName << "'";
                              throw std::runtime_error(oss.str());
                           } else if (param.isInput && idx < call->args.size()) {
                              idx++;
                           }
                        }
                     }
                  } else {
                     for (const auto& arg : call->args) {
                        std::string value = genExpr(*arg.value);
                        if (arg.isOutput) {
                           m_ctx.m_src << m_ctx.ind() << value << " = " << calleeName << ".get_" << m_ctx.normalizeIdent(arg.name)
                                       << "();\n";
                        }
                     }
                  }
                  return;
               }

               // CASE 1b: external FB invocation without arguments (no setters)
               if (extFb.isFb && !isMethodCall) {
                  if (extFb.step.empty()) {
                     m_ctx.m_src << m_ctx.ind() << calleeName << "();\n";
                  } else {
                     m_ctx.m_src << m_ctx.ind() << calleeName << "." << extFb.step << "();\n";
                  }
                  return;
               }

               // CASE 2: Function or Method call
               FunctionSignature* activeSig = nullptr;
               std::string activeCalleeName = calleeName;

               // Try global function signature
               if (sigIt != m_ctx.m_signatures.end()) {
                  activeSig = &sigIt->second;
               } else {
                  size_t dotPos = calleeName.find('.');
                  if (dotPos != std::string::npos) {
                     std::string instanceName = calleeName.substr(0, dotPos);
                     std::string methodName = calleeName.substr(dotPos + 1);
                     auto varIt2 = m_ctx.m_scope.lookupVariable(instanceName);
                     if (varIt2) {
                        std::string fbType = *varIt2;
                        std::string methodKey = fbType + "::" + methodName;
                        auto methodIt = m_ctx.m_methodSignatures.find(methodKey);
                        if (methodIt != m_ctx.m_methodSignatures.end()) {
                           activeSig = &methodIt->second;
                           activeCalleeName = calleeName;
                        }
                     }
                  }
               }

               // External-library FUNCTION: replace the callee with its C++
               // binding (freeFunction verbatim / owner::symbol for static
               // methods). Local functions and FBs are left untouched.
               if (!isFunctionBlock && !isMethodCall) {
                  const std::string bound = m_ctx.semanticCallTargetName(*call);
                  if (!bound.empty()) {
                     activeCalleeName = bound;
                  }
               }

               bool hasArguments = !call->args.empty();

               // Enter if we have a signature OR there are arguments to process
               // For method calls (isMethodCall == true), we enter even if isFunctionBlock is true
               if ((activeSig != nullptr || hasArguments) && (!isFunctionBlock || isMethodCall)) {
                  if (activeSig != nullptr) {
                     std::unordered_map<std::string, std::string> providedArgs;
                     std::vector<std::string> tempVars;

                     // Determine call style: positional vs named (only if there are args)
                     bool isPositional = hasArguments && !call->args[0].named;

                     if (isPositional) {
                        size_t idx = 0;
                        for (const auto& param : activeSig->parameters) {
                           if (idx < call->args.size()) {
                              std::string value = genExpr(*call->args[idx].value);
                              std::string paramKey = m_ctx.normalizeIdent(param.name);
                              providedArgs[paramKey] = value;
                           }
                           idx++;
                        }
                     } else if (hasArguments) {
                        for (const auto& arg : call->args) {
                           std::string value = genExpr(*arg.value);
                           std::string normalizedName = m_ctx.normalizeIdent(arg.name);
                           providedArgs[normalizedName] = value;
                        }
                     }
                     // else: no arguments - providedArgs empty

                     // Build argument list in signature order
                     std::string callArgs;
                     bool first = true;
                     for (const auto& param : activeSig->parameters) {
                        if (!first) {
                           callArgs += ", ";
                        }
                        first = false;

                        std::string paramKey = m_ctx.normalizeIdent(param.name);
                        auto it = providedArgs.find(paramKey);

                        if (it != providedArgs.end()) {
                           callArgs += it->second;
                        } else if (param.isInput && param.defaultValue) {
                           // Input parameter with default value
                           callArgs += genExpr(*param.defaultValue);
                        } else if (param.isInput && !param.defaultValue) {
                           // Input parameter WITHOUT default value - use {} (value-initialization)
                           callArgs += "{}";
                        } else if (param.isOutputVar) {
                           // OUTPUT parameter NOT provided - create a temporary variable with unique name
                           int counter = m_ctx.m_scope.getNextTempCounter(param.name);
                           std::string tempVar = "__temp_" + m_ctx.normalizeIdent(param.name) + "_" + std::to_string(counter);
                           tempVars.push_back(tempVar);
                           std::string type;
                           if (!param.type.arrayDims.empty()) {
                              type = m_ctx.getArrayType(m_ctx.normalizeType(m_ctx.getBaseTypeName(param.type)), param.type);
                           } else {
                              type = m_ctx.normalizeType(m_ctx.getBaseTypeName(param.type));
                           }
                           std::string initValue;
                           if (param.defaultValue) {
                              initValue = " = " + genExpr(*param.defaultValue);
                           } else {
                              initValue = "{}";
                           }
                           m_ctx.m_src << m_ctx.ind() << type << " " << tempVar << initValue << ";\n";
                           callArgs += tempVar;
                        } else if (!param.isInput && !param.isOutputVar) {
                           // IN_OUT parameter - REQUIRED but not provided!
                           uint32_t line = call->args.empty() ? 0 : call->args[0].line;
                           uint32_t col = call->args.empty() ? 0 : call->args[0].col;
                           std::ostringstream oss;
                           oss << "Error at line " << line << ":" << col << ": Missing required IN_OUT parameter '" << param.name
                               << "' in call to '" << activeCalleeName << "'";
                           throw std::runtime_error(oss.str());
                        }
                     }

                     m_ctx.m_src << m_ctx.ind() << activeCalleeName << "(" << callArgs << ");\n";
                     return;
                  } else if (hasArguments) {
                     // No signature but there are arguments - fallback to positional call
                     std::string callArgs;
                     bool first = true;
                     for (const auto& arg : call->args) {
                        if (!first) {
                           callArgs += ", ";
                        }
                        first = false;
                        callArgs += genExpr(*arg.value);
                     }
                     m_ctx.m_src << m_ctx.ind() << activeCalleeName << "(" << callArgs << ");\n";
                     return;
                  }
               }
            }

            // Fallback: normal expression statement
            m_ctx.m_src << m_ctx.ind() << genExpr(*s.expr) << ";\n";
         } else if constexpr (std::is_same_v<T, ReturnStmt>) {
            // If the current function/method has a return type (non-void), return the _ret variable
            if (!m_ctx.m_currentFunctionReturnType.empty() && m_ctx.m_currentFunctionReturnType != "void") {
               m_ctx.m_src << m_ctx.ind() << "return " << m_ctx.m_currentFunctionName << "_ret;\n";
            } else {
               m_ctx.m_src << m_ctx.ind() << "return;\n";
            }
         } else if constexpr (std::is_same_v<T, ExitStmt>) {
            m_ctx.m_src << m_ctx.ind() << "break;\n";
         } else if constexpr (std::is_same_v<T, EmptyStmt>) {
            // Nothing to generate
         } else if constexpr (std::is_same_v<T, IfStmt>) {
            genIf(s);
         } else if constexpr (std::is_same_v<T, ForStmt>) {
            genFor(s);
         } else if constexpr (std::is_same_v<T, WhileStmt>) {
            genWhile(s);
         } else if constexpr (std::is_same_v<T, RepeatStmt>) {
            genRepeat(s);
         } else if constexpr (std::is_same_v<T, CaseStmt>) {
            genCase(s);
         }
      },
      stmt.node);
}

void BodyEmitter::genIf(const IfStmt& s)
{
   bool first = true;
   for (const auto& branch : s.branches) {
      if (branch.condition) {
         if (first) {
            m_ctx.m_src << m_ctx.ind() << "if (";
         } else {
            m_ctx.m_src << m_ctx.ind() << "} else if (";
         }
         m_ctx.m_src << genExpr(*branch.condition) << ") {\n";
         first = false;
      } else {
         m_ctx.m_src << m_ctx.ind() << "} else {\n";
      }
      m_ctx.push();
      for (const auto& st : branch.body) {
         genStmt(*st);
      }
      m_ctx.pop();
   }
   m_ctx.m_src << m_ctx.ind() << "}\n";
}

void BodyEmitter::genFor(const ForStmt& s)
{
   std::string byExpr = s.by ? genExpr(*s.by) : "1";
   std::string normalizedVar = m_ctx.normalizeIdent(s.var);
   std::string fromExpr = genExpr(*s.from);
   std::string toExpr = genExpr(*s.to);

   // Check if step is a negative literal to determine loop direction
   bool isNegativeStep = false;
   if (s.by) {
      if (auto* lit = std::get_if<LiteralExpr>(&s.by->node)) {
         if (!lit->value.empty() && lit->value[0] == '-') {
            isNegativeStep = true;
         }
      } else if (auto* unary = std::get_if<UnaryExpr>(&s.by->node)) {
         if (unary->op == "-") {
            isNegativeStep = true;
         }
      }
   }

   // The control variable must NOT be redeclared with `auto` when it is an
   // already-declared ST variable: `for (auto i = ...)` would shadow the
   // declared variable, leaving it untouched after the loop (IEC reads the
   // control variable's final value) and giving the counter a C++ type that
   // may differ from the declared one. Only undeclared (implicit) control
   // variables get `auto` so the loop counter still has a C++ declaration.
   const bool varDeclared = m_ctx.m_scope.lookupVariableInfo(normalizedVar).has_value();
   const std::string loopVarDecl = varDeclared ? "" : "auto ";

   if (isNegativeStep) {
      // Negative step: loop while var >= to
      m_ctx.m_src << m_ctx.ind() << "for (" << loopVarDecl << normalizedVar << " = " << fromExpr << "; " << normalizedVar
                  << " >= " << toExpr << "; " << normalizedVar << " += " << byExpr << ") {\n";
   } else {
      // Positive step: loop while var <= to
      m_ctx.m_src << m_ctx.ind() << "for (" << loopVarDecl << normalizedVar << " = " << fromExpr << "; " << normalizedVar
                  << " <= " << toExpr << "; " << normalizedVar << " += " << byExpr << ") {\n";
   }
   m_ctx.push();
   for (const auto& st : s.body) {
      genStmt(*st);
   }
   m_ctx.pop();
   m_ctx.m_src << m_ctx.ind() << "}\n";
}

void BodyEmitter::genWhile(const WhileStmt& s)
{
   m_ctx.m_src << m_ctx.ind() << "while (" << genExpr(*s.condition) << ") {\n";
   m_ctx.push();
   for (const auto& st : s.body) {
      genStmt(*st);
   }
   m_ctx.pop();
   m_ctx.m_src << m_ctx.ind() << "}\n";
}

void BodyEmitter::genRepeat(const RepeatStmt& s)
{
   m_ctx.m_src << m_ctx.ind() << "do {\n";
   m_ctx.push();
   for (const auto& st : s.body) {
      genStmt(*st);
   }
   m_ctx.pop();
   m_ctx.m_src << m_ctx.ind() << "} while (!(" << genExpr(*s.condition) << "));\n";
}

void BodyEmitter::genCase(const CaseStmt& s)
{
   bool firstBranch = true;

   for (const auto& branch : s.branches) {
      if (branch.values.empty()) {
         // ELSE branch
         if (firstBranch) {
            // No conditions before ELSE - generate dummy if(false)
            m_ctx.m_src << m_ctx.ind() << "if (false) {\n";
         } else {
            m_ctx.m_src << m_ctx.ind() << "} else {\n";
         }
         firstBranch = false;
      } else {
         // Build condition expression for this branch
         std::string condition;
         bool firstValue = true;

         for (const auto& cv : branch.values) {
            if (!firstValue) {
               condition += " || ";
            }
            firstValue = false;

            if (cv.high) {
               // Range case: low..high
               condition += "(" + genExpr(*s.selector) + " >= " + genExpr(*cv.low) + " && " + genExpr(*s.selector)
                            + " <= " + genExpr(*cv.high) + ")";
            } else {
               // Single value case
               condition += genExpr(*s.selector) + " == " + genExpr(*cv.low);
            }
         }

         if (firstBranch) {
            m_ctx.m_src << m_ctx.ind() << "if (" << condition << ") {\n";
            firstBranch = false;
         } else {
            m_ctx.m_src << m_ctx.ind() << "} else if (" << condition << ") {\n";
         }
      }

      // Generate branch body
      m_ctx.push();
      for (const auto& stmt : branch.body) {
         genStmt(*stmt);
      }
      m_ctx.pop();
   }

   // Close the last if/else chain
   if (!firstBranch) {
      m_ctx.m_src << m_ctx.ind() << "}\n";
   }
}

std::string BodyEmitter::genExpr(const Expr& expr, BaseType typeHint)
{
   return std::visit(
      [&](const auto& e) -> std::string {
         using T = std::decay_t<decltype(e)>;

         if constexpr (std::is_same_v<T, LiteralExpr>) {
            // Time literals are folded to their value in milliseconds (Time = UInt32).
            if (e.suffix == "TIME") {
               if (auto ms = st2cpp::semantic::iecTimeLiteralToMilliseconds(e.value)) {
                  return std::to_string(*ms) + "u";
               }
               return "0u"; // malformed literal; semantic analysis already reported it
            }
            std::string value = e.value;

            // Remove type prefix from typed literals (e.g., UDINT#123 -> 123)
            size_t firstHash = value.find('#');
            if (firstHash != std::string::npos) {
               // Verifica se ciò che precede è un tipo valido
               std::string prefix = value.substr(0, firstHash);
               // clang-format off
               static const std::unordered_set<std::string> typePrefixes = {"BOOL", "SINT", "INT", "DINT", "LINT", "USINT", 
                  "UINT", "UDINT", "ULINT", "REAL", "LREAL", "BYTE", "WORD", "DWORD", "LWORD"};
               // clang-format on

               if (typePrefixes.find(prefix) != typePrefixes.end()) {
                  // Rimuovi solo il prefisso del tipo (primo #)
                  value = value.substr(firstHash + 1);
               }
            }

            // Convert 16#... to 0x...
            if (value.rfind("16#", 0) == 0) {
               std::string hex = value.substr(3);
               // Remove underscores
               hex.erase(std::remove(hex.begin(), hex.end(), '_'), hex.end());
               return "0x" + hex;
            }
            // Convert 2#... to 0b... (C++14 binary literals)
            else if (value.rfind("2#", 0) == 0) {
               std::string bin = value.substr(2);
               bin.erase(std::remove(bin.begin(), bin.end(), '_'), bin.end());
               return "0b" + bin;
            }
            // Convert 8#... to 0... (octal)
            else if (value.rfind("8#", 0) == 0) {
               std::string oct = value.substr(2);
               oct.erase(std::remove(oct.begin(), oct.end(), '_'), oct.end());
               return "0" + oct;
            }

            // Convert string literal quotes for C++:
            // STRING (std::string): '...' -> "..."
            // WSTRING (std::wstring): "..." -> L"..."
            if (value.size() >= 2) {
               if (value.front() == '\'' && value.back() == '\'') {
                  value = "\"" + value.substr(1, value.size() - 2) + "\"";
               } else if (typeHint == BaseType::WSTRING && value.front() == '"' && value.back() == '"') {
                  value = "L" + value;
               }
            }

            return value;
         } else if constexpr (std::is_same_v<T, BoolLitExpr>) {
            return e.value ? "true" : "false";
         } else if constexpr (std::is_same_v<T, IdentExpr>) {
            std::string varName = m_ctx.declaredIdent(e.name, e.symbolId);

            // Semantic: the identifier resolved to an ENUMERATOR symbol.
            // This replaces the name-based m_ctx.m_enumeratorToEnum guesswork.
            // External-library members keep the descriptor spelling (matched
            // case-insensitively), e.g. examplelib::State::RUNNING.
            if (m_ctx.semanticAvailable() && e.symbolId != 0) {
               if (const st2cpp::semantic::Symbol* sym = m_ctx.semanticSymTab()->get(e.symbolId)) {
                  if (sym->kind == st2cpp::semantic::SymbolKind::Enumerator) {
                     const st2cpp::semantic::TypeInfo* et = m_ctx.semanticSymTab()->getType(sym->typeId);
                     const std::string enumCpp = m_ctx.semanticTypeCppName(sym->typeId, m_ctx.normalizeType(et ? et->name : ""));
                     if (!enumCpp.empty()) {
                        return enumCpp + "::" + m_ctx.semanticEnumeratorCppName(sym->typeId, sym->name);
                     }
                  }
               }
            }

            // Legacy fallback: name-based enumerator -> enum map (no semantic info)
            auto enumIt = m_ctx.m_enumeratorToEnum.find(varName);
            if (enumIt != m_ctx.m_enumeratorToEnum.end()) {
               // This is an enum enumerator - return qualified name
               return enumIt->second + "::" + varName;
            }

            // Look up the variable in the scope manager (local > global)
            auto infoOpt = m_ctx.m_scope.lookupVariableInfo(varName);
            if (infoOpt) {
               const auto& info = *infoOpt;
               if (info.atAddress.has_value()) {
                  // Variable has AT statement
                  if (info.isFunctionLocal && m_ctx.m_scope.isFunctionScope()) {
                     // In a function scope, direct access to the process image
                     // Build the key to obtain resolved address (if placeholder)
                     std::string key = m_ctx.m_currentFunctionName + "::" + varName;
                     auto it = m_ctx.m_resolvedATAddresses.find(key);
                     if (it != m_ctx.m_resolvedATAddresses.end()) {
                        return generateAddressAccess(it->second);
                     } else {
                        // Fallback: parse of the orginal variable
                        AddressExpr addr;
                        if (parseAddressString(*info.atAddress, addr)) {
                           return generateAddressAccess(addr);
                        }
                     }
                  } else {
                     // Variable with AT address normal scope (struct/global) - use getter
                     return "getPi_" + varName + "()";
                  }
               }
               // Normal variable
               return varName;
            }

            // External library global/constant: bind the C++ name from the
            // descriptor (project locals already matched above and win).
            if (m_ctx.semanticAvailable() && e.symbolId != 0) {
               if (const st2cpp::semantic::Symbol* sym = m_ctx.semanticSymTab()->get(e.symbolId)) {
                  if (sym->isExternal && sym->kind == st2cpp::semantic::SymbolKind::Variable) {
                     const std::string bound = m_ctx.semanticVariableBinding(*sym);
                     if (!bound.empty()) {
                        return bound;
                     }
                  }
               }
            }

            // Fallback: use name directly (could be a function, constant, etc.)
            return varName;
         }

         else if constexpr (std::is_same_v<T, UnaryExpr>) {
            if (e.op == "NOT") {
               return "!" + genExpr(*e.operand);
            }
            return e.op + genExpr(*e.operand);
         } else if constexpr (std::is_same_v<T, BinaryExpr>) {
            std::string op = e.op;
            // Semantic-aware operand check: prefer the decorated resolvedTypeId
            // (canonical BOOL) over the syntactic isBoolExpression heuristic.
            auto areBoolOperands = [&]() -> bool {
               if (m_ctx.semanticAvailable() && e.left->resolvedTypeId != 0 && e.right->resolvedTypeId != 0) {
                  return m_ctx.isSemanticBoolType(e.left->resolvedTypeId) && m_ctx.isSemanticBoolType(e.right->resolvedTypeId);
               }
               return isBoolExpression(e.left) && isBoolExpression(e.right);
            };
            // Map ST operators to C++
            if (op == "=" || op == "==") {
               op = "==";
            } else if (op == "AND" || op == "&&") {
               // AND is logical (&&) for BOOL, bitwise (&) for integers
               if (areBoolOperands()) {
                  op = "&&";
               } else {
                  op = "&";
               }
            } else if (op == "OR" || op == "||") {
               // OR is logical (||) for BOOL, bitwise (|) for integers
               if (areBoolOperands()) {
                  op = "||";
               } else {
                  op = "|";
               }
            } else if (op == "XOR") {
               // XOR is logical (!= for bool) for BOOL, bitwise (^) for integers
               if (areBoolOperands()) {
                  op = "!="; // Logical XOR for bool
               } else {
                  op = "^";
               }
            } else if (op == "MOD") {
               op = "%";
            } else if (op == "<>") {
               op = "!=";
            } else if (op == "**") {
               return "std::pow(" + genExpr(*e.left) + ", " + genExpr(*e.right) + ")";
            }

            // Check if parentheses are actually needed
            bool needParens = false;

            // Get precedence of current operator
            int currentPrec = getBinaryPrecFromOp(op);

            // Check left operand - if it has lower precedence, we need parentheses
            if (auto* leftBinary = std::get_if<BinaryExpr>(&e.left->node)) {
               int leftPrec = getBinaryPrecFromOp(leftBinary->op);
               if (leftPrec < currentPrec) {
                  needParens = true;
               }
            }

            // Check right operand - similar logic
            if (auto* rightBinary = std::get_if<BinaryExpr>(&e.right->node)) {
               int rightPrec = getBinaryPrecFromOp(rightBinary->op);
               if (rightPrec < currentPrec || (rightPrec == currentPrec && (op == "-" || op == "/" || op == "%"))) {
                  needParens = true;
               }
            }

            if (needParens) {
               return "(" + genExpr(*e.left) + " " + op + " " + genExpr(*e.right) + ")";
            }
            return genExpr(*e.left) + " " + op + " " + genExpr(*e.right);
         } else if constexpr (std::is_same_v<T, MemberExpr>) {
            std::string object = genExpr(*e.object);
            std::string member = m_ctx.normalizeIdent(e.member);

            // Semantic: the object type is an ENUM -> use '::' instead of '.'.
            // External enums qualify with the descriptor binding and member
            // spelling (e.g. examplelib::State::RUNNING); local enums are
            // unaffected (e.g. COLOR::GREEN).
            if (m_ctx.semanticAvailable() && e.object->resolvedTypeId != 0 && m_ctx.isSemanticEnumType(e.object->resolvedTypeId)) {
               const st2cpp::semantic::TypeInfo* et = m_ctx.semanticSymTab()->getType(e.object->resolvedTypeId);
               const std::string enumCpp = m_ctx.semanticTypeCppName(e.object->resolvedTypeId, m_ctx.normalizeType(et ? et->name : ""));
               return enumCpp + "::" + m_ctx.semanticEnumeratorCppName(e.object->resolvedTypeId, e.member);
            }

            // Legacy fallback: enum member access via name-based maps (needs :: instead of .)
            auto varIt = m_ctx.m_scope.lookupVariable(object);
            if (varIt && m_ctx.m_enumTypes.find(*varIt) != m_ctx.m_enumTypes.end()) {
               return *varIt + "::" + member;
            } else if (m_ctx.m_enumTypes.find(object) != m_ctx.m_enumTypes.end()) {
               return object + "::" + member;
            } else {
               return object + "." + member;
            }
         } else if constexpr (std::is_same_v<T, IndexExpr>) {
            std::string r = genExpr(*e.array);
            for (const auto& idx : e.indices) {
               r += "[" + genExpr(*idx) + "]";
            }
            return r;
         } else if constexpr (std::is_same_v<T, DerefExpr>) {
            return "(*" + genExpr(*e.pointer) + ")";
         } else if constexpr (std::is_same_v<T, CallExpr>) {
            if (e.isStructInit) {
               if (auto* ident = std::get_if<IdentExpr>(&e.callee->node)) {
                  std::string calleeName = m_ctx.declaredIdent(ident->name, ident->symbolId);
                  if (m_ctx.m_structTypes.find(calleeName) != m_ctx.m_structTypes.end()) {
                     std::vector<StructInitExpr::MemberInit> members;
                     for (const auto& arg : e.args) {
                        if (!arg.named || arg.isOutput) {
                           continue;
                        }
                        members.push_back({arg.name, arg.value});
                     }
                     auto orderedMembers = m_ctx.orderStructMembers(members, calleeName);
                     std::string result = "{";
                     bool first = true;
                     for (const auto& member : orderedMembers) {
                        if (!first) {
                           result += ", ";
                        }
                        first = false;
                        result += "." + m_ctx.normalizeIdent(member.member) + " = " + genExpr(*member.value);
                     }
                     result += "}";
                     return result;
                  }
               }
            }
            std::string calleeName = genExpr(*e.callee);
            // External-library FUNCTION: bind the call target (freeFunction
            // verbatim / owner::symbol). Legacy and local calls are unchanged.
            if (m_ctx.semanticAvailable()) {
               const std::string bound = m_ctx.semanticCallTargetName(e);
               if (!bound.empty()) {
                  calleeName = bound;
               }
            }
            std::string r = calleeName + "(";
            bool first = true;

            // Check if we know the function signature (for named arguments).
            // Semantic path: calleeSymbolId + SymbolTable::params; legacy path: m_ctx.m_signatures.
            auto sigIt = m_ctx.m_signatures.find(calleeName);
            std::optional<FunctionSignature> semanticSig;
            const FunctionSignature* sigPtr = nullptr;
            if (sigIt != m_ctx.m_signatures.end()) {
               sigPtr = &sigIt->second;
            } else {
               semanticSig = m_ctx.semanticSignatureForCall(e);
               if (semanticSig.has_value()) {
                  sigPtr = &*semanticSig;
               }
            }

            if (sigPtr != nullptr && !e.args.empty() && e.args[0].named) {
               // Named arguments - reorder according to signature
               const auto& sig = *sigPtr;

               std::unordered_map<std::string, std::string> argMap;
               for (const auto& arg : e.args) {
                  if (arg.named) {
                     // Normalize the argument name to match signature parameter names
                     std::string argName = m_ctx.normalizeIdent(arg.name);
                     argMap[argName] = genExpr(*arg.value);
                  }
               }

               for (const auto& param : sig.parameters) {
                  if (!first) {
                     r += ", ";
                  }
                  first = false;

                  auto it = argMap.find(param.name);
                  if (it != argMap.end()) {
                     r += it->second;
                  } else {
                     r += "/* missing: " + param.name + " */";
                  }
               }
            } else {
               // Positional arguments - preserve order
               for (const auto& arg : e.args) {
                  if (!first) {
                     r += ", ";
                  }
                  first = false;
                  r += genExpr(*arg.value);
               }
            }

            r += ")";
            return r;
         } else if constexpr (std::is_same_v<T, AdrExpr>) {
            return "&(" + genExpr(*e.operand) + ")";
         } else if constexpr (std::is_same_v<T, SizeofExpr>) {
            if (e.isType) {
               // SIZEOF(type) - standard IEC
               return "sizeof(" + m_ctx.mapType(e.type) + ")";
            } else {
               // SIZEOF(expression) - extension
               return "sizeof(" + genExpr(*e.expr) + ")";
            }
         } else if constexpr (std::is_same_v<T, CastExpr>) {
            return "static_cast<" + m_ctx.mapType(e.targetType) + ">(" + genExpr(*e.operand) + ")";
         } else if constexpr (std::is_same_v<T, ArrayInitExpr>) {
            std::string result = "{";
            bool first = true;
            for (const auto& elem : e.elements) {
               if (!first) {
                  result += ", ";
               }
               first = false;

               // Check if the element is itself an ArrayInitExpr (nested)
               if (auto* nestedArray = std::get_if<ArrayInitExpr>(&elem->node)) {
                  // Recursively generate nested initializer
                  result += genExpr(*elem);
               } else {
                  result += genExpr(*elem);
               }
            }
            result += "}";
            return result;
         } else if constexpr (std::is_same_v<T, StructInitExpr>) {
            std::string result = "{";
            bool first = true;
            for (const auto& member : e.members) {
               if (!first) {
                  result += ", ";
               }
               first = false;
               result += "." + m_ctx.normalizeIdent(member.member) + " = " + genExpr(*member.value);
            }
            result += "}";
            return result;
         } else if constexpr (std::is_same_v<T, SuperCallExpr>) {
            std::string base = m_ctx.m_scope.getBaseClass();
            if (base.empty()) {
               throw std::runtime_error("SUPER^ used but no base class in scope");
            }
            std::string r = base + "::" + m_ctx.normalizeIdent(e.methodName) + "(";
            bool first = true;
            for (const auto& arg : e.args) {
               if (!first) {
                  r += ", ";
               }
               first = false;
               r += genExpr(*arg.value);
            }
            r += ")";
            return r;
         } else if constexpr (std::is_same_v<T, AddressExpr>) {
            // Default to read access
            return generateAddressAccess(e);
         }

         return "";
      },
      expr.node);
}

std::string BodyEmitter::generateAddressAccess(const AddressExpr& addr, const TypeRef* type) const
{
   std::string imageVar = m_ctx.m_piConfig.instanceName;
   std::string accessor;

   if (addr.qualifier == AddressExpr::AddressQualifier::BIT) {
      accessor = (addr.type == AddressExpr::AddressType::INPUT)    ? "readInputBit"
                 : (addr.type == AddressExpr::AddressType::OUTPUT) ? "readOutputBit"
                                                                   : "readMarkerBit";
      if (addr.bitOffset >= 0) {
         return imageVar + "." + accessor + "(" + std::to_string(addr.byteOffset) + ", " + std::to_string(addr.bitOffset) + ")";
      } else {
         return imageVar + "." + accessor + "(" + std::to_string(addr.byteOffset) + ", 0)";
      }
   }

   // Use type size if available, otherwise fall back to qualifier
   int size = 0;
   if (type) {
      size = m_ctx.getTypeSizeInBytes(*type);
   }
   if (size == 0) {
      // Fallback: use qualifier
      switch (addr.qualifier) {
      case AddressExpr::AddressQualifier::BYTE:
         size = 1;
         break;
      case AddressExpr::AddressQualifier::WORD:
         size = 2;
         break;
      case AddressExpr::AddressQualifier::DWORD:
         size = 4;
         break;
      case AddressExpr::AddressQualifier::LWORD:
         size = 8;
         break;
      default:
         size = 1;
         break;
      }
   }

   switch (size) {
   case 1:
      accessor = (addr.type == AddressExpr::AddressType::INPUT)    ? "readInputByte"
                 : (addr.type == AddressExpr::AddressType::OUTPUT) ? "readOutputByte"
                                                                   : "readMarkerByte";
      break;
   case 2:
      accessor = (addr.type == AddressExpr::AddressType::INPUT)    ? "readInputWord"
                 : (addr.type == AddressExpr::AddressType::OUTPUT) ? "readOutputWord"
                                                                   : "readMarkerWord";
      break;
   case 4:
      accessor = (addr.type == AddressExpr::AddressType::INPUT)    ? "readInputDword"
                 : (addr.type == AddressExpr::AddressType::OUTPUT) ? "readOutputDword"
                                                                   : "readMarkerDword";
      break;
   case 8:
      accessor = (addr.type == AddressExpr::AddressType::INPUT)    ? "readInputLword"
                 : (addr.type == AddressExpr::AddressType::OUTPUT) ? "readOutputLword"
                                                                   : "readMarkerLword";
      break;
   default:
      // fallback to BYTE
      accessor = (addr.type == AddressExpr::AddressType::INPUT)    ? "readInputByte"
                 : (addr.type == AddressExpr::AddressType::OUTPUT) ? "readOutputByte"
                                                                   : "readMarkerByte";
      break;
   }

   return imageVar + "." + accessor + "(" + std::to_string(addr.byteOffset) + ")";
}

std::string BodyEmitter::generateAddressAccess(const AddressExpr& addr)
{
   return generateAddressAccess(addr, nullptr);
}

std::string BodyEmitter::generateAddressWrite(const AddressExpr& addr, const std::string& value, const TypeRef* type) const
{
   std::string imageVar = m_ctx.m_piConfig.instanceName;
   std::string accessor;

   // BIT access: always use bit accessors
   if (addr.qualifier == AddressExpr::AddressQualifier::BIT) {
      accessor = (addr.type == AddressExpr::AddressType::INPUT)    ? "writeInputBit"
                 : (addr.type == AddressExpr::AddressType::OUTPUT) ? "writeOutputBit"
                                                                   : "writeMarkerBit";
      if (addr.bitOffset >= 0) {
         return imageVar + "." + accessor + "(" + std::to_string(addr.byteOffset) + ", " + std::to_string(addr.bitOffset) + ", " + value
                + ")";
      } else {
         return imageVar + "." + accessor + "(" + std::to_string(addr.byteOffset) + ", 0, " + value + ")";
      }
   }

   // Determine size: prefer type if available, otherwise use qualifier
   int size = 0;
   if (type) {
      size = m_ctx.getTypeSizeInBytes(*type);
   }
   if (size == 0) {
      // Fallback to qualifier
      switch (addr.qualifier) {
      case AddressExpr::AddressQualifier::BYTE:
         size = 1;
         break;
      case AddressExpr::AddressQualifier::WORD:
         size = 2;
         break;
      case AddressExpr::AddressQualifier::DWORD:
         size = 4;
         break;
      case AddressExpr::AddressQualifier::LWORD:
         size = 8;
         break;
      default:
         size = 1;
         break;
      }
   }

   // Select the appropriate write accessor
   switch (size) {
   case 1:
      accessor = (addr.type == AddressExpr::AddressType::INPUT)    ? "writeInputByte"
                 : (addr.type == AddressExpr::AddressType::OUTPUT) ? "writeOutputByte"
                                                                   : "writeMarkerByte";
      break;
   case 2:
      accessor = (addr.type == AddressExpr::AddressType::INPUT)    ? "writeInputWord"
                 : (addr.type == AddressExpr::AddressType::OUTPUT) ? "writeOutputWord"
                                                                   : "writeMarkerWord";
      break;
   case 4:
      accessor = (addr.type == AddressExpr::AddressType::INPUT)    ? "writeInputDword"
                 : (addr.type == AddressExpr::AddressType::OUTPUT) ? "writeOutputDword"
                                                                   : "writeMarkerDword";
      break;
   case 8:
      accessor = (addr.type == AddressExpr::AddressType::INPUT)    ? "writeInputLword"
                 : (addr.type == AddressExpr::AddressType::OUTPUT) ? "writeOutputLword"
                                                                   : "writeMarkerLword";
      break;
   default:
      // Fallback to byte
      accessor = (addr.type == AddressExpr::AddressType::INPUT)    ? "writeInputByte"
                 : (addr.type == AddressExpr::AddressType::OUTPUT) ? "writeOutputByte"
                                                                   : "writeMarkerByte";
      break;
   }

   return imageVar + "." + accessor + "(" + std::to_string(addr.byteOffset) + ", " + value + ")";
}

std::string BodyEmitter::generateAddressWrite(const AddressExpr& addr, const std::string& value)
{
   return generateAddressWrite(addr, value, nullptr);
}

std::string BodyEmitter::generateOrderedStructInit(const TypeRef& type, const std::shared_ptr<Expr>& initExpr)
{
   if (!initExpr) {
      return "{}";
   }

   if (type.base == BaseType::NAMED) {
      std::string structName = m_ctx.normalizeType(type.name);
      if (m_ctx.m_structTypes.find(structName) != m_ctx.m_structTypes.end()) {
         if (auto* initStruct = std::get_if<StructInitExpr>(&initExpr->node)) {
            auto orderedMembers = m_ctx.orderStructMembers(initStruct->members, structName);
            StructInitExpr orderedInit;
            orderedInit.members = orderedMembers;
            Expr tmp(std::move(orderedInit));
            return genExpr(tmp);
         }
      }
   }
   return genExpr(*initExpr);
}

bool BodyEmitter::isBoolExpression(const std::shared_ptr<Expr>& expr) const
{
   if (!expr) {
      return false;
   }

   return std::visit(
      [this](const auto& e) -> bool {
         using T = std::decay_t<decltype(e)>;

         if constexpr (std::is_same_v<T, BoolLitExpr>) {
            return true;
         } else if constexpr (std::is_same_v<T, IdentExpr>) {
            // Check if variable is BOOL-typed in scope
            std::string varName = m_ctx.declaredIdent(e.name, e.symbolId);
            auto infoOpt = m_ctx.m_scope.lookupVariableInfo(varName);
            if (infoOpt) {
               const auto& info = *infoOpt;
               // Check if the C++ type is Bool
               return info.type == "Bool" || info.type == "bool" || info.type == "BOOL";
            }
            // Check if it's an enum (not bool)
            if (m_ctx.m_enumTypes.find(varName) != m_ctx.m_enumTypes.end()) {
               return false;
            }
            return false;
         } else if constexpr (std::is_same_v<T, UnaryExpr>) {
            // NOT returns BOOL
            if (e.op == "NOT") {
               return true;
            }
            return isBoolExpression(e.operand);
         } else if constexpr (std::is_same_v<T, BinaryExpr>) {
            // Comparison operators return BOOL
            if (e.op == "=" || e.op == "==" || e.op == "<>" || e.op == "!=" || e.op == "<" || e.op == "<=" || e.op == ">"
                || e.op == ">=") {
               return true;
            }
            // Logical operators return BOOL (but we're trying to determine if operands are BOOL)
            // For AND/OR/XOR, the result is BOOL if both operands are BOOL
            if (e.op == "AND" || e.op == "OR" || e.op == "XOR") {
               return isBoolExpression(e.left) && isBoolExpression(e.right);
            }
            return false;
         } else if constexpr (std::is_same_v<T, CallExpr>) {
            // Check if function returns BOOL
            if (auto* ident = std::get_if<IdentExpr>(&e.callee->node)) {
               std::string funcName = m_ctx.declaredIdent(ident->name, ident->symbolId);
               auto sigIt = m_ctx.m_signatures.find(funcName);
               if (sigIt != m_ctx.m_signatures.end()) {
                  return m_ctx.isVoidType(sigIt->second.returnType) == false && sigIt->second.returnType.base == BaseType::BOOL;
               }
            }
            return false;
         } else if constexpr (std::is_same_v<T, MemberExpr>) {
            // Check if member access is BOOL
            return isBoolExpression(e.object);
         } else if constexpr (std::is_same_v<T, CastExpr>) {
            // Check target type
            return e.targetType.base == BaseType::BOOL;
         } else if constexpr (std::is_same_v<T, AddressExpr>) {
            // Bit access (%IX, %QX, %MX) returns BOOL
            return e.qualifier == AddressExpr::AddressQualifier::BIT;
         }
         return false;
      },
      expr->node);
}

} // namespace st2cpp::codegen
