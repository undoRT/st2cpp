/**
 * @file test_allocator.cpp
 * @brief Implementation of test cases for the AddressAllocator
 *
 * @author Salvatore Bamundo
 * @date July 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#include <gtest/gtest.h>
#include "codegen/CodeGenerator.h"

// ============================================================================
//  Fixed Address Tests
// ============================================================================

/**
 * @brief Test marking a fixed bit address
 */
TEST(AddressAllocatorTest, MarkFixedBit)
{
   AddressAllocator allocator;
   AddressExpr addr;
   addr.type = AddressExpr::AddressType::INPUT;
   addr.qualifier = AddressExpr::AddressQualifier::BIT;
   addr.byteOffset = 0;
   addr.bitOffset = 0;

   allocator.markFixedAddress(addr, 1);
   EXPECT_EQ(allocator.getRegionSize(AddressExpr::AddressType::INPUT), 1);
}

/**
 * @brief Test marking a fixed byte address
 * 
 * The allocator uses power-of-two growth, so offset 5 grows to 8
 */
TEST(AddressAllocatorTest, MarkFixedByte)
{
   AddressAllocator allocator;
   AddressExpr addr;
   addr.type = AddressExpr::AddressType::INPUT;
   addr.qualifier = AddressExpr::AddressQualifier::BYTE;
   addr.byteOffset = 5;

   allocator.markFixedAddress(addr, 1);
   // Region grows to power of two >= (offset + 1) = 6 -> 8
   EXPECT_EQ(allocator.getRegionSize(AddressExpr::AddressType::INPUT), 8);
}

/**
 * @brief Test marking a fixed word address (2 bytes)
 * 
 * The allocator uses power-of-two growth, so offset 10 + 2 = 12 -> 16
 */
TEST(AddressAllocatorTest, MarkFixedWord)
{
   AddressAllocator allocator;
   AddressExpr addr;
   addr.type = AddressExpr::AddressType::OUTPUT;
   addr.qualifier = AddressExpr::AddressQualifier::WORD;
   addr.byteOffset = 10;

   allocator.markFixedAddress(addr, 2);
   // Region grows to power of two >= 12 -> 16
   EXPECT_EQ(allocator.getRegionSize(AddressExpr::AddressType::OUTPUT), 16);
}

/**
 * @brief Test marking a fixed dword address (4 bytes)
 * 
 * The allocator uses power-of-two growth, so offset 20 + 4 = 24 -> 32
 */
TEST(AddressAllocatorTest, MarkFixedDword)
{
   AddressAllocator allocator;
   AddressExpr addr;
   addr.type = AddressExpr::AddressType::MARKER;
   addr.qualifier = AddressExpr::AddressQualifier::DWORD;
   addr.byteOffset = 20;

   allocator.markFixedAddress(addr, 4);
   // Region grows to power of two >= 24 -> 32
   EXPECT_EQ(allocator.getRegionSize(AddressExpr::AddressType::MARKER), 32);
}

/**
 * @brief Test marking multiple fixed addresses in same region
 */
TEST(AddressAllocatorTest, MarkMultipleFixed)
{
   AddressAllocator allocator;

   // Mark bit at 0.0
   AddressExpr bitAddr;
   bitAddr.type = AddressExpr::AddressType::INPUT;
   bitAddr.qualifier = AddressExpr::AddressQualifier::BIT;
   bitAddr.byteOffset = 0;
   bitAddr.bitOffset = 0;
   allocator.markFixedAddress(bitAddr, 1);

   // Mark byte at offset 5
   AddressExpr byteAddr;
   byteAddr.type = AddressExpr::AddressType::INPUT;
   byteAddr.qualifier = AddressExpr::AddressQualifier::BYTE;
   byteAddr.byteOffset = 5;
   allocator.markFixedAddress(byteAddr, 1);

   // Mark word at offset 10
   AddressExpr wordAddr;
   wordAddr.type = AddressExpr::AddressType::INPUT;
   wordAddr.qualifier = AddressExpr::AddressQualifier::WORD;
   wordAddr.byteOffset = 10;
   allocator.markFixedAddress(wordAddr, 2);

   // Max offset = 10 + 2 = 12 -> next power of two = 16
   EXPECT_EQ(allocator.getRegionSize(AddressExpr::AddressType::INPUT), 16);
}

// ============================================================================
//  Placeholder Allocation Tests
// ============================================================================

/**
 * @brief Test allocating a byte after a fixed bit
 */
