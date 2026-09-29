/**
 * @file ProcessImage.h
 * @brief Process-image sizing and the AT address allocator
 * @details A variable declared with an `AT` address either pins its storage to a
 * fixed location in the process image (`%IX0.0`) or leaves it to the compiler
 * with a placeholder (`%IX*`). `AddressAllocator` owns the second case: it keeps
 * one bitmap per memory area, marks the fixed addresses as occupied, and hands
 * out the first free aligned position to each placeholder. `ProcessImageAnalyzer`
 * is the read-only counterpart: it walks a translation unit and reports how
 * large each area needs to be, so the emitter can size the generated image
 * without guessing.
 *
 * Both were nested in CodeGenerator; they are free of the output streams and of
 * the scope stack, so they live on their own.
 *
 * @author Salvatore Bamundo
 * @date 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#pragma once

#include "ast/AST.h"
#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

/**
 * @brief Configuration for Process Image sizing
 */
struct ProcessImageConfig
{
   size_t inputBytes = 1024;
   size_t outputBytes = 1024;
   size_t markerBytes = 1024;
   bool autoDetect = true;   // Auto-detect from addresses used
   bool useGlobalPI = false; // Project-style: shared process image
   std::string instanceName = "processImage";
};

/**
 * @brief Parse an IEC address string into an AddressExpr.
 * @param addrStr The address string (e.g. "%IX0.0", "%QW2", "%MW10", "%IX*")
 * @param out The AddressExpr to fill
 * @return true if parsing succeeded, false otherwise
 */
bool parseAddressString(const std::string& addrStr, AddressExpr& out);

// ============================================================================
//  Address Allocator - allocates AT addresses with the '*' placeholder
// ============================================================================

/**
 * @brief Manages allocation of AT addresses with placeholder '*'
 *
 * Handles:
 * - Fixed addresses (e.g., %IX0.0) - marked as occupied
 * - Placeholder addresses (e.g., %IX*) - allocated to first free position
 * - Proper alignment for multi-byte types (WORD aligned to 2, DWORD to 4, etc.)
 * - Memory region expansion when needed
 */
class AddressAllocator
{
public:
   /**
    * @brief Result of an address allocation
    */
   struct AllocationResult
   {
      int byteOffset;
      int bitOffset; // -1 if not a bit access
      bool success;
   };

   /**
    * @brief Information about a memory region
    */
   struct MemoryRegion
   {
      size_t size;                    // Current size in bytes
      std::vector<bool> byteOccupied; // true if byte is occupied
      std::vector<bool> bitOccupied;  // true if bit is occupied (for BIT access)
   };

   AddressAllocator();

   /**
    * @brief Mark a fixed address as occupied
    * @param addr The address to mark
    * @param sizeInBytes Size of the variable in bytes
    */
   void markFixedAddress(const AddressExpr& addr, int sizeInBytes);

   /**
    * @brief Allocate a placeholder address
    * @param area Memory area (INPUT, OUTPUT, MARKER)
    * @param qualifier Address qualifier (BIT, BYTE, WORD, DWORD, LWORD)
    * @param sizeInBytes Size of the variable in bytes
    * @param alignment Required alignment in bytes
    * @return AllocationResult with the allocated offset
    */
   AllocationResult allocatePlaceholder(AddressExpr::AddressType area,
                                        AddressExpr::AddressQualifier qualifier,
                                        int sizeInBytes,
                                        int alignment);

   /**
    * @brief Get the current size of a memory region
    * @param area Memory area
    * @return Size in bytes
    */
   size_t getRegionSize(AddressExpr::AddressType area) const;

   /**
    * @brief Check if the allocator has any allocations
    */
   bool hasAllocations() const { return !m_regions.empty(); }
private:
   std::unordered_map<AddressExpr::AddressType, MemoryRegion> m_regions;

   /**
    * @brief Ensure a region exists with at least the specified size
    */
   void ensureRegion(AddressExpr::AddressType area, size_t minSize);

   /**
    * @brief Expand a region to accommodate more allocations
    */
   void expandRegion(AddressExpr::AddressType area, size_t newSize);

   /**
    * @brief Align a value to the next multiple of alignment
    */
   size_t alignUp(size_t value, size_t alignment) const;

   /**
    * @brief Get the size of a region type
    */
   size_t getDefaultRegionSize(AddressExpr::AddressType area) const;
};

// ============================================================================
//  Process Image Analyzer - detects the required image sizes from the AST
// ============================================================================

/**
 * @brief Analyzer for detecting Process Image sizes from AST
 */
class ProcessImageAnalyzer
{
public:
   struct AddressInfo
   {
      AddressExpr::AddressType type;
      int maxByteOffset = 0;
      int maxBitOffset = 0;
      bool hasBitAccess = false;
   };

   void analyze(const TranslationUnit& tu);
   ProcessImageConfig getRecommendedConfig() const;
   bool hasAddresses() const { return !m_addressInfos.empty(); }

private:
   std::vector<AddressInfo> m_addressInfos;
   std::unordered_map<AddressExpr::AddressType, AddressInfo> m_typeMap;

   void findAddresses(const std::shared_ptr<Stmt>& stmt);
   void findAddressesInExpr(const std::shared_ptr<Expr>& expr);
   void updateMaxOffset(const AddressExpr& addr);
   size_t nextPowerOfTwo(size_t n) const;
};
