/**
 * @file SymbolTable.cpp
 * @brief Symbol table implementation
 * @author Salvatore Bamundo
 * @date 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#include "semantic/SymbolTable.h"
#include "semantic/TypeSystem.h"

namespace st2cpp::semantic {

void SymbolTable::registerIecConversionFunctions()
{
    // IEC 61131-3 conversion functions (X_TO_Y) implemented by the runtime
    // conversions.hpp header (undoCore). Keep this list in sync with that
    // header: the code generator emits the function name verbatim.
    static const char* const kIecConversions[] = {
        "BCD_TO_INT", "BCD_TO_USINT", "BOOL_TO_BYTE", "BOOL_TO_DINT", "BOOL_TO_DWORD", "BOOL_TO_INT",
        "BOOL_TO_LINT", "BOOL_TO_LWORD", "BOOL_TO_SINT", "BOOL_TO_STRING", "BOOL_TO_UDINT", "BOOL_TO_UINT",
        "BOOL_TO_ULINT", "BOOL_TO_USINT", "BOOL_TO_WORD", "BYTE_TO_BOOL", "BYTE_TO_DINT", "BYTE_TO_DWORD",
        "BYTE_TO_INT", "BYTE_TO_LINT", "BYTE_TO_LWORD", "BYTE_TO_SINT", "BYTE_TO_STRING", "BYTE_TO_UDINT",
        "BYTE_TO_UINT", "BYTE_TO_ULINT", "BYTE_TO_USINT", "BYTE_TO_WORD", "DATE_TO_DINT", "DATE_TO_DT",
        "DATE_TO_INT", "DATE_TO_LINT", "DATE_TO_SINT", "DATE_TO_STRING", "DATE_TO_TOD", "DATE_TO_UDINT",
        "DATE_TO_UINT", "DATE_TO_ULINT", "DATE_TO_USINT", "DINT_TO_BOOL", "DINT_TO_BYTE", "DINT_TO_DATE",
        "DINT_TO_DT", "DINT_TO_DWORD", "DINT_TO_INT", "DINT_TO_LINT", "DINT_TO_LREAL", "DINT_TO_LWORD",
        "DINT_TO_REAL", "DINT_TO_SINT", "DINT_TO_STRING", "DINT_TO_TIME", "DINT_TO_TOD", "DINT_TO_UDINT",
        "DINT_TO_UINT", "DINT_TO_ULINT", "DINT_TO_USINT", "DINT_TO_WORD", "DT_TO_DATE", "DT_TO_DINT",
        "DT_TO_INT", "DT_TO_LINT", "DT_TO_SINT", "DT_TO_STRING", "DT_TO_TOD", "DT_TO_UDINT",
        "DT_TO_UINT", "DT_TO_ULINT", "DT_TO_USINT", "DWORD_TO_BOOL", "DWORD_TO_BYTE", "DWORD_TO_DINT",
        "DWORD_TO_INT", "DWORD_TO_LINT", "DWORD_TO_LWORD", "DWORD_TO_SINT", "DWORD_TO_STRING", "DWORD_TO_UDINT",
        "DWORD_TO_UINT", "DWORD_TO_ULINT", "DWORD_TO_USINT", "DWORD_TO_WORD", "INT_TO_BCD", "INT_TO_BOOL",
        "INT_TO_BYTE", "INT_TO_DATE", "INT_TO_DINT", "INT_TO_DT", "INT_TO_DWORD", "INT_TO_LINT",
        "INT_TO_LREAL", "INT_TO_LWORD", "INT_TO_REAL", "INT_TO_SINT", "INT_TO_STRING", "INT_TO_TIME",
        "INT_TO_TOD", "INT_TO_UDINT", "INT_TO_UINT", "INT_TO_ULINT", "INT_TO_USINT", "INT_TO_WORD",
        "LINT_TO_BOOL", "LINT_TO_BYTE", "LINT_TO_DATE", "LINT_TO_DINT", "LINT_TO_DT", "LINT_TO_DWORD",
        "LINT_TO_INT", "LINT_TO_LREAL", "LINT_TO_LWORD", "LINT_TO_REAL", "LINT_TO_SINT", "LINT_TO_STRING",
        "LINT_TO_TIME", "LINT_TO_TOD", "LINT_TO_UDINT", "LINT_TO_UINT", "LINT_TO_ULINT", "LINT_TO_USINT",
        "LINT_TO_WORD", "LREAL_TO_DINT", "LREAL_TO_INT", "LREAL_TO_LINT", "LREAL_TO_REAL", "LREAL_TO_SINT",
        "LREAL_TO_STRING", "LREAL_TO_UDINT", "LREAL_TO_UINT", "LREAL_TO_ULINT", "LREAL_TO_USINT", "LWORD_TO_BOOL",
        "LWORD_TO_BYTE", "LWORD_TO_DINT", "LWORD_TO_DWORD", "LWORD_TO_INT", "LWORD_TO_LINT", "LWORD_TO_SINT",
        "LWORD_TO_STRING", "LWORD_TO_UDINT", "LWORD_TO_UINT", "LWORD_TO_ULINT", "LWORD_TO_USINT", "LWORD_TO_WORD",
        "REAL_TO_DINT", "REAL_TO_INT", "REAL_TO_LINT", "REAL_TO_LREAL", "REAL_TO_SINT", "REAL_TO_STRING",
        "REAL_TO_UDINT", "REAL_TO_UINT", "REAL_TO_ULINT", "REAL_TO_USINT", "SINT_TO_BOOL", "SINT_TO_BYTE",
        "SINT_TO_DATE", "SINT_TO_DINT", "SINT_TO_DT", "SINT_TO_DWORD", "SINT_TO_INT", "SINT_TO_LINT",
        "SINT_TO_LREAL", "SINT_TO_LWORD", "SINT_TO_REAL", "SINT_TO_STRING", "SINT_TO_TIME", "SINT_TO_TOD",
        "SINT_TO_UDINT", "SINT_TO_UINT", "SINT_TO_ULINT", "SINT_TO_USINT", "SINT_TO_WORD", "STRING_TO_BOOL",
        "STRING_TO_BYTE", "STRING_TO_DINT", "STRING_TO_DWORD", "STRING_TO_INT", "STRING_TO_LINT", "STRING_TO_LREAL",
        "STRING_TO_LWORD", "STRING_TO_REAL", "STRING_TO_SINT", "STRING_TO_UDINT", "STRING_TO_UINT", "STRING_TO_ULINT",
        "STRING_TO_USINT", "STRING_TO_WORD", "STRING_TO_WSTRING", "TIME_TO_DINT", "TIME_TO_INT", "TIME_TO_LINT",
        "TIME_TO_SINT", "TIME_TO_STRING", "TIME_TO_TOD", "TIME_TO_UDINT", "TIME_TO_UINT", "TIME_TO_ULINT",
        "TIME_TO_USINT", "TOD_TO_DATE", "TOD_TO_DINT", "TOD_TO_INT", "TOD_TO_LINT", "TOD_TO_SINT",
        "TOD_TO_STRING", "TOD_TO_TIME", "TOD_TO_UDINT", "TOD_TO_UINT", "TOD_TO_ULINT", "TOD_TO_USINT",
        "UDINT_TO_BOOL", "UDINT_TO_BYTE", "UDINT_TO_DATE", "UDINT_TO_DINT", "UDINT_TO_DT", "UDINT_TO_DWORD",
        "UDINT_TO_LREAL", "UDINT_TO_LWORD", "UDINT_TO_REAL", "UDINT_TO_SINT", "UDINT_TO_STRING", "UDINT_TO_TIME",
        "UDINT_TO_TOD", "UDINT_TO_UINT", "UDINT_TO_ULINT", "UDINT_TO_USINT", "UDINT_TO_WORD", "UINT_TO_BOOL",
        "UINT_TO_BYTE", "UINT_TO_DATE", "UINT_TO_DT", "UINT_TO_DWORD", "UINT_TO_INT", "UINT_TO_LREAL",
        "UINT_TO_LWORD", "UINT_TO_REAL", "UINT_TO_SINT", "UINT_TO_STRING", "UINT_TO_TIME", "UINT_TO_TOD",
        "UINT_TO_UDINT", "UINT_TO_ULINT", "UINT_TO_USINT", "UINT_TO_WORD", "ULINT_TO_BOOL", "ULINT_TO_BYTE",
        "ULINT_TO_DATE", "ULINT_TO_DT", "ULINT_TO_DWORD", "ULINT_TO_LINT", "ULINT_TO_LREAL", "ULINT_TO_LWORD",
        "ULINT_TO_REAL", "ULINT_TO_SINT", "ULINT_TO_STRING", "ULINT_TO_TIME", "ULINT_TO_TOD", "ULINT_TO_UDINT",
        "ULINT_TO_UINT", "ULINT_TO_USINT", "ULINT_TO_WORD", "USINT_TO_BCD", "USINT_TO_BOOL", "USINT_TO_BYTE",
        "USINT_TO_DATE", "USINT_TO_DT", "USINT_TO_DWORD", "USINT_TO_LREAL", "USINT_TO_LWORD", "USINT_TO_REAL",
        "USINT_TO_SINT", "USINT_TO_STRING", "USINT_TO_TIME", "USINT_TO_TOD", "USINT_TO_UDINT", "USINT_TO_UINT",
        "USINT_TO_ULINT", "USINT_TO_WORD", "WORD_TO_BOOL", "WORD_TO_BYTE", "WORD_TO_DINT", "WORD_TO_DWORD",
        "WORD_TO_INT", "WORD_TO_LINT", "WORD_TO_LWORD", "WORD_TO_SINT", "WORD_TO_STRING", "WORD_TO_UDINT",
        "WORD_TO_UINT", "WORD_TO_ULINT", "WORD_TO_USINT", "WSTRING_TO_STRING",
    };

    for (const char* conv : kIecConversions) {
        const std::string name(conv);
        const size_t sep = name.rfind("_TO_");
        if (sep == std::string::npos) {
            continue;
        }
        const TypeId srcTypeId = getTypeIdByName(name.substr(0, sep));
        const TypeId dstTypeId = getTypeIdByName(name.substr(sep + 4));

        SymbolId symId = declare(name, SymbolKind::Function);
        if (symId == 0) {
            continue;
        }
        Symbol* sym = get(symId);
        if (!sym) {
            continue;
        }
        sym->returnTypeId = dstTypeId;
        if (srcTypeId != 0) {
            SymbolId paramId = declare(name + "_VALUE", SymbolKind::Parameter, srcTypeId);
            if (paramId != 0) {
                Symbol* param = get(paramId);
                if (param) {
                    param->typeId = srcTypeId;
                    param->paramDir = ParamDir::Input;
                    param->scopeId = globalScopeId_;
                }
                sym->params.push_back(paramId);
            }
        }
    }
}

} // namespace st2cpp::semantic