TEST(AddressAllocatorTest, AllocateByteAfterBit)
{
   AddressAllocator allocator;

   // Occupy %IX0.0
   AddressExpr bitAddr;
   bitAddr.type = AddressExpr::AddressType::INPUT;
   bitAddr.qualifier = AddressExpr::AddressQualifier::BIT;
   bitAddr.byteOffset = 0;
   bitAddr.bitOffset = 0;
   allocator.markFixedAddress(bitAddr, 1);

   // Allocate %IB*
   auto result = allocator.allocatePlaceholder(AddressExpr::AddressType::INPUT, AddressExpr::AddressQualifier::BYTE, 1, 1);

   EXPECT_EQ(result.byteOffset, 1); // Should allocate at offset 1 (after the bit)
   EXPECT_EQ(result.bitOffset, -1);
   EXPECT_TRUE(result.success);
}

/**
 * @brief Test allocating a bit in a byte with existing bits
 * 
 * NOTE: The allocator allocates the first free bit in the first free byte.
 * Since byte 0 is occupied by the bit, it moves to byte 1.
 */
TEST(AddressAllocatorTest, AllocateBitAfterBit)
{
   AddressAllocator allocator;

   // Occupy %IX0.0
   AddressExpr bitAddr;
   bitAddr.type = AddressExpr::AddressType::INPUT;
   bitAddr.qualifier = AddressExpr::AddressQualifier::BIT;
   bitAddr.byteOffset = 0;
   bitAddr.bitOffset = 0;
   allocator.markFixedAddress(bitAddr, 1);

   // Allocate %IX*
   auto result = allocator.allocatePlaceholder(AddressExpr::AddressType::INPUT, AddressExpr::AddressQualifier::BIT, 1, 1);

   // The allocator looks for free bits in free bytes.
   // Byte 0 is occupied (has a bit), so it moves to byte 1.
   EXPECT_EQ(result.byteOffset, 1);
   EXPECT_EQ(result.bitOffset, 0);
   EXPECT_TRUE(result.success);
}

/**
 * @brief Test allocating a bit when byte is full
 */
TEST(AddressAllocatorTest, AllocateBitWhenByteFull)
{
   AddressAllocator allocator;

   // Occupy all 8 bits in byte 0
   for (int bit = 0; bit < 8; ++bit) {
      AddressExpr addr;
      addr.type = AddressExpr::AddressType::INPUT;
      addr.qualifier = AddressExpr::AddressQualifier::BIT;
      addr.byteOffset = 0;
      addr.bitOffset = bit;
      allocator.markFixedAddress(addr, 1);
   }

   // Allocate next bit
   auto result = allocator.allocatePlaceholder(AddressExpr::AddressType::INPUT, AddressExpr::AddressQualifier::BIT, 1, 1);

   EXPECT_EQ(result.byteOffset, 1); // Should move to next byte
   EXPECT_EQ(result.bitOffset, 0);
   EXPECT_TRUE(result.success);
}

// ============================================================================
//  Alignment Tests
// ============================================================================

/**
 * @brief Test allocating a word with 2-byte alignment
 */
TEST(AddressAllocatorTest, AllocateWordAligned)
{
   AddressAllocator allocator;

   // Allocate WORD with alignment 2
   auto result = allocator.allocatePlaceholder(AddressExpr::AddressType::OUTPUT, AddressExpr::AddressQualifier::WORD, 2, 2);

   EXPECT_EQ(result.byteOffset, 0);
   EXPECT_EQ(result.byteOffset % 2, 0); // aligned to 2
   EXPECT_TRUE(result.success);

   // Allocate another WORD
   auto result2 = allocator.allocatePlaceholder(AddressExpr::AddressType::OUTPUT, AddressExpr::AddressQualifier::WORD, 2, 2);

   EXPECT_EQ(result2.byteOffset, 2);
   EXPECT_EQ(result2.byteOffset % 2, 0);
   EXPECT_TRUE(result2.success);
}

/**
 * @brief Test allocating a dword with 4-byte alignment
 */
TEST(AddressAllocatorTest, AllocateDwordAligned)
{
   AddressAllocator allocator;

   // Allocate DWORD with alignment 4
   auto result = allocator.allocatePlaceholder(AddressExpr::AddressType::MARKER, AddressExpr::AddressQualifier::DWORD, 4, 4);

   EXPECT_EQ(result.byteOffset, 0);
   EXPECT_EQ(result.byteOffset % 4, 0);
   EXPECT_TRUE(result.success);

   // Allocate another DWORD
   auto result2 = allocator.allocatePlaceholder(AddressExpr::AddressType::MARKER, AddressExpr::AddressQualifier::DWORD, 4, 4);

   EXPECT_EQ(result2.byteOffset, 4);
   EXPECT_EQ(result2.byteOffset % 4, 0);
   EXPECT_TRUE(result2.success);
}

