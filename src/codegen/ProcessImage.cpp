/**
 * @file ProcessImage.cpp
 * @brief Process-image sizing and AT address allocation
 * @author Salvatore Bamundo
 * @date 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#include "codegen/ProcessImage.h"
#include <algorithm>

// ============================================================================
//  IEC address parsing
// ============================================================================

/**
 * @brief Helper function to parse an IEC address string into an AddressExpr
 * 
 * @param addrStr The address string (e.g., "%IX0.0", "%QW2", "%MW10", "%IX*")
 * @param out The AddressExpr to fill
 * @return true if parsing succeeded, false otherwise
 */
bool parseAddressString(const std::string& addrStr, AddressExpr& out)
{
   if (addrStr.size() < 3) {
      return false;
   }

   if (addrStr[0] != '%') {
      return false;
   }

   // Check for placeholder '*'
   size_t starPos = addrStr.find('*');
   out.isPlaceholder = (starPos != std::string::npos);

   // Determine type
   switch (addrStr[1]) {
   case 'I':
      out.type = AddressExpr::AddressType::INPUT;
      break;
   case 'Q':
      out.type = AddressExpr::AddressType::OUTPUT;
      break;
   case 'M':
      out.type = AddressExpr::AddressType::MARKER;
      break;
   default:
      return false;
   }

   if (out.isPlaceholder && addrStr.size() == 3) {
      // There is not the third element (X, B, W...)
      out.qualifier = AddressExpr::AddressQualifier::BIT;
      out.qualifierInferred = true;
      out.byteOffset = 0;
      out.bitOffset = -1;
      out.rawText = addrStr;
      return true;
   }

   // Determine qualifier
   switch (addrStr[2]) {
   case 'X':
      out.qualifier = AddressExpr::AddressQualifier::BIT;
      break;
   case 'B':
      out.qualifier = AddressExpr::AddressQualifier::BYTE;
      break;
   case 'W':
      out.qualifier = AddressExpr::AddressQualifier::WORD;
      break;
   case 'D':
      out.qualifier = AddressExpr::AddressQualifier::DWORD;
      break;
   case 'L':
      out.qualifier = AddressExpr::AddressQualifier::LWORD;
      break;
   default:
      return false;
   }

   if (out.isPlaceholder) {
      // Placeholder address: no offsets to parse
      out.byteOffset = 0;
      out.bitOffset = -1;
      out.rawText = addrStr;
      return true;
   }

   // Parse offsets for fixed addresses
   size_t pos = 3;
   std::string numStr;
   while (pos < addrStr.size() && std::isdigit(addrStr[pos])) {
      numStr += addrStr[pos++];
   }
   if (numStr.empty()) {
      return false;
   }
   out.byteOffset = std::stoi(numStr);

   if (pos < addrStr.size() && addrStr[pos] == '.') {
      pos++;
      numStr.clear();
      while (pos < addrStr.size() && std::isdigit(addrStr[pos])) {
         numStr += addrStr[pos++];
      }
      if (numStr.empty()) {
         return false;
      }
      out.bitOffset = std::stoi(numStr);
   } else {
      out.bitOffset = -1;
   }

   out.rawText = addrStr;
   return true;
}

// ============================================================================
//  AddressAllocator Implementation
// ============================================================================

/**
 * @brief Construct a new AddressAllocator object
 *
 * Initializes memory regions for INPUT, OUTPUT, and MARKER with default sizes.
 */
AddressAllocator::AddressAllocator() {}

/**
 * @brief Get the default size for a memory region
 *
 * @param area The memory area type
 * @return size_t Default size in bytes (1024 for all regions)
 */
size_t AddressAllocator::getDefaultRegionSize(AddressExpr::AddressType area) const
{
   // // Default size for each region (like Siemens S7-1200)
   // switch (area) {
   // case AddressExpr::AddressType::INPUT:
   //    return 1024;
   // case AddressExpr::AddressType::OUTPUT:
   //    return 1024;
   // case AddressExpr::AddressType::MARKER:
   //    return 1024;
   // default:
   //    return 1024;
   // }
   return 0;
}

/**
 * @brief Align a value to the next multiple of alignment
 *
 * @param value The value to align
 * @param alignment The alignment boundary
 * @return size_t Aligned value
 */
size_t AddressAllocator::alignUp(size_t value, size_t alignment) const
{
   if (alignment == 0) {
      return value;
   }
   return ((value + alignment - 1) / alignment) * alignment;
}

/**
 * @brief Ensure that a memory region exists and has at least the specified size
 *
 * @param area The memory area type
 * @param minSize The minimum required size in bytes
 */
