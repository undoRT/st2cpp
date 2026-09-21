/**
 * @file DeclVisitor.cpp
 * @brief Declaration visitor implementation for semantic analysis
 * @author Salvatore Bamundo
 * @date 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#include "semantic/DeclVisitor.h"
#include "semantic/SymbolTable.h"
#include "semantic/Diagnostics.h"
#include "semantic/TypeSystem.h"
#include "ast/AST.h"
#include <algorithm>
#include <unordered_set>
#include <queue>

namespace st2cpp::semantic {

namespace {

/**
 * @brief Extract a constant array bound from its AST expression.
 *
 * IEC 61131-3 array bounds are integer literals (possibly negative).
 * Mirrors the CodeGenerator's parsing of ArrayDim bounds so the semantic
 * TypeInfo holds the real bounds instead of placeholders.
 *
 * @param bound Bound expression (may be null)
 * @param[out] value Extracted bound value
 * @return true when the bound was a decodable integer literal
 */
bool extractArrayBoundValue(const std::shared_ptr<Expr>& bound, int& value)
{
   if (!bound) {
      return false;
   }
   if (const auto* lit = std::get_if<LiteralExpr>(&bound->node)) {
      try {
         value = std::stoi(lit->value);
         return true;
      } catch (...) { /* fall through */ }
   }
   if (const auto* unary = std::get_if<UnaryExpr>(&bound->node)) {
      if (unary->op == "-") {
         if (const auto* lit = std::get_if<LiteralExpr>(&unary->operand->node)) {
            try {
               value = -std::stoi(lit->value);
               return true;
            } catch (...) { /* fall through */ }
         }
      }
   }
   return false;
}

} // namespace

/**
 * @brief Construct a declaration visitor for header registration.
 * @param symTab The symbol table to register declarations into
 * @param diag The diagnostics collector for reported problems
 */
DeclVisitor::DeclVisitor(SymbolTable& symTab, Diagnostics& diag)
    : symTab_(symTab), diag_(diag) {}

/**
 * @brief Register all declarations of a translation unit into the symbol table.
 * @details Two real passes: Pass A declares every enum, struct, interface and
 * POU name (with their TypeInfo) without resolving any type reference, so IEC
 * 61131-3 source order never matters — a STRUCT may reference a STRUCT or FB
 * declared later, a FUNCTION may return a later type, and so on. Pass B
 * resolves struct members, interface signatures, POU bodies and globals now
 * that all type names are visible, then resolves inheritance and computes the
 * topological orders.
 * @param tu The translation unit to register
 */
void DeclVisitor::visitTranslationUnit(const TranslationUnit& tu) {
    // Pass A: declare names and register empty TypeInfos (stable TypeIds).
    // ENUMs go first: there is no type resolution in this pass, but the
    // enumerators must be typed with their enum as today.
    for (const auto& et : tu.enums) {
        registerEnumType(et);
    }
    for (const auto& st : tu.structs) {
        registerStructHeader(st);
    }
    for (const auto& iface : tu.interfaces) {
        registerInterfaceHeader(iface);
    }
    registerPouHeaders(tu.pous);

    // Pass B: resolve bodies. Every type name is now visible regardless of the
    // order in which enum/struct/interface/POU were declared or merged.
    for (const auto& st : tu.structs) {
        registerStructBody(st);
    }
    for (const auto& iface : tu.interfaces) {
        registerInterfaceBody(iface);
    }
    registerPouBodies(tu.pous);

    // Phase 4: Register global variables
    for (const auto& sec : tu.globals) {
        SymbolId globalScopeId = symTab_.globalScope();
        registerVarSection(sec, globalScopeId, SymbolKind::Variable);
    }

    // Phase 5: Resolve inheritance
    resolveInheritance();

    // Phase 6: Compute topological orders
    computeTopoOrders();
}

// ============================================================================
// Type Registration
// ============================================================================

/**
 * @brief Register a struct type name (Pass A), with no member resolution.
 * @details Declares the struct type symbol, registers its TypeInfo with a
 * stable TypeId and opens — but does not fill — the STRUCT_<name> scope whose
 * id is kept for Pass B.
 * @param st The struct type to register
 */
void DeclVisitor::registerStructHeader(const StructType& st) {
    SourceLocation loc = makeLocation(0);
    
    // Check for duplicate in global scope
    SymbolId existing = symTab_.lookupGlobal(st.name);
    if (existing != 0) {
        reportError(DiagnosticCode::DuplicateDeclaration, 
            "duplicate struct declaration: " + st.name, loc);
        return;
    }

    // Create the struct type symbol
    SymbolId structSymId = symTab_.declare(st.name, SymbolKind::Type);
    if (structSymId == 0) {
        reportError(DiagnosticCode::DuplicateDeclaration, 
            "duplicate struct declaration: " + st.name, loc);
        return;
    }

    // Register TypeInfo for the struct (empty: members resolved in Pass B)
    TypeInfo typeInfo;
    typeInfo.id = 0;
    typeInfo.kind = TypeKind::Struct;
    typeInfo.name = st.name;
    typeInfo.symbolId = structSymId;
    TypeId typeId = symTab_.registerType(typeInfo);
    Symbol* structSym = symTab_.get(structSymId);
    if (structSym) {
        structSym->typeId = typeId;
    }

    // Open the struct scope now so its ScopeId is stable; members are declared
    // in it during Pass B. The scope is left inactive until then.
    structScopes_[SymbolTable::normalizeKey(st.name)] = symTab_.pushScope("STRUCT_" + st.name);
    symTab_.popScope();
}

/**
 * @brief Resolve and register a struct type's members (Pass B).
 * @details Re-enters the STRUCT_<name> scope created in Pass A and declares
 * each member with its resolved type, reporting duplicate member names.
 * @param st The struct type whose members are registered
 */
void DeclVisitor::registerStructBody(const StructType& st) {
    SymbolId structSymId = symTab_.lookupGlobal(st.name);
    if (structSymId == 0) {
        // Duplicate declaration already reported in header phase
        return;
    }
    Symbol* structSym = symTab_.get(structSymId);
    if (!structSym) return;

    auto scopeIt = structScopes_.find(SymbolTable::normalizeKey(st.name));
    if (scopeIt == structScopes_.end() || !symTab_.enterScope(scopeIt->second)) {
        return;
    }

    std::vector<SymbolId> memberSymIds;
    for (const auto& member : st.members) {
        TypeId memberTypeId = resolveTypeRef(member.type);
        SymbolId memberSymId = symTab_.declare(member.name, SymbolKind::StructMember, memberTypeId);
        if (memberSymId == 0) {
            reportError(DiagnosticCode::DuplicateDeclaration,
                "duplicate struct member: " + member.name, makeLocation(0));
        } else {
            Symbol* memberSym = symTab_.get(memberSymId);
            if (memberSym) {
                memberSym->typeId = memberTypeId;
            }
            memberSymIds.push_back(memberSymId);
        }
    }
    symTab_.exitScope(); // Leave struct scope

    structSym->members = memberSymIds;
}

/**
 * @brief Register an enum type and its enumerators in the symbol table.
 * @details Registers the enum TypeInfo before its enumerators so each enumerator
 * is typed with the enum type, and reports duplicate enumerator names.
 * @param et The enum type to register
 */
void DeclVisitor::registerEnumType(const EnumType& et) {
    SourceLocation loc = makeLocation(0);
    
    // Check for duplicate in global scope
    SymbolId existing = symTab_.lookup(et.name);
    if (existing != 0) {
        reportError(DiagnosticCode::DuplicateDeclaration, 
            "duplicate enum declaration: " + et.name, loc);
        return;
    }

    // Create the enum type symbol
    SymbolId enumSymId = symTab_.declare(et.name, SymbolKind::Type);
    if (enumSymId == 0) {
        reportError(DiagnosticCode::DuplicateDeclaration, 
            "duplicate enum declaration: " + et.name, loc);
        return;
    }

    // Register TypeInfo for the enum (before enumerators, so that each
    // enumerator can be typed with the enum type instead of plain INT)
    Symbol* enumSym = symTab_.get(enumSymId);
    TypeInfo typeInfo;
    typeInfo.id = 0;
    typeInfo.kind = TypeKind::Enum;
    typeInfo.name = et.name;
    typeInfo.symbolId = enumSymId;
    TypeId enumTypeId = symTab_.registerType(typeInfo);
    if (enumSym) {
        enumSym->typeId = enumTypeId;
    }

    // Register enumerators
    std::vector<SymbolId> enumeratorSymIds;
    std::unordered_set<std::string> seenEnumValues;
    
    for (size_t i = 0; i < et.enumerators.size(); ++i) {
        const auto& enumVal = et.enumerators[i];
        
        // Check for duplicate enumerator names
        if (seenEnumValues.find(enumVal.name) != seenEnumValues.end()) {
            reportError(DiagnosticCode::DuplicateEnumValue,
                "duplicate enum value: " + enumVal.name, makeLocation(0));
        }
        seenEnumValues.insert(enumVal.name);

        SymbolId enumValSymId = symTab_.declare(enumVal.name, SymbolKind::Enumerator);
        if (enumValSymId != 0) {
            Symbol* enumValSym = symTab_.get(enumValSymId);
            if (enumValSym) {
                // Enumerators are values of the enum type, not INT
                enumValSym->typeId = enumTypeId;
            }
            enumeratorSymIds.push_back(enumValSymId);
        }
    }

    // Update enum symbol with enumerators
    if (enumSym) {
        enumSym->enumerators = enumeratorSymIds;
    }
}

// ============================================================================
// Interface Registration
// ============================================================================

/**
 * @brief Register an interface type name (Pass A), with no method resolution.
 * @details Declares the interface symbol, registers its TypeInfo with a stable
 * TypeId and opens — but does not fill — the INTERFACE_<name> scope whose id is
 * kept for Pass B.
 * @param iface The interface to register
 */
void DeclVisitor::registerInterfaceHeader(const Interface& iface) {
    SourceLocation loc = makeLocation(iface.line);
    
    // Check for duplicate in global scope
    SymbolId existing = symTab_.lookupGlobal(iface.name);
    if (existing != 0) {
        reportError(DiagnosticCode::DuplicateDeclaration, 
            "duplicate interface declaration: " + iface.name, loc);
        return;
    }

    // Create interface symbol
    SymbolId ifaceSymId = symTab_.declare(iface.name, SymbolKind::Interface);
    if (ifaceSymId == 0) {
        reportError(DiagnosticCode::DuplicateDeclaration, 
            "duplicate interface declaration: " + iface.name, loc);
        return;
    }

    // Register TypeInfo for interface
    TypeInfo typeInfo;
    typeInfo.id = 0;
    typeInfo.kind = TypeKind::Interface;
    typeInfo.name = iface.name;
    typeInfo.symbolId = ifaceSymId;
    TypeId typeId = symTab_.registerType(typeInfo);
    Symbol* ifaceSym = symTab_.get(ifaceSymId);
    if (ifaceSym) {
        ifaceSym->typeId = typeId;
    }

    // Open the interface scope now so its ScopeId is stable; methods are
    // declared in it during Pass B. The scope is left inactive until then.
    interfaceScopes_[SymbolTable::normalizeKey(iface.name)] = symTab_.pushScope("INTERFACE_" + iface.name);
    symTab_.popScope();
}

/**
 * @brief Resolve and register an interface's abstract methods (Pass B).
 * @details Re-enters the INTERFACE_<name> scope created in Pass A and declares
 * each method with its resolved return type and parameters.
 * @param iface The interface whose methods are registered
 */
void DeclVisitor::registerInterfaceBody(const Interface& iface) {
    SymbolId ifaceSymId = symTab_.lookupGlobal(iface.name);
    if (ifaceSymId == 0) {
        // Duplicate declaration already reported in header phase
        return;
    }
    Symbol* ifaceSym = symTab_.get(ifaceSymId);
    if (!ifaceSym) return;

    auto scopeIt = interfaceScopes_.find(SymbolTable::normalizeKey(iface.name));
    if (scopeIt == interfaceScopes_.end() || !symTab_.enterScope(scopeIt->second)) {
        return;
    }

    std::vector<SymbolId> methodSymIds;
    for (const auto& method : iface.methods) {
        SymbolId methodSymId = symTab_.declare(method.name, SymbolKind::Method);
        if (methodSymId != 0) {
            Symbol* methodSym = symTab_.get(methodSymId);
            if (methodSym) {
                methodSym->isAbstract = true; // Interface methods are abstract by default
                methodSym->containingFbId = ifaceSymId;
                
                // Register return type (interface methods use the interface's line)
                TypeId returnTypeId = 0;
                if (method.returnType.base != BaseType::VOID) {
                    returnTypeId = resolveTypeRef(method.returnType, iface.line);
                }
                methodSym->returnTypeId = returnTypeId;
                
                // Register parameters
                std::vector<SymbolId> paramSymIds;
                for (const auto& param : method.parameters) {
                    TypeId paramTypeId = resolveTypeRef(param.type, iface.line);
                    SymbolId paramSymId = symTab_.declare(param.name, SymbolKind::Parameter, paramTypeId);
                    if (paramSymId != 0) {
                        Symbol* paramSym = symTab_.get(paramSymId);
                        if (paramSym) {
                            paramSym->typeId = paramTypeId;
                            if (param.kind == VarKind::INPUT) {
                                paramSym->paramDir = ParamDir::Input;
                            } else if (param.kind == VarKind::OUTPUT) {
                                paramSym->paramDir = ParamDir::Output;
                            } else if (param.kind == VarKind::IN_OUT) {
                                paramSym->paramDir = ParamDir::InOut;
                            }
                            paramSym->hasDefaultValue = (param.initialValue != nullptr);
                        }
                        paramSymIds.push_back(paramSymId);
                    }
                }
                methodSym->params = paramSymIds;
            }
            methodSymIds.push_back(methodSymId);
        }
    }
    symTab_.exitScope(); // Leave interface scope

    ifaceSym->members = methodSymIds;
}