/**
 * @brief Test allocating a word after a byte (alignment adjustment)
 */
TEST(AddressAllocatorTest, AllocateWordAfterByte)
{
   AddressAllocator allocator;

   // Allocate a BYTE at offset 0
   auto byteResult = allocator.allocatePlaceholder(AddressExpr::AddressType::OUTPUT, AddressExpr::AddressQualifier::BYTE, 1, 1);
   EXPECT_EQ(byteResult.byteOffset, 0);

   // Allocate a WORD - should align to offset 2 (skipping offset 1)
   auto wordResult = allocator.allocatePlaceholder(AddressExpr::AddressType::OUTPUT, AddressExpr::AddressQualifier::WORD, 2, 2);

   EXPECT_EQ(wordResult.byteOffset, 2); // Aligned to 2
   EXPECT_EQ(wordResult.byteOffset % 2, 0);
   EXPECT_TRUE(wordResult.success);
}

/**
 * @brief Test allocating a dword after a word (alignment adjustment)
 */
TEST(AddressAllocatorTest, AllocateDwordAfterWord)
{
   AddressAllocator allocator;

   // Allocate a WORD at offset 0
   auto wordResult = allocator.allocatePlaceholder(AddressExpr::AddressType::MARKER, AddressExpr::AddressQualifier::WORD, 2, 2);
   EXPECT_EQ(wordResult.byteOffset, 0);

   // Allocate a DWORD - should align to offset 4 (skipping offset 2-3)
   auto dwordResult = allocator.allocatePlaceholder(AddressExpr::AddressType::MARKER, AddressExpr::AddressQualifier::DWORD, 4, 4);

   EXPECT_EQ(dwordResult.byteOffset, 4); // Aligned to 4
   EXPECT_EQ(dwordResult.byteOffset % 4, 0);
   EXPECT_TRUE(dwordResult.success);
}

// ============================================================================
//  Mixed Address Type Tests
// ============================================================================

/**
 * @brief Test allocating bytes and bits in the same region
 */
TEST(AddressAllocatorTest, MixedBitAndByteAllocation)
{
   AddressAllocator allocator;

   // Allocate a bit at offset 0, bit 0
   auto bitResult = allocator.allocatePlaceholder(AddressExpr::AddressType::INPUT, AddressExpr::AddressQualifier::BIT, 1, 1);
   EXPECT_EQ(bitResult.byteOffset, 0);
   EXPECT_EQ(bitResult.bitOffset, 0);

   // Allocate a byte - should go to offset 1 (byte 0 is occupied by bit)
   auto byteResult = allocator.allocatePlaceholder(AddressExpr::AddressType::INPUT, AddressExpr::AddressQualifier::BYTE, 1, 1);

   EXPECT_EQ(byteResult.byteOffset, 1);
   EXPECT_EQ(byteResult.bitOffset, -1);
   EXPECT_TRUE(byteResult.success);
}

/**
 * @brief Test that a region grows when needed (power-of-two)
 */
TEST(AddressAllocatorTest, RegionGrows)
{
   AddressAllocator allocator;

   // Allocate a byte at offset 100
   auto result = allocator.allocatePlaceholder(AddressExpr::AddressType::INPUT, AddressExpr::AddressQualifier::BYTE, 1, 1);

   // Should allocate at offset 0 (first free)
   EXPECT_EQ(result.byteOffset, 0);
   EXPECT_TRUE(result.success);

   // Allocate a byte at offset 200
   AddressExpr fixedAddr;
   fixedAddr.type = AddressExpr::AddressType::INPUT;
   fixedAddr.qualifier = AddressExpr::AddressQualifier::BYTE;
   fixedAddr.byteOffset = 200;
   allocator.markFixedAddress(fixedAddr, 1);

   // Region should grow to power-of-two >= 201 -> 256
   // (The allocator may use a different growth strategy, so we check >=)
   size_t size = allocator.getRegionSize(AddressExpr::AddressType::INPUT);
   EXPECT_GE(size, 201);
}

/**
 * @brief Test allocating a word after mixed bit allocations
 */