void AddressAllocator::ensureRegion(AddressExpr::AddressType area, size_t minSize)
{
   auto it = m_regions.find(area);
   if (it == m_regions.end()) {
      MemoryRegion region;
      region.size = 0;
      region.byteOccupied.assign(0, false);
      region.bitOccupied.assign(0, false);
      m_regions[area] = std::move(region);
      it = m_regions.find(area);
   }
   if (it != m_regions.end() && it->second.size < minSize) {
      expandRegion(area, minSize);
   }
}

/**
 * @brief Expand a memory region to accommodate more allocations
 *
 * Ensures the region has at least the specified size. Uses power-of-two
 * growth for efficient memory usage.
 *
 * @param area The memory area type
 * @param newSize The minimum required size in bytes
 */
void AddressAllocator::expandRegion(AddressExpr::AddressType area, size_t newSize)
{
   auto it = m_regions.find(area);
   if (it == m_regions.end()) {
      ensureRegion(area, newSize);
      return;
   }

   // Ensure newSize is at least 1 byte
   if (newSize == 0) {
      newSize = 1;
   }

   size_t currentSize = it->second.size;
   size_t targetSize = currentSize;

   if (currentSize == 0) {
      // First expansion: start from 1 and double until we reach newSize
      targetSize = 1;
      while (targetSize < newSize) {
         targetSize <<= 1;
      }
   } else {
      // Grow by doubling or aligning to 8-byte boundary
      while (targetSize < newSize) {
         if (targetSize * 2 >= newSize) {
            targetSize *= 2;
         } else {
            targetSize = alignUp(newSize, 8);
         }
      }
   }

   // Ensure targetSize is at least 1
   if (targetSize == 0) {
      targetSize = 1;
   }

   // Resize the vectors
   it->second.byteOccupied.resize(targetSize, false);
   it->second.bitOccupied.resize(targetSize * 8, false);
   it->second.size = targetSize;
}

/**
 * @brief Mark a fixed address as occupied in the memory region
 *
 * Marks the specified address and all its bits as occupied.
 * Handles both BIT and byte-aligned accesses.
 *
 * @param addr The address to mark
 * @param sizeInBytes Size of the variable in bytes
 */
void AddressAllocator::markFixedAddress(const AddressExpr& addr, int sizeInBytes)
{
   // Validate input parameters
   if (sizeInBytes <= 0) {
      return;
   }

   AddressExpr::AddressType area = addr.type;

   // Validate byte offset
   if (addr.byteOffset < 0) {
      return;
   }

   // Ensure the region exists with enough space
   size_t neededSize = static_cast<size_t>(addr.byteOffset + sizeInBytes);
   ensureRegion(area, neededSize);

   auto it = m_regions.find(area);
   if (it == m_regions.end()) {
      return;
   }

   auto& region = it->second;

   // Check if the offset is within the region
   if (static_cast<size_t>(addr.byteOffset) >= region.size) {
      return;
   }

   if (addr.qualifier == AddressExpr::AddressQualifier::BIT) {
      // BIT access: mark a specific bit
      if (addr.bitOffset < 0 || addr.bitOffset >= 8) {
         return;
      }

      size_t bitIndex = static_cast<size_t>(addr.byteOffset) * 8 + static_cast<size_t>(addr.bitOffset);

      // Ensure the bit vector is large enough
      if (bitIndex >= region.bitOccupied.size()) {
         size_t newSize = (bitIndex / 8) + 1;
         expandRegion(area, newSize);
         // Reload the reference after expansion
         it = m_regions.find(area);
         if (it == m_regions.end()) {
            return;
         }
         auto& newRegion = it->second;
         if (bitIndex < newRegion.bitOccupied.size()) {
            newRegion.bitOccupied[bitIndex] = true;
            if (static_cast<size_t>(addr.byteOffset) < newRegion.byteOccupied.size()) {
               newRegion.byteOccupied[addr.byteOffset] = true;
            }
         }
      } else {
         // Mark the bit as occupied
         region.bitOccupied[bitIndex] = true;
         // Mark the entire byte as occupied to prevent byte-aligned overlaps
         if (static_cast<size_t>(addr.byteOffset) < region.byteOccupied.size()) {
            region.byteOccupied[addr.byteOffset] = true;
         }
      }
   } else {
      // Byte-aligned access: mark a range of bytes
      for (int i = 0; i < sizeInBytes; ++i) {
         size_t byteOffset = static_cast<size_t>(addr.byteOffset + i);

         // Check if we need to expand the region
         if (byteOffset >= region.size) {
            expandRegion(area, byteOffset + 1);
            // Reload the reference after expansion
            it = m_regions.find(area);
            if (it == m_regions.end()) {
               return;
            }
            auto& newRegion = it->second;
            if (byteOffset < newRegion.byteOccupied.size()) {
               newRegion.byteOccupied[byteOffset] = true;
               size_t bitBase = byteOffset * 8;
               if (bitBase + 8 <= newRegion.bitOccupied.size()) {
                  for (size_t b = 0; b < 8; ++b) {
                     newRegion.bitOccupied[bitBase + b] = true;
                  }
               }
            }
         } else {
            // Mark the byte as occupied
            region.byteOccupied[byteOffset] = true;
            // Mark all bits in the byte as occupied
            size_t bitBase = byteOffset * 8;
            if (bitBase + 8 <= region.bitOccupied.size()) {
               for (size_t b = 0; b < 8; ++b) {
                  region.bitOccupied[bitBase + b] = true;
               }
            }
         }
      }
   }
}