// ============================================================================
// POU Registration (Two-Pass)
// ============================================================================

/**
 * @brief Register POU headers (names only, Pass A).
 * @details Declares each POU symbol with its kind, assigns a FunctionBlock
 * TypeInfo for FBs and stores the pending EXTENDS/IMPLEMENTS clauses and the
 * FUNCTION return types for later resolution (Pass B). No type reference is
 * resolved here, so POU order never matters.
 * @param pous The list of POUs whose headers are registered
 */
void DeclVisitor::registerPouHeaders(const std::vector<POU>& pous) {
    for (const auto& pou : pous) {
        SourceLocation loc = makeLocation(pou.line);
        
        // Check for duplicate in global scope
        SymbolId existing = symTab_.lookupGlobal(pou.name);
        if (existing != 0) {
            reportError(DiagnosticCode::DuplicateDeclaration, 
                "duplicate POU declaration: " + pou.name, loc);
            continue;
        }

        SymbolKind kind;
        switch (pou.kind) {
            case POUKind::FUNCTION_BLOCK: kind = SymbolKind::FunctionBlock; break;
            case POUKind::FUNCTION: kind = SymbolKind::Function; break;
            case POUKind::PROGRAM: kind = SymbolKind::Program; break;
        }

        SymbolId pouSymId = symTab_.declare(pou.name, kind);
        if (pouSymId == 0) {
            reportError(DiagnosticCode::DuplicateDeclaration, 
                "duplicate POU declaration: " + pou.name, loc);
            continue;
        }

        Symbol* pouSym = symTab_.get(pouSymId);
        if (pouSym) {
            pouSym->isAbstract = pou.isAbstract;
            pouSym->isFinal = pou.isFinal;
            
            // Register the FB instance type: FB-typed members/parameters then
            // resolve to a nominal FunctionBlock TypeInfo instead of Unknown.
            // Used by FB composition ordering (topoSortFbs) and nominal typing.
            if (kind == SymbolKind::FunctionBlock) {
                TypeInfo fbType;
                fbType.kind = TypeKind::FunctionBlock;
                fbType.name = pou.name;
                fbType.symbolId = pouSymId;
                fbType.sizeInBytes = 0;   // computed per instance
                fbType.alignment = 0;
                pouSym->typeId = symTab_.registerType(fbType);
            }
            
            // Store EXTENDS/IMPLEMENTS for later resolution
            if (!pou.extends.empty()) {
                pendingExtends_[pou.name] = pou.extends;
            }
            if (!pou.implements.empty()) {
                pendingImplements_[pou.name] = pou.implements;
            }
        }
    }
}

/**
 * @brief Register the bodies (scopes and members) of a list of POUs.
 * @param pous The list of POUs whose bodies are registered
 */
void DeclVisitor::registerPouBodies(const std::vector<POU>& pous) {
    for (const auto& pou : pous) {
        registerPou(pou);
    }
}

/**
 * @brief Register the full body of a single POU.
 * @details Opens the POU's scope (FB_/FUNC_/PROG_ prefix), registers its variable
 * sections and, for FBs, its methods, then records the scope on the POU symbol.
 * @param pou The POU to register
 */
void DeclVisitor::registerPou(const POU& pou) {
    SourceLocation loc = makeLocation(pou.line);
    
    // Find the symbol ID for this POU
    SymbolId pouSymId = symTab_.lookup(pou.name);
    if (pouSymId == 0) {
        // Already reported as duplicate in header phase
        return;
    }

    Symbol* pouSym = symTab_.get(pouSymId);
    if (!pouSym) return;

    currentPouId_ = pouSymId;

    // Resolve FUNCTION return type (deferred to Pass B, when every type name
    // is visible regardless of declaration order).
    if (pou.kind == POUKind::FUNCTION && pou.returnType.base != BaseType::VOID) {
        pouSym->returnTypeId = resolveTypeRef(pou.returnType, pou.line);
    }

    // Create POU scope
    std::string scopeName;
    switch (pou.kind) {
        case POUKind::FUNCTION_BLOCK: scopeName = "FB_" + pou.name; break;
        case POUKind::FUNCTION: scopeName = "FUNC_" + pou.name; break;
        case POUKind::PROGRAM: scopeName = "PROG_" + pou.name; break;
    }
    ScopeId pouScopeId = symTab_.pushScope(scopeName);
    pouSym->scopeId = pouScopeId;

    // Register variable sections
    for (const auto& sec : pou.varSections) {
        SymbolKind varKind;
        switch (sec.kind) {
            case VarKind::INPUT: varKind = SymbolKind::Parameter; break;
            case VarKind::OUTPUT: varKind = SymbolKind::Parameter; break;
            case VarKind::IN_OUT: varKind = SymbolKind::Parameter; break;
            case VarKind::EXTERNAL: varKind = SymbolKind::Variable; break;
            case VarKind::GLOBAL: varKind = SymbolKind::Variable; break;
            case VarKind::TEMP: varKind = SymbolKind::Variable; break;
            default: varKind = SymbolKind::Variable; break;
        }
        registerVarSection(sec, pouScopeId, varKind);
    }

    // Register methods (only for FUNCTION_BLOCK)
    if (pou.kind == POUKind::FUNCTION_BLOCK) {
        registerMethods(pou.methods, pouScopeId);
    }

    symTab_.popScope(); // Pop POU scope
    currentPouId_ = 0;
}

// ============================================================================
// Variable Registration
// ============================================================================

/**
 * @brief Register every declaration of a variable section into a scope.
 * @details The section kind is forwarded to parameter-aware registration; the
 * isMethodLocal flag is currently unused.
 * @param section The variable section to register
 * @param scopeId The scope the variables are declared in
 * @param varKind The symbol kind for the declared variables
 * @param isMethodLocal Whether the declarations belong to a method (unused)
 */