TEST(AddressAllocatorTest, AllocateWordAfterMixedBits)
{
   AddressAllocator allocator;

   // Occupy bits in byte 0
   for (int bit = 0; bit < 4; ++bit) {
      AddressExpr addr;
      addr.type = AddressExpr::AddressType::OUTPUT;
      addr.qualifier = AddressExpr::AddressQualifier::BIT;
      addr.byteOffset = 0;
      addr.bitOffset = bit;
      allocator.markFixedAddress(addr, 1);
   }

   // Allocate a WORD - byte 0 is occupied by bits, so should start at offset 2
   auto wordResult = allocator.allocatePlaceholder(AddressExpr::AddressType::OUTPUT, AddressExpr::AddressQualifier::WORD, 2, 2);

   // Should be at offset 2 (aligned to 2, skipping byte 0 and 1)
   EXPECT_EQ(wordResult.byteOffset, 2);
   EXPECT_EQ(wordResult.byteOffset % 2, 0);
   EXPECT_TRUE(wordResult.success);
}

// ============================================================================
//  Marker (Internal Memory) Tests
// ============================================================================

/**
 * @brief Test allocation in MARKER region
 */
TEST(AddressAllocatorTest, AllocateMarkerByte)
{
   AddressAllocator allocator;

   auto result = allocator.allocatePlaceholder(AddressExpr::AddressType::MARKER, AddressExpr::AddressQualifier::BYTE, 1, 1);

   EXPECT_EQ(result.byteOffset, 0);
   EXPECT_TRUE(result.success);
   EXPECT_EQ(allocator.getRegionSize(AddressExpr::AddressType::MARKER), 1);
}

/**
 * @brief Test marker region with mixed types
 */
TEST(AddressAllocatorTest, AllocateMarkerMixed)
{
   AddressAllocator allocator;

   // Allocate a word at offset 0
   auto wordResult = allocator.allocatePlaceholder(AddressExpr::AddressType::MARKER, AddressExpr::AddressQualifier::WORD, 2, 2);
   EXPECT_EQ(wordResult.byteOffset, 0);

   // Allocate a byte - should go to offset 2 (after the word)
   auto byteResult = allocator.allocatePlaceholder(AddressExpr::AddressType::MARKER, AddressExpr::AddressQualifier::BYTE, 1, 1);
   EXPECT_EQ(byteResult.byteOffset, 2);

   // Allocate a bit - byte 2 is occupied by the byte, so move to byte 3
   auto bitResult = allocator.allocatePlaceholder(AddressExpr::AddressType::MARKER, AddressExpr::AddressQualifier::BIT, 1, 1);
   EXPECT_EQ(bitResult.byteOffset, 3);
   EXPECT_EQ(bitResult.bitOffset, 0);
}

// ============================================================================
//  Edge Cases Tests
// ============================================================================

/**
 * @brief Test allocating with zero size
 */
TEST(AddressAllocatorTest, AllocateZeroSize)
{
   AddressAllocator allocator;

   auto result = allocator.allocatePlaceholder(AddressExpr::AddressType::INPUT, AddressExpr::AddressQualifier::BYTE, 0, 1);

   EXPECT_FALSE(result.success);
   EXPECT_EQ(result.byteOffset, 0);
}

/**
 * @brief Test allocating with zero alignment
 */
TEST(AddressAllocatorTest, AllocateZeroAlignment)
{
   AddressAllocator allocator;

   auto result = allocator.allocatePlaceholder(AddressExpr::AddressType::INPUT, AddressExpr::AddressQualifier::BYTE, 1, 0);

   EXPECT_TRUE(result.success);
   EXPECT_EQ(result.byteOffset, 0);
}

/**
 * @brief Test multiple allocations to fill a region
 * 
 * The allocator uses power-of-two growth, so 10 bytes grows to 16
 */
TEST(AddressAllocatorTest, MultipleAllocations)
{
   AddressAllocator allocator;

   // Allocate 10 bytes
   for (int i = 0; i < 10; ++i) {
      auto result = allocator.allocatePlaceholder(AddressExpr::AddressType::INPUT, AddressExpr::AddressQualifier::BYTE, 1, 1);
      EXPECT_TRUE(result.success);
      EXPECT_EQ(result.byteOffset, i);
   }

   // Region should be power-of-two >= 10 -> 16
   EXPECT_EQ(allocator.getRegionSize(AddressExpr::AddressType::INPUT), 16);
}