/**
 * @brief Allocate a placeholder address in the memory region
 *
 * Finds the first free position that satisfies alignment requirements.
 * For BIT access, finds the first free bit.
 * For byte-aligned access, finds contiguous free bytes with proper alignment.
 *
 * @param area The memory area type
 * @param qualifier The address qualifier (BIT, BYTE, WORD, DWORD, LWORD)
 * @param sizeInBytes Size of the variable in bytes
 * @param alignment Required alignment in bytes
 * @return AllocationResult containing the allocated offset and bit position
 */
AddressAllocator::AllocationResult AddressAllocator::allocatePlaceholder(AddressExpr::AddressType area,
                                                                         AddressExpr::AddressQualifier qualifier,
                                                                         int sizeInBytes,
                                                                         int alignment)
{
   // Validate input parameters
   if (sizeInBytes <= 0) {
      return {0, -1, false};
   }

   if (alignment <= 0) {
      alignment = 1;
   }

   // Ensure the region exists with at least the alignment size
   ensureRegion(area, static_cast<size_t>(alignment));

   auto it = m_regions.find(area);
   if (it == m_regions.end()) {
      return {0, -1, false};
   }

   auto& region = it->second;

   if (qualifier == AddressExpr::AddressQualifier::BIT) {
      // BIT access: find the first free bit
      for (size_t byte = 0; byte < region.size; ++byte) {
         // Check if the byte is already fully occupied
         if (byte >= region.byteOccupied.size() || region.byteOccupied[byte]) {
            continue;
         }

         // Check each bit in the byte
         for (int bit = 0; bit < 8; ++bit) {
            size_t bitIndex = byte * 8 + bit;
            if (bitIndex < region.bitOccupied.size() && !region.bitOccupied[bitIndex]) {
               // Found a free bit - mark it as occupied
               region.bitOccupied[bitIndex] = true;
               if (byte < region.byteOccupied.size()) {
                  region.byteOccupied[byte] = true;
               }
               return {static_cast<int>(byte), bit, true};
            }
         }
      }

      // No free bit found - expand the region and retry
      expandRegion(area, region.size + 1);
      return allocatePlaceholder(area, qualifier, sizeInBytes, alignment);
   }

   // Byte-aligned access: find a contiguous free block with proper alignment
   size_t start = 0;
   while (start + static_cast<size_t>(sizeInBytes) <= region.size) {
      // Check alignment: start must be a multiple of alignment
      if (start % static_cast<size_t>(alignment) != 0) {
         start = alignUp(start + 1, static_cast<size_t>(alignment));
         continue;
      }

      // Check if all bytes in the block are free
      bool free = true;
      for (int i = 0; i < sizeInBytes; ++i) {
         size_t idx = start + i;
         if (idx >= region.byteOccupied.size() || region.byteOccupied[idx]) {
            free = false;
            break;
         }
      }

      if (free) {
         // Found a free block - mark all bytes and bits as occupied
         for (int i = 0; i < sizeInBytes; ++i) {
            size_t byteOff = start + i;
            if (byteOff < region.byteOccupied.size()) {
               region.byteOccupied[byteOff] = true;
               // Mark all bits in the byte as occupied
               size_t bitBase = byteOff * 8;
               if (bitBase + 8 <= region.bitOccupied.size()) {
                  for (size_t b = 0; b < 8; ++b) {
                     region.bitOccupied[bitBase + b] = true;
                  }
               }
            }
         }
         return {static_cast<int>(start), -1, true};
      }

      // Try the next position
      start += 1;
   }

   // No free block found - expand the region and retry
   expandRegion(area, region.size + static_cast<size_t>(sizeInBytes + alignment));
   return allocatePlaceholder(area, qualifier, sizeInBytes, alignment);
}