void DeclVisitor::registerVarSection(const VarSection& section, SymbolId scopeId, 
                                     SymbolKind varKind, bool isMethodLocal) {
    (void)isMethodLocal; // May be used later
    
    for (const auto& decl : section.decls) {
        registerVarDecl(decl, scopeId, varKind, section.kind);
    }
}

/**
 * @brief Register a single variable declaration in the symbol table.
 * @details Resolves the declared type, declares the variable symbol and copies
 * its properties (constant, retain, AT address, scope). POU parameter sections
 * also append the symbol to the current POU's parameter list.
 * @param decl The variable declaration to register
 * @param scopeId The scope the variable is declared in
 * @param varKind The symbol kind for the variable
 * @param sectionKind The section kind of the declaring variable section
 */
void DeclVisitor::registerVarDecl(const VarDecl& decl, SymbolId scopeId, SymbolKind varKind, VarKind sectionKind) {
    SourceLocation loc = makeLocation(decl.line);
    
    // Check for duplicate in current scope
    SymbolId existing = symTab_.lookup(decl.name);
    if (existing != 0) {
        reportError(DiagnosticCode::DuplicateDeclaration, 
            "duplicate variable declaration: " + decl.name, loc);
        return;
    }

    TypeId typeId = resolveTypeRef(decl.type, decl.line);
    
    SymbolId varSymId = symTab_.declare(decl.name, varKind, typeId);
    if (varSymId == 0) {
        reportError(DiagnosticCode::DuplicateDeclaration, 
            "duplicate variable declaration: " + decl.name, loc);
        return;
    }

    // Update symbol with variable properties
    Symbol* varSym = symTab_.get(varSymId);
    if (varSym) {
        varSym->typeId = typeId;
        varSym->isConstant = decl.isConstant;
        varSym->isRetain = decl.isRetain;
        varSym->atAddress = decl.atAddress;
        varSym->scopeId = scopeId;
        if (sectionKind == VarKind::INPUT) {
            varSym->paramDir = ParamDir::Input;
        } else if (sectionKind == VarKind::OUTPUT) {
            varSym->paramDir = ParamDir::Output;
        } else if (sectionKind == VarKind::IN_OUT) {
            varSym->paramDir = ParamDir::InOut;
        }
        varSym->hasDefaultValue = (decl.initialValue != nullptr);
    }

    // If this is a parameter section (INPUT, OUTPUT, IN_OUT) and we're in a POU context,
    // add the parameter to the current POU's params vector
    if (currentPouId_ != 0 && 
        (sectionKind == VarKind::INPUT || sectionKind == VarKind::OUTPUT || sectionKind == VarKind::IN_OUT)) {
        Symbol* pouSym = symTab_.get(currentPouId_);
        if (pouSym) {
            pouSym->params.push_back(varSymId);
        }
    }
}

// ============================================================================
// Method Registration
// ============================================================================

/**
 * @brief Register all methods of a function block.
 * @param methods The list of methods to register
 * @param fbScopeId The scope of the containing function block
 */
void DeclVisitor::registerMethods(const std::vector<Method>& methods, SymbolId fbScopeId) {
    for (const auto& method : methods) {
        registerMethod(method, fbScopeId, currentPouId_);
    }
}

/**
 * @brief Register a single method of a function block.
 * @details Declares the method symbol, records its return type, opens a scoped
 * METHOD_<name> scope where parameters and local variables are declared, and
 * appends the method to the FB's member list.
 * @param method The method to register
 * @param fbScopeId The scope of the containing function block
 * @param fbSymbolId The symbol of the containing function block
 */
void DeclVisitor::registerMethod(const Method& method, SymbolId fbScopeId, SymbolId fbSymbolId) {
    SourceLocation loc = makeLocation(method.line);
    
    SymbolId methodSymId = symTab_.declare(method.name, SymbolKind::Method);
    if (methodSymId == 0) {
        reportError(DiagnosticCode::DuplicateDeclaration, 
            "duplicate method declaration: " + method.name, loc);
        return;
    }

    Symbol* methodSym = symTab_.get(methodSymId);
    if (!methodSym) return;

    methodSym->containingFbId = fbSymbolId;
    methodSym->isAbstract = method.isAbstract;
    methodSym->isFinal = method.isFinal;
    methodSym->isOverride = method.isOverride;
    
    // Register return type
    TypeId returnTypeId = 0;
    if (method.returnType.base != BaseType::VOID) {
        returnTypeId = resolveTypeRef(method.returnType, method.line);
    }
    methodSym->returnTypeId = returnTypeId;
    
    // Create method scope
    ScopeId methodScopeId = symTab_.pushScope("METHOD_" + method.name);
    methodSym->scopeId = methodScopeId;
    
    // Register parameters in method scope
    std::vector<SymbolId> paramSymIds;
    for (const auto& param : method.parameters) {
        TypeId paramTypeId = resolveTypeRef(param.type, method.line);
        SymbolId paramSymId = symTab_.declare(param.name, SymbolKind::Parameter, paramTypeId);
        if (paramSymId != 0) {
            Symbol* paramSym = symTab_.get(paramSymId);
            if (paramSym) {
                paramSym->typeId = paramTypeId;
                if (param.kind == VarKind::INPUT) {
                    paramSym->paramDir = ParamDir::Input;
                } else if (param.kind == VarKind::OUTPUT) {
                    paramSym->paramDir = ParamDir::Output;
                } else if (param.kind == VarKind::IN_OUT) {
                    paramSym->paramDir = ParamDir::InOut;
                }
                paramSym->hasDefaultValue = (param.initialValue != nullptr);
            }
            paramSymIds.push_back(paramSymId);
        }
    }
    methodSym->params = paramSymIds;
    
    // Register local variables in method scope
    for (const auto& localVar : method.localVars) {
        TypeId varTypeId = resolveTypeRef(localVar.type, localVar.line);
        SymbolId varSymId = symTab_.declare(localVar.name, SymbolKind::Variable, varTypeId);
        if (varSymId != 0) {
            Symbol* varSym = symTab_.get(varSymId);
            if (varSym) {
                varSym->typeId = varTypeId;
                varSym->isConstant = localVar.isConstant;
                varSym->isRetain = localVar.isRetain;
                varSym->atAddress = localVar.atAddress;
                varSym->scopeId = methodScopeId;
            }
        }
    }
    
    // Add method to FB's members
    Symbol* fbSym = symTab_.get(currentPouId_);
    if (fbSym) {
        fbSym->members.push_back(methodSymId);
    }
    
    // Pop method scope
    symTab_.popScope();
}

// ============================================================================
// Inheritance Resolution
// ============================================================================

/**
 * @brief Resolve the pending EXTENDS/IMPLEMENTS clauses against registered symbols.
 * @details Sets base-class links for valid EXTENDS (rejecting non-FB bases,
 * circular and final-base inheritance) and records implemented interface symbols,
 * reporting missing bases or interfaces as diagnostics.
 */
void DeclVisitor::resolveInheritance() {
    // Resolve EXTENDS
    for (const auto& [pouName, baseName] : pendingExtends_) {
        SymbolId pouSymId = symTab_.lookup(pouName);
        SymbolId baseSymId = symTab_.lookup(baseName);
        
        if (pouSymId == 0 || baseSymId == 0) {
            if (baseSymId == 0) {
                reportError(DiagnosticCode::InvalidExtends,
                    "base class not found: " + baseName, makeLocation(0));
            }
            continue;
        }

        Symbol* pouSym = symTab_.get(pouSymId);
        Symbol* baseSym = symTab_.get(baseSymId);
        
        if (pouSym && baseSym) {
            // Check that base is a FunctionBlock
            if (baseSym->kind != SymbolKind::FunctionBlock) {
                reportError(DiagnosticCode::InvalidExtends,
                    "base class must be a FUNCTION_BLOCK: " + baseName, makeLocation(0));
                continue;
            }
            
            // Check for circular inheritance
            if (wouldCreateCycle(pouSymId, baseSymId)) {
                reportError(DiagnosticCode::CircularInheritance,
                    "circular inheritance detected: " + pouName + " extends " + baseName, makeLocation(0));
                continue;
            }
            
            // Check final
            if (baseSym->isFinal) {
                reportError(DiagnosticCode::FinalMethodOverride,
                    "cannot extend final function block: " + baseName, makeLocation(0));
                continue;
            }
            
            pouSym->baseClassId = baseSymId;
            fbBaseClass_[pouSymId] = baseSymId;
        }
    }

    // Resolve IMPLEMENTS
    for (const auto& [pouName, interfaceNames] : pendingImplements_) {
        SymbolId pouSymId = symTab_.lookup(pouName);
        if (pouSymId == 0) continue;

        Symbol* pouSym = symTab_.get(pouSymId);
        if (!pouSym) continue;

        std::vector<SymbolId> implInterfaceIds;
        for (const auto& ifaceName : interfaceNames) {
            SymbolId ifaceSymId = symTab_.lookup(ifaceName);
            if (ifaceSymId == 0) {
                reportError(DiagnosticCode::InvalidImplements,
                    "interface not found: " + ifaceName, makeLocation(0));
                continue;
            }
            
            Symbol* ifaceSym = symTab_.get(ifaceSymId);
            if (ifaceSym && ifaceSym->kind != SymbolKind::Interface) {
                reportError(DiagnosticCode::InvalidImplements,
                    "not an interface: " + ifaceName, makeLocation(0));
                continue;
            }
            
            pouSym->implementedInterfaces.push_back(ifaceSymId);
            implInterfaceIds.push_back(ifaceSymId);
        }
        fbImplements_[pouSymId] = implInterfaceIds;
    }
}

/**
 * @brief Check whether deriving from base would create a circular inheritance chain.
 * @details Walks the base-class chain starting from base, looking for derived.
 * @param derived The derived symbol being checked
 * @param base The candidate base symbol
 * @return true when base inherits, directly or indirectly, from derived
 */
bool DeclVisitor::wouldCreateCycle(SymbolId derived, SymbolId base) {
    // Simple cycle detection: walk up the inheritance chain from base
    SymbolId current = base;
    while (current != 0) {
        if (current == derived) return true;
        Symbol* sym = symTab_.get(current);
        if (!sym) break;
        current = sym->baseClassId;
    }
    return false;
}

/**
 * @brief Resolve a type name to its type symbol.
 * @details Prefers built-in types via the type table, then falls back to user
 * defined types through a recursive lookup.
 * @param name The type name to resolve
 * @return The resolved SymbolId, or 0 when not found
 */
SymbolId DeclVisitor::resolveTypeName(const std::string& name) {
    // First try built-in types
    TypeId builtinTypeId = symTab_.getTypeIdByName(name);
    if (builtinTypeId != 0) {
        return symTab_.lookupRecursive(name);
    }
    
    // Try user-defined types
    SymbolId typeSymId = symTab_.lookupRecursive(name);
    return typeSymId;
}

// ============================================================================
// Topological Sorting
// ============================================================================

/**
 * @brief Collect composition edges for one function block's variables and parameters.
 * @details Adds a graph edge from each FB-typed variable/parameter (including
 * method parameters) to the owning FB, so the containee is declared before its
 * container. Self-containment edges are skipped.
 * @param fb The function block symbol being analyzed
 * @param allFbs All known function block symbols
 * @param graph The dependency graph being built (out)
 * @param inDegree The in-degree map being built (out)
 */
void DeclVisitor::collectFbCompositionEdges(Symbol* fb, const std::vector<SymbolId>& allFbs,
                                            std::unordered_map<SymbolId, std::vector<SymbolId>>& graph,
                                            std::unordered_map<SymbolId, int>& inDegree) {
    const Scope* scope = symTab_.getScope(fb->scopeId);
    if (!scope) return;

    std::vector<SymbolId> scalarSymbols;
    for (const auto& [name, symId] : scope->symbols) {
        Symbol* sym = symTab_.get(symId);
        if (!sym) continue;
        if (sym->kind == SymbolKind::Method) {
            // Method parameters are stored on the Method symbol
            for (SymbolId paramId : sym->params) {
                scalarSymbols.push_back(paramId);
            }
            continue;
        }
        if (sym->kind == SymbolKind::Variable || sym->kind == SymbolKind::Parameter ||
            sym->kind == SymbolKind::StructMember) {
            scalarSymbols.push_back(symId);
        }
    }

    for (SymbolId symId : scalarSymbols) {
        Symbol* sym = symTab_.get(symId);
        if (!sym) continue;
        const TypeInfo* type = symTab_.getType(sym->typeId);
        if (!type || type->kind != TypeKind::FunctionBlock) continue;
        if (type->symbolId == 0) continue;
        SymbolId depFbId = type->symbolId;
        if (depFbId == fb->id) continue; // self-containment: never adds progress
        auto& edges = graph[depFbId];
        if (std::find(edges.begin(), edges.end(), fb->id) == edges.end()) {
            edges.push_back(fb->id); // dependency FB -> owner FB
            inDegree[fb->id]++;
        }
    }
}