/**
 * @brief Get the current size of a memory region
 *
 * @param area The memory area type
 * @return size_t Size in bytes, or 0 if region doesn't exist
 */
size_t AddressAllocator::getRegionSize(AddressExpr::AddressType area) const
{
   auto it = m_regions.find(area);
   return (it != m_regions.end()) ? it->second.size : 0;
}

// ============================================================================
//  ProcessImageAnalyzer Implementation
// ============================================================================

/**
 * @brief Analyze a translation unit to detect address usage
 *
 * Resets the analyzer state and scans all POU bodies and global AT declarations
 * to determine the required process image sizes.
 *
 * @param tu The translation unit to analyze
 */
void ProcessImageAnalyzer::analyze(const TranslationUnit& tu)
{
   // Reset state
   m_addressInfos.clear();
   m_typeMap.clear();

   // Initialize type map
   m_typeMap[AddressExpr::AddressType::INPUT] = {AddressExpr::AddressType::INPUT, 0, 0, false};
   m_typeMap[AddressExpr::AddressType::OUTPUT] = {AddressExpr::AddressType::OUTPUT, 0, 0, false};
   m_typeMap[AddressExpr::AddressType::MARKER] = {AddressExpr::AddressType::MARKER, 0, 0, false};

   // Scan all POUs
   for (const auto& pou : tu.pous) {
      for (const auto& stmt : pou.body) {
         findAddresses(stmt);
      }
   }

   // Scan global variables for AT addresses
   for (const auto& sec : tu.globals) {
      for (const auto& decl : sec.decls) {
         if (!decl.atAddress.empty()) {
            // Parse the AT address string
            AddressExpr addr;
            if (parseAddressString(decl.atAddress, addr)) {
               updateMaxOffset(addr);
            }
         }
      }
   }
}

/**
 * @brief Recursively find address expressions inside a statement
 *
 * @param stmt The statement to scan
 */
void ProcessImageAnalyzer::findAddresses(const std::shared_ptr<Stmt>& stmt)
{
   if (!stmt) {
      return;
   }

   std::visit(
      [this](const auto& s) {
         using T = std::decay_t<decltype(s)>;

          if constexpr (std::is_same_v<T, AssignStmt>) {
             findAddressesInExpr(s.lhs);
             for (const auto& target : s.additionalTargets) {
                findAddressesInExpr(target);
             }
             findAddressesInExpr(s.rhs);
         } else if constexpr (std::is_same_v<T, ExprStmt>) {
            findAddressesInExpr(s.expr);
         } else if constexpr (std::is_same_v<T, IfStmt>) {
            for (const auto& branch : s.branches) {
               if (branch.condition) {
                  findAddressesInExpr(branch.condition);
               }
               for (const auto& st : branch.body) {
                  findAddresses(st);
               }
            }
         } else if constexpr (std::is_same_v<T, ForStmt>) {
            findAddressesInExpr(s.from);
            findAddressesInExpr(s.to);
            if (s.by) {
               findAddressesInExpr(s.by);
            }
            for (const auto& st : s.body) {
               findAddresses(st);
            }
         } else if constexpr (std::is_same_v<T, WhileStmt>) {
            findAddressesInExpr(s.condition);
            for (const auto& st : s.body) {
               findAddresses(st);
            }
         } else if constexpr (std::is_same_v<T, RepeatStmt>) {
            for (const auto& st : s.body) {
               findAddresses(st);
            }
            findAddressesInExpr(s.condition);
         } else if constexpr (std::is_same_v<T, CaseStmt>) {
            findAddressesInExpr(s.selector);
            for (const auto& branch : s.branches) {
               for (const auto& cv : branch.values) {
                  findAddressesInExpr(cv.low);
                  if (cv.high) {
                     findAddressesInExpr(cv.high);
                  }
               }
               for (const auto& st : branch.body) {
                  findAddresses(st);
               }
            }
         }
         // ReturnStmt, ExitStmt, EmptyStmt have no expressions
      },
      stmt->node);
}

/**
 * @brief Recursively find address expressions inside an expression
 *
 * @param expr The expression to scan
 */