/**
 * @brief Compute the topological orders and detect by-value dependency cycles.
 * @details The two sort functions above still produce the emission orders; this
 * pass independently walks a unified graph of structs and FBs to flag cycles
 * that would not compile in C++ (struct A contains struct B contains A, a
 * function block containing itself, or any mutual struct/FB containment).
 * Pointer and reference members never participate: they are not by-value
 * dependencies and pointer cycles are legal.
 */
void DeclVisitor::detectValueCycles() {
    std::unordered_map<SymbolId, std::vector<SymbolId>> graph;
    std::unordered_map<SymbolId, int> inDegree;
    std::vector<SymbolId> nodes;

    for (const auto& sym : symTab_.getSymbols()) {
        if (sym.kind == SymbolKind::FunctionBlock) {
            nodes.push_back(sym.id);
            inDegree[sym.id] = 0;
        } else if (sym.kind == SymbolKind::Type) {
            TypeInfo* typeInfo = symTab_.getType(sym.typeId);
            if (typeInfo && typeInfo->kind == TypeKind::Struct) {
                nodes.push_back(sym.id);
                inDegree[sym.id] = 0;
            }
        }
    }
    if (nodes.empty()) return;

    auto addEdge = [&](SymbolId payload, SymbolId container) {
        if (payload == 0) return;
        auto& edges = graph[payload];
        if (std::find(edges.begin(), edges.end(), container) == edges.end()) {
            edges.push_back(container);
            inDegree[container]++;
        }
    };

    // Struct -> container edges (member holds a struct/FB by value)
    for (const auto& structId : nodes) {
        Symbol* structSym = symTab_.get(structId);
        if (!structSym || structSym->kind != SymbolKind::Type) continue;
        for (SymbolId memberId : structSym->members) {
            Symbol* memberSym = symTab_.get(memberId);
            if (!memberSym) continue;
            addEdge(valuePayloadSymbol(memberSym->typeId), structId);
        }
    }

    // FB -> container edges (owned variables and parameters, including method
    // parameters, holding a struct/FB by value). Self-containment is a cycle
    // by value and must be reported, so it is NOT skipped here.
    for (const auto& fbId : nodes) {
        Symbol* fbSym = symTab_.get(fbId);
        if (!fbSym || fbSym->kind != SymbolKind::FunctionBlock) continue;
        const Scope* scope = symTab_.getScope(fbSym->scopeId);
        if (!scope) continue;
        std::vector<SymbolId> owned;
        for (const auto& [name, symId] : scope->symbols) {
            Symbol* sym = symTab_.get(symId);
            if (!sym) continue;
            if (sym->kind == SymbolKind::Method) {
                for (SymbolId paramId : sym->params) owned.push_back(paramId);
                continue;
            }
            if (sym->kind == SymbolKind::Variable || sym->kind == SymbolKind::Parameter ||
                sym->kind == SymbolKind::StructMember) {
                owned.push_back(symId);
            }
        }
        for (SymbolId ownedId : owned) {
            Symbol* sym = symTab_.get(ownedId);
            if (!sym) continue;
            addEdge(valuePayloadSymbol(sym->typeId), fbId);
        }
    }

    // Kahn's algorithm
    std::queue<SymbolId> q;
    for (const auto& [nodeId, deg] : inDegree) {
        if (deg == 0) q.push(nodeId);
    }
    size_t processed = 0;
    while (!q.empty()) {
        SymbolId nodeId = q.front();
        q.pop();
        processed++;
        for (SymbolId dep : graph[nodeId]) {
            inDegree[dep]--;
            if (inDegree[dep] == 0) q.push(dep);
        }
    }

    if (processed != nodes.size()) {
        // Name the types trapped in the cycle
        std::vector<SymbolId> cyclic;
        for (const auto& [nodeId, deg] : inDegree) {
            if (deg > 0) cyclic.push_back(nodeId);
        }
        std::sort(cyclic.begin(), cyclic.end());
        std::string msg = "circular by-value dependency between types: ";
        for (size_t i = 0; i < cyclic.size(); ++i) {
            if (i > 0) msg += ", ";
            Symbol* sym = symTab_.get(cyclic[i]);
            msg += sym ? sym->name : std::to_string(cyclic[i]);
        }
        reportError(DiagnosticCode::CircularDependency, msg, makeLocation(0));
    }
}

/**
 * @brief Compute the topological orders for FBs and structs.
 */
void DeclVisitor::computeTopoOrders() {
    topoSortFbs();
    topoSortStructs();
    detectValueCycles();
}

/**
 * @brief Return the symbol that a resolved type holds by value, or 0.
 * @details Unwraps ARRAY element types (arrays hold their elements by value);
 * POINTER TO and REF_TO are never followed because they are not by-value
 * dependencies (pointer cycles are legal). Named struct/FB references resolve
 * to their declaring symbol.
 * @param typeId The resolved type to inspect
 * @return The symbol of the by-value payload, or 0 when none applies
 */
SymbolId DeclVisitor::valuePayloadSymbol(TypeId typeId) const {
    while (typeId != 0) {
        const TypeInfo* type = symTab_.getType(typeId);
        if (!type) return 0;
        switch (type->kind) {
            case TypeKind::Array:
                typeId = type->elementTypeId;
                continue;
            case TypeKind::Struct:
            case TypeKind::FunctionBlock:
                return type->symbolId;
            case TypeKind::Pointer:
            case TypeKind::Reference:
                return 0;
            default:
                return 0;
        }
    }
    return 0;
}

/**
 * @brief Topologically sort function blocks by inheritance and composition.
 * @details Builds base-to-derived and containment edges (base or FB-typed
 * variables), then runs Kahn's algorithm. A cycle results in a circular
 * dependency diagnostic.
 */
void DeclVisitor::topoSortFbs() {
    // Build dependency graph for FBs
    std::unordered_map<SymbolId, std::vector<SymbolId>> graph;
    std::unordered_map<SymbolId, int> inDegree;
    std::vector<SymbolId> allFbs;
    
    // Collect all FBs
    for (const auto& sym : symTab_.getSymbols()) {
        if (sym.kind == SymbolKind::FunctionBlock) {
            allFbs.push_back(sym.id);
            inDegree[sym.id] = 0;
        }
    }
    
    // Build edges: derived -> base (base must come before derived)
    for (const auto& fbId : allFbs) {
        Symbol* fb = symTab_.get(fbId);
        if (fb && fb->baseClassId != 0) {
            graph[fb->baseClassId].push_back(fbId); // base -> derived
            inDegree[fbId]++;
        }
        // NOTE: Interface dependencies are NOT added to the FB topological sort
        // because interfaces are not FBs and don't affect FB generation order.
        // Interface implementation is a compile-time check, not a runtime dependency.
    }
    
    // Composition edges: an FB typed as member or parameter must be declared
    // before the FB that contains it (mirrors the legacy buildFBDependencies).
    // Iterate over the FB-local symbols (variables and parameters, including
    // method parameters) and add an edge from the referred FB to the owner.
    for (const auto& fbId : allFbs) {
        Symbol* fb = symTab_.get(fbId);
        if (!fb) continue;
        DeclVisitor::collectFbCompositionEdges(fb, allFbs, graph, inDegree);
    }
    
    // Kahn's algorithm
    std::queue<SymbolId> q;
    for (const auto& fbId : allFbs) {
        if (inDegree[fbId] == 0) {
            q.push(fbId);
        }
    }
    
    while (!q.empty()) {
        SymbolId fbId = q.front();
        q.pop();
        fbTopoOrder_.push_back(fbId);
        
        for (SymbolId dep : graph[fbId]) {
            inDegree[dep]--;
            if (inDegree[dep] == 0) {
                q.push(dep);
            }
        }
    }
    
    // Check for cycles: covered by detectValueCycles() with a dedicated
    // CircularDependency diagnostic that names the involved types.
    (void)allFbs;
}

/**
 * @brief Topologically sort structs by their member nesting.
 * @details Adds an edge from each member struct to its parent struct and runs
 * Kahn's algorithm. A cycle results in a circular dependency warning.
 */
void DeclVisitor::topoSortStructs() {
    // Build dependency graph for STRUCTs
    std::unordered_map<SymbolId, std::vector<SymbolId>> graph;
    std::unordered_map<SymbolId, int> inDegree;
    std::vector<SymbolId> allStructs;
    
    // Collect all STRUCTs
    for (const auto& sym : symTab_.getSymbols()) {
        if (sym.kind == SymbolKind::Type) {
            // Check if this is a struct type by looking at its TypeInfo
            TypeInfo* typeInfo = symTab_.getType(sym.typeId);
            if (typeInfo && typeInfo->kind == TypeKind::Struct) {
                allStructs.push_back(sym.id);
                inDegree[sym.id] = 0;
            }
        }
    }
    
    // Build edges based on member types
    for (const auto& structId : allStructs) {
        Symbol* structSym = symTab_.get(structId);
        if (!structSym) continue;
        
        for (SymbolId memberId : structSym->members) {
            Symbol* memberSym = symTab_.get(memberId);
            if (!memberSym) continue;
            
            // Get the type of the member
            TypeInfo* memberTypeInfo = symTab_.getType(memberSym->typeId);
            if (memberTypeInfo && memberTypeInfo->kind == TypeKind::Struct) {
                SymbolId memberStructSymId = memberTypeInfo->symbolId;
                if (memberStructSymId != 0 && memberStructSymId != structId) {
                    graph[memberStructSymId].push_back(structId); // member struct -> parent struct
                    inDegree[structId]++;
                }
            }
        }
    }
    
    // Kahn's algorithm
    std::queue<SymbolId> q;
    for (const auto& structId : allStructs) {
        if (inDegree[structId] == 0) {
            q.push(structId);
        }
    }
    
    while (!q.empty()) {
        SymbolId structId = q.front();
        q.pop();
        structTopoOrder_.push_back(structId);
        
        for (SymbolId dep : graph[structId]) {
            inDegree[dep]--;
            if (inDegree[dep] == 0) {
                q.push(dep);
            }
        }
    }
    
    // Check for cycles: covered by detectValueCycles() with a dedicated
    // CircularDependency diagnostic that names the involved types.
    (void)allStructs;
}

// ============================================================================
// Type Resolution
// ============================================================================

/**
 * @brief Resolve a type reference to a TypeId.
 * @details Handles POINTER TO, REF_TO and ARRAY wrappers by building the
 * corresponding TypeInfo and registering it; plain references resolve to a base
 * or named type. The line is threaded to the named-type resolution so unknown
 * types are reported on the declaration that references them.
 * @param typeRef The type reference to resolve
 * @param line The source line of the referencing declaration (0 when unknown)
 * @return The resolved TypeId
 */
TypeId DeclVisitor::resolveTypeRef(const TypeRef& typeRef, uint32_t line) {
    // Handle POINTER TO
    if (typeRef.isPointer) {
        TypeId pointedTypeId = 0;
        std::string baseName;
        if (typeRef.base == BaseType::NAMED) {
            pointedTypeId = resolveNamedType(typeRef.name, line);
            baseName = typeRef.name;
        } else {
            pointedTypeId = resolveBaseType(typeRef.base);
            baseName = baseTypeName(typeRef.base);
        }
        // Create proper Pointer TypeInfo
        TypeInfo ptrType;
        ptrType.kind = TypeKind::Pointer;
        ptrType.pointedTypeId = pointedTypeId;
        ptrType.name = "POINTER TO " + baseName;
        ptrType.isNumeric = false;
        if (const TypeInfo* pointedType = symTab_.getType(pointedTypeId)) {
            ptrType.sizeInBytes = pointedType->sizeInBytes;
        }
        return symTab_.registerPointerType(ptrType);
    }
    
    // Handle REF_TO
    if (typeRef.isRefTo) {
        TypeId pointedTypeId = 0;
        std::string baseName;
        if (typeRef.base == BaseType::NAMED) {
            pointedTypeId = resolveNamedType(typeRef.name, line);
            baseName = typeRef.name;
        } else {
            pointedTypeId = resolveBaseType(typeRef.base);
            baseName = baseTypeName(typeRef.base);
        }
        // Create proper Reference TypeInfo
        TypeInfo refType;
        refType.kind = TypeKind::Reference;
        refType.pointedTypeId = pointedTypeId;
        refType.name = "REF_TO " + baseName;
        refType.isNumeric = false;
        if (const TypeInfo* pointedType = symTab_.getType(pointedTypeId)) {
            refType.sizeInBytes = pointedType->sizeInBytes;
        }
        return symTab_.registerReferenceType(refType);
    }
    
    // Handle ARRAY
    if (!typeRef.arrayDims.empty()) {
        TypeId elementTypeId = 0;
        if (typeRef.base == BaseType::NAMED) {
            elementTypeId = resolveNamedType(typeRef.name, line);
        } else {
            elementTypeId = resolveBaseType(typeRef.base);
        }
        // Create proper Array TypeInfo
        TypeInfo arrayType;
        arrayType.kind = TypeKind::Array;
        arrayType.elementTypeId = elementTypeId;
        arrayType.name = "ARRAY";
        arrayType.isNumeric = false;
        size_t elemCount = 1;
        for (const auto& dim : typeRef.arrayDims) {
            ArrayDimInfo dimInfo;
            dimInfo.isConstant = true;
            int low = 0;
            int high = 9;
            if (!extractArrayBoundValue(dim.low, low)) low = 0;
            if (!extractArrayBoundValue(dim.high, high)) high = 9;
            dimInfo.low = low;
            dimInfo.high = high;
            arrayType.dimensions.push_back(dimInfo);
            elemCount *= static_cast<size_t>(high - low + 1);
        }
        if (const TypeInfo* elemType = symTab_.getType(elementTypeId)) {
            arrayType.sizeInBytes = elemType->sizeInBytes * elemCount;
        }
        return symTab_.registerArrayType(arrayType);
    }
    
    // Handle base types
    if (typeRef.base != BaseType::NAMED) {
        return resolveBaseType(typeRef.base);
    }
    
    // Handle named types (user-defined)
    return resolveNamedType(typeRef.name, line);
}