void ProcessImageAnalyzer::findAddressesInExpr(const std::shared_ptr<Expr>& expr)
{
   if (!expr) {
      return;
   }

   std::visit(
      [this](const auto& e) {
         using T = std::decay_t<decltype(e)>;

         if constexpr (std::is_same_v<T, AddressExpr>) {
            updateMaxOffset(e);
         } else if constexpr (std::is_same_v<T, UnaryExpr>) {
            findAddressesInExpr(e.operand);
         } else if constexpr (std::is_same_v<T, BinaryExpr>) {
            findAddressesInExpr(e.left);
            findAddressesInExpr(e.right);
         } else if constexpr (std::is_same_v<T, MemberExpr>) {
            findAddressesInExpr(e.object);
         } else if constexpr (std::is_same_v<T, IndexExpr>) {
            findAddressesInExpr(e.array);
            for (const auto& idx : e.indices) {
               findAddressesInExpr(idx);
            }
         } else if constexpr (std::is_same_v<T, DerefExpr>) {
            findAddressesInExpr(e.pointer);
         } else if constexpr (std::is_same_v<T, CallExpr>) {
            findAddressesInExpr(e.callee);
            for (const auto& arg : e.args) {
               findAddressesInExpr(arg.value);
            }
         } else if constexpr (std::is_same_v<T, CastExpr>) {
            findAddressesInExpr(e.operand);
         } else if constexpr (std::is_same_v<T, ArrayInitExpr>) {
            for (const auto& elem : e.elements) {
               findAddressesInExpr(elem);
            }
         }
         // LiteralExpr, BoolLitExpr, IdentExpr, SuperCallExpr, AdrExpr, SizeofExpr have no nested addresses
      },
      expr->node);
}

/**
 * @brief Update the maximum byte and bit offsets for an address's memory area
 *
 * @param addr The address expression to record
 */
void ProcessImageAnalyzer::updateMaxOffset(const AddressExpr& addr)
{
   auto it = m_typeMap.find(addr.type);
   if (it == m_typeMap.end()) {
      return;
   }

   AddressInfo& info = it->second;

   // Update byte offset (considering the size of the access)
   int sizeInBytes = 1;
   switch (addr.qualifier) {
   case AddressExpr::AddressQualifier::WORD:
      sizeInBytes = 2;
      break;
   case AddressExpr::AddressQualifier::DWORD:
      sizeInBytes = 4;
      break;
   case AddressExpr::AddressQualifier::LWORD:
      sizeInBytes = 8;
      break;
   default:
      sizeInBytes = 1;
      break;
   }

   int endOffset = addr.byteOffset + sizeInBytes - 1;
   if (endOffset > info.maxByteOffset) {
      info.maxByteOffset = endOffset;
   }

   if (addr.qualifier == AddressExpr::AddressQualifier::BIT) {
      info.hasBitAccess = true;
      if (addr.bitOffset > info.maxBitOffset) {
         info.maxBitOffset = addr.bitOffset;
      }
   }
}

/**
 * @brief Compute the recommended process image configuration
 *
 * Sizes each memory area to the largest offset used, rounded up to a power of
 * two, with a minimum of 1024 bytes per area.
 *
 * @return Recommended ProcessImageConfig
 */
ProcessImageConfig ProcessImageAnalyzer::getRecommendedConfig() const
{
   ProcessImageConfig config;
   config.autoDetect = true;

   if (m_addressInfos.empty()) {
      // No addresses used - use minimum sizes
      config.inputBytes = 1024;
      config.outputBytes = 1024;
      config.markerBytes = 1024;
      return config;
   }

   // Calculate sizes from type map
   for (const auto& [type, info] : m_typeMap) {
      size_t needed = info.maxByteOffset + 1;
      if (info.hasBitAccess) {
         // Ensure at least one byte for bit access
         needed = std::max(needed, size_t(1));
      }

      switch (type) {
      case AddressExpr::AddressType::INPUT:
         config.inputBytes = nextPowerOfTwo(needed);
         break;
      case AddressExpr::AddressType::OUTPUT:
         config.outputBytes = nextPowerOfTwo(needed);
         break;
      case AddressExpr::AddressType::MARKER:
         config.markerBytes = nextPowerOfTwo(needed);
         break;
      default:
         break;
      }
   }

   // Minimum sizes (like Siemens S7-1200)
   config.inputBytes = std::max(config.inputBytes, size_t(1024));
   config.outputBytes = std::max(config.outputBytes, size_t(1024));
   config.markerBytes = std::max(config.markerBytes, size_t(1024));

   return config;
}

/**
 * @brief Round a value up to the next power of two
 *
 * @param n The value to round up
 * @return size_t The smallest power of two greater than or equal to n
 */
size_t ProcessImageAnalyzer::nextPowerOfTwo(size_t n) const
{
   if (n <= 1) {
      return 1;
   }
   size_t power = 1;
   while (power < n) {
      power <<= 1;
   }
   return power;
}