/**
 * @brief Resolve an elementary base type to its registered TypeId.
 * @param baseType The base type enumerator
 * @return The TypeId of the built-in type, or 0 when unknown
 */
TypeId DeclVisitor::resolveBaseType(BaseType baseType) {
    static const std::unordered_map<BaseType, std::string> baseTypeNames = {
        {BaseType::BOOL, "BOOL"}, {BaseType::SINT, "SINT"}, {BaseType::INT, "INT"},
        {BaseType::DINT, "DINT"}, {BaseType::LINT, "LINT"}, {BaseType::USINT, "USINT"},
        {BaseType::UINT, "UINT"}, {BaseType::UDINT, "UDINT"}, {BaseType::ULINT, "ULINT"},
        {BaseType::REAL, "REAL"}, {BaseType::LREAL, "LREAL"}, {BaseType::BYTE, "BYTE"},
        {BaseType::WORD, "WORD"}, {BaseType::DWORD, "DWORD"}, {BaseType::LWORD, "LWORD"},
        {BaseType::STRING, "STRING"}, {BaseType::WSTRING, "WSTRING"},
        {BaseType::TIME, "TIME"}, {BaseType::DATE, "DATE"}, {BaseType::TOD, "TOD"},
        {BaseType::DT, "DT"}, {BaseType::VOID, "VOID"}
    };
    
    auto it = baseTypeNames.find(baseType);
    if (it != baseTypeNames.end()) {
        return symTab_.getTypeIdByName(it->second);
    }
    return 0;
}

/**
 * @brief Return the canonical ST name of an elementary base type.
 * @param baseType The base type enumerator
 * @return The base type name, or "UNKNOWN" when not recognized
 */
std::string DeclVisitor::baseTypeName(BaseType baseType) {
    static const std::unordered_map<BaseType, std::string> baseTypeNames = {
        {BaseType::BOOL, "BOOL"}, {BaseType::SINT, "SINT"}, {BaseType::INT, "INT"},
        {BaseType::DINT, "DINT"}, {BaseType::LINT, "LINT"}, {BaseType::USINT, "USINT"},
        {BaseType::UINT, "UINT"}, {BaseType::UDINT, "UDINT"}, {BaseType::ULINT, "ULINT"},
        {BaseType::REAL, "REAL"}, {BaseType::LREAL, "LREAL"}, {BaseType::BYTE, "BYTE"},
        {BaseType::WORD, "WORD"}, {BaseType::DWORD, "DWORD"}, {BaseType::LWORD, "LWORD"},
        {BaseType::STRING, "STRING"}, {BaseType::WSTRING, "WSTRING"},
        {BaseType::TIME, "TIME"}, {BaseType::DATE, "DATE"}, {BaseType::TOD, "TOD"},
        {BaseType::DT, "DT"}, {BaseType::VOID, "VOID"}
    };
    auto it = baseTypeNames.find(baseType);
    return it != baseTypeNames.end() ? it->second : "UNKNOWN";
}

/**
 * @brief Resolve a user-defined named type to its TypeId.
 * @details Searches only the global (type) namespace via lookupGlobal so a
 * member variable or struct field with the same name (case-insensitive) can
 * never shadow a type. Accepts Type, FunctionBlock, Program and Interface
 * symbols as valid type declarations; unknown names are reported as an
 * InvalidTypeName diagnostic, pointing at the declaration that referenced the
 * type when a line is available.
 * @param name The type name to resolve
 * @param line The source line of the referencing declaration (0 when unknown)
 * @return The resolved TypeId, or 0 when the type is unknown
 */
TypeId DeclVisitor::resolveNamedType(const std::string& name, uint32_t line) {
    // Try to find the type symbol in the global (type) namespace only
    SymbolId typeSymId = symTab_.lookupGlobal(name);
    if (typeSymId != 0) {
        Symbol* typeSym = symTab_.get(typeSymId);
        // Accept Type, FunctionBlock, Program, and Interface as valid types
        // for variable declarations
        if (typeSym && (typeSym->kind == SymbolKind::Type || 
                        typeSym->kind == SymbolKind::FunctionBlock ||
                        typeSym->kind == SymbolKind::Program ||
                        typeSym->kind == SymbolKind::Interface)) {
            return typeSym->typeId;
        }
    }
    
    // Type not found - report error
    reportError(DiagnosticCode::InvalidTypeName, "unknown type: " + name, makeLocation(line));
    return 0;
}

// ============================================================================
// Helpers
// ============================================================================

/**
 * @brief Build a SourceLocation tagged as coming from the input file.
 * @param line The source line number
 * @param col The source column number (defaults to 0)
 * @return The constructed source location
 */
SourceLocation DeclVisitor::makeLocation(uint32_t line, uint32_t col) const {
    SourceLocation loc;
    loc.line = line;
    loc.column = col;
    loc.fileName = "<input>";
    return loc;
}

/**
 * @brief Report an error through the diagnostics collector.
 * @param code The diagnostic code
 * @param msg The diagnostic message
 * @param loc The source location of the problem
 */
void DeclVisitor::reportError(DiagnosticCode code, const std::string& msg, const SourceLocation& loc) {
    diag_.addError(code, msg, loc);
}

/**
 * @brief Report a warning through the diagnostics collector.
 * @param code The diagnostic code
 * @param msg The diagnostic message
 * @param loc The source location of the problem
 */
void DeclVisitor::reportWarning(DiagnosticCode code, const std::string& msg, const SourceLocation& loc) {
    diag_.addWarning(code, msg, loc);
}

} // namespace st2cpp::semantic