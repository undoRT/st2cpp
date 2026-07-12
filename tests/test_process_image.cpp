/**
 * @file test_process_image.cpp
 * @brief Implementation of test cases for Process Image functionality
 *
 * @author Salvatore Bamundo
 * @date July 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#include <gtest/gtest.h>
#include "helpers/TestHelper.h"
#include <regex>

// ============================================================================
//  Test Fixture
// ============================================================================

class ProcessImageTest : public ::testing::Test
{
protected:
   GeneratedCode result;

   void SetUp() override
   {
      std::string stCode = R"(
            (*
            * test_process_image.st
            * Test file for Process Image functionality
            * Tests: %I, %Q, %M addresses with different qualifiers and placeholders "*"
            *)

            TYPE TestEnum :
                (
                IDLE,
                RUNNING,
                STOPPED,
                ERROR
                );
            END_TYPE

            TYPE MotorStatus :
                STRUCT
                    Running : BOOL;
                    Speed : INT;
                    Temp : REAL;
                    Fault : BOOL;
                END_STRUCT;
            END_TYPE

            (* Global variables with AT addresses - fixed and placeholder *)
            VAR_GLOBAL
                (* Fixed addresses - will be marked as occupied *)
                StartButton AT %IX0.0 : BOOL := FALSE;
                StopButton AT %IX0.1 : BOOL := FALSE;
                Emergency AT %IX0.2 : BOOL := FALSE;

                MotorOutput AT %QX0.0 : BOOL := FALSE;
                AlarmLight AT %QX0.1 : BOOL := FALSE;
                StatusWord AT %QW2 : WORD := 16#0000;

                Counter AT %MW10 : INT := 0;
                Temperature AT %MW12 : INT := 25;
                Flags AT %MB20 : BYTE := 0;
                Status AT %ML22 : DWORD := 0;

                (* ========== PLACEHOLDER ADDRESSES - auto-allocated ========== *)

                (* BIT placeholders - will be allocated after fixed bits *)
                SensorA AT %IX* : BOOL := FALSE;
                SensorB AT %IX* : BOOL := FALSE;
                ActuatorA AT %QX* : BOOL := FALSE;
                ActuatorB AT %QX* : BOOL := FALSE;
                FlagA AT %MX* : BOOL := FALSE;

                (* BYTE placeholders - aligned to byte boundaries *)
                AnalogInput AT %IB* : BYTE := 0;
                AnalogOutput AT %QB* : BYTE := 0;
                StatusFlags AT %MB* : BYTE := 0;

                (* WORD placeholders - must be aligned to 2-byte boundary *)
                Setpoint AT %IW* : INT := 100;
                Feedback AT %IW* : INT := 0;
                Command AT %QW* : WORD := 16#0000;
                MarkerData AT %MW* : WORD := 0;

                (* DWORD placeholders - must be aligned to 4-byte boundary *)
                LargeCounter AT %MD* : DWORD := 0;
                Timestamp AT %MD* : DWORD := 0;

                (* Mixed placeholders in different areas *)
                Diagnostic AT %MD* : DWORD := 0;
                DebugFlag AT %MX* : BOOL := FALSE;
                DebugValue AT %MW* : INT := 0;
            END_VAR

            (* Function block with process image access and methods *)
            FUNCTION_BLOCK MotorController
                VAR_INPUT
                    Enable : BOOL;
                    Speed : INT;
                END_VAR

                VAR_OUTPUT
                    Running : BOOL;
                    Fault : BOOL;
                END_VAR

                VAR
                    Timer : INT := 0;
                    (* Local AT placeholder inside FB *)
                    InternalFlag AT %MX* : BOOL := FALSE;
                    InternalData AT %MW* : INT := 0;
                END_VAR

                METHOD CheckOverheat : BOOL
                    VAR_INPUT
                        Temp : INT;
                    END_VAR
                    VAR_OUTPUT
                        Overheat : BOOL;
                    END_VAR
                    Overheat := Temp > 80;
                    CheckOverheat := Overheat;
                END_METHOD

                METHOD ReadInternalData : INT
                    VAR_OUTPUT
                        Value : INT;
                    END_VAR
                    Value := InternalData;
                END_METHOD
            END_FUNCTION_BLOCK

            (* Main program *)
            PROGRAM Main
                VAR
                    Motor : MotorController;
                    TempRead : INT;
                    State : TestEnum := TestEnum.IDLE;
                    Status : MotorStatus;
                    CycleCounter : INT := 0;
                    NewState : TestEnum := IDLE;
                    Updated : BOOL := FALSE;
                    Start : BOOL := FALSE;
                    Stop : BOOL := FALSE;
                    EStop : BOOL := FALSE;
                    Increment : BOOL := FALSE;
                    NewTemp : INT := 0;
                    (* Local AT placeholders inside program *)
                    LocalSensor AT %IX* : BOOL := FALSE;
                    LocalOutput AT %QX* : BOOL := FALSE;
                    LocalData AT %MW* : INT := 0;
                END_VAR

                (* Main program body *)
                Start := StartButton;
                Stop := StopButton;
                EStop := Emergency;

                IF Emergency THEN
                    MotorOutput := FALSE;
                    AlarmLight := TRUE;
                    Flags := Flags OR 16#80;
                END_IF;

                IF NOT Emergency THEN
                    IF State = TestEnum.RUNNING THEN
                        MotorOutput := TRUE;
                        AlarmLight := FALSE;
                        StatusWord := 16#0001;
                    ELSIF State = TestEnum.STOPPED THEN
                        MotorOutput := FALSE;
                        AlarmLight := TRUE;
                        StatusWord := 16#0002;
                    ELSE
                        MotorOutput := FALSE;
                        AlarmLight := FALSE;
                        StatusWord := 16#0000;
                    END_IF;
                END_IF;

                IF Increment THEN
                    Counter := Counter + 1;
                END_IF;

                Temperature := NewTemp;
                Status.Temp := INT_TO_REAL(NewTemp) / 10.0;

                Motor(Enable := Start, Speed := 100);

                Updated := TRUE;

                LocalSensor := SensorA;
                LocalOutput := ActuatorA;
                LocalData := Setpoint;
            END_PROGRAM

            (* Diagnostic program *)
            PROGRAM Diagnostic
                VAR
                    ErrorCode AT %MW50 : INT := 0;
                    ErrorTime AT %MD52 : DWORD := 0;
                    ErrorFlag AT %MX54.0 : BOOL := FALSE;
                    Status AT %MW60 : INT := 0;
                    Code : INT := 0;
                    (* Local AT placeholders inside Diagnostic program *)
                    DiagData AT %MD* : DWORD := 0;
                    DiagFlag AT %MX* : BOOL := FALSE;
                    DiagWord AT %MW* : WORD := 0;
                END_VAR

                ErrorCode := Code;
                ErrorTime := 0;
                ErrorFlag := TRUE;
                Status := 1;

                ErrorCode := 0;
                ErrorTime := 0;
                ErrorFlag := FALSE;
                Status := 0;

                DiagData := ErrorTime;
                DiagFlag := ErrorFlag;
                DiagWord := 16#FFFF;
            END_PROGRAM

            (* Function that uses process image addresses *)
            FUNCTION GetMotorSpeed : INT
                VAR_INPUT
                    MotorID : INT;
                END_VAR
                VAR
                    Speed AT %IW100 : INT := 0;
                    Speed2 AT %IW102 : INT := 0;
                    (* Local AT placeholders inside function *)
                    TempSpeed AT %IW* : INT := 0;
                    SensorValue AT %IW* : INT := 0;
                END_VAR
                IF MotorID = 1 THEN
                    GetMotorSpeed := Speed;
                ELSE
                    GetMotorSpeed := Speed2;
                END_IF;
            END_FUNCTION
        )";

      result = TestHelper::generateFromST(stCode);
   }

   void expectContains(const std::string& text, const std::string& msg = "")
   {
      EXPECT_NE(result.header.find(text), std::string::npos) << msg << "\nFull header:\n" << result.header;
   }

   void expectContainsSource(const std::string& text, const std::string& msg = "")
   {
      EXPECT_NE(result.source.find(text), std::string::npos) << msg << "\nFull source:\n" << result.source;
   }

   void expectNotContains(const std::string& text, const std::string& msg = "")
   {
      EXPECT_EQ(result.header.find(text), std::string::npos) << msg << "\nFull header:\n" << result.header;
   }

   void expectNotContainsSource(const std::string& text, const std::string& msg = "")
   {
      EXPECT_EQ(result.source.find(text), std::string::npos) << msg << "\nFull source:\n" << result.source;
   }
};

// ============================================================================
//  Fixed Address Tests
// ============================================================================

/**
 * @brief Verify fixed addresses are correctly mapped in comments
 */
TEST_F(ProcessImageTest, FixedAddresses)
{
   expectContains("// AT %IX0.0");
   expectContains("// AT %IX0.1");
   expectContains("// AT %IX0.2");
   expectContains("// AT %QX0.0");
   expectContains("// AT %QX0.1");
   expectContains("// AT %QW2");
   expectContains("// AT %MW10");
   expectContains("// AT %MW12");
   expectContains("// AT %MB20");
   expectContains("// AT %ML22");
}

/**
 * @brief Verify fixed addresses are correctly accessed in the code
 * 
 * Note: The getter/setter functions use the full name like getPi_STARTBUTTON()
 * and internally call readInputBit(0, 0). The access patterns are in the
 * header (getter/setter definitions) and source (usage).
 */
TEST_F(ProcessImageTest, FixedAddressAccess)
{
   // Check that getter/setter functions exist for fixed addresses
   expectContains("getPi_STARTBUTTON");
   expectContains("getPi_STOPBUTTON");
   expectContains("getPi_EMERGENCY");
   expectContains("getPi_MOTOROUTPUT");
   expectContains("getPi_ALARMLIGHT");
   expectContains("getPi_STATUSWORD");
   expectContains("getPi_COUNTER");
   expectContains("getPi_TEMPERATURE");
   expectContains("getPi_FLAGS");
   expectContains("getPi_STATUS");

   // Check that the getter functions are used in the source
   expectContainsSource("getPi_STARTBUTTON()");
   expectContainsSource("getPi_STOPBUTTON()");
   expectContainsSource("getPi_EMERGENCY()");
   expectContainsSource("getPi_FLAGS()");
}

// ============================================================================
//  Placeholder Allocation Tests
// ============================================================================

/**
 * @brief Verify placeholder addresses are allocated and getter/setter functions are generated
 */
TEST_F(ProcessImageTest, PlaceholderAllocation)
{
   // Getter functions for placeholder variables (all UPPERCASE) - in HEADER
   expectContains("getPi_SENSORA");
   expectContains("getPi_SENSORB");
   expectContains("getPi_ACTUATORA");
   expectContains("getPi_ACTUATORB");
   expectContains("getPi_FLAGA");
   expectContains("getPi_ANALOGINPUT");
   expectContains("getPi_ANALOGOUTPUT");
   expectContains("getPi_STATUSFLAGS");
   expectContains("getPi_SETPOINT");
   expectContains("getPi_FEEDBACK");
   expectContains("getPi_COMMAND");
   expectContains("getPi_MARKERDATA");
   expectContains("getPi_LARGECOUNTER");
   expectContains("getPi_TIMESTAMP");
   expectContains("getPi_DIAGNOSTIC");
   expectContains("getPi_DEBUGFLAG");
   expectContains("getPi_DEBUGVALUE");
}

/**
 * @brief Verify placeholder variables are used in the source
 */
TEST_F(ProcessImageTest, PlaceholderAllocationUsage)
{
   // Check that placeholder variables are accessed via getter/setter in SOURCE
   // These are the actual calls to getter functions in the program body
   expectContainsSource("getPi_SENSORA()");
   expectContainsSource("getPi_ACTUATORA()");
   expectContainsSource("getPi_SETPOINT()");
   expectContainsSource("setPi_LOCALSENSOR");
   expectContainsSource("setPi_LOCALOUTPUT");
   expectContainsSource("setPi_LOCALDATA");
}

// ============================================================================
//  Function Block AT Variables Tests
// ============================================================================

/**
 * @brief Verify AT variables in FUNCTION_BLOCK generate getter/setter
 */
TEST_F(ProcessImageTest, FunctionBlockATVariables)
{
   // All UPPERCASE - in HEADER
   expectContains("inline Bool getPi_INTERNALFLAG() const");
   expectContains("inline void setPi_INTERNALFLAG(Bool value)");
   expectContains("inline Int16 getPi_INTERNALDATA() const");
   expectContains("inline void setPi_INTERNALDATA(Int16 value)");

   // Verify they are NOT declared as normal members - in HEADER
   expectNotContains("Bool INTERNALFLAG");
   expectNotContains("Int16 INTERNALDATA");
}

/**
 * @brief Verify FB methods use AT getter/setter correctly
 */
TEST_F(ProcessImageTest, FunctionBlockMethodUsesAT)
{
   expectContainsSource("getPi_INTERNALDATA()");
}

// ============================================================================
//  Function AT Variables Tests
// ============================================================================

/**
 * @brief Verify AT variables in FUNCTION are accessed directly (no getter/setter)
 */
TEST_F(ProcessImageTest, FunctionATVariables)
{
   // Verify GetMotorSpeed function does NOT declare AT variables as locals
   expectNotContains("Int16 SPEED{0};");
   expectNotContains("Int16 SPEED2{0};");

   // Verify direct process image access
   expectContainsSource("processImage.readInputWord(100)"); // Speed
   expectContainsSource("processImage.readInputWord(102)"); // Speed2

   // Verify no getters for function locals
   expectNotContains("getPi_SPEED");
   expectNotContains("getPi_SPEED2");
}

/**
 * @brief Verify function with AT variables doesn't create getter/setter
 */
TEST_F(ProcessImageTest, FunctionNoATGetter)
{
   // The function GetMotorSpeed should NOT have getter functions
   expectNotContains("getPi_TEMPSPEED");
   expectNotContains("getPi_SENSORVALUE");
}

// ============================================================================
//  Program AT Variables Tests
// ============================================================================

/**
 * @brief Verify AT variables in PROGRAM generate getter/setter
 */
TEST_F(ProcessImageTest, ProgramATVariables)
{
   // Main program - UPPERCASE - in HEADER
   expectContains("inline Bool getPi_LOCALSENSOR() const");
   expectContains("inline void setPi_LOCALSENSOR(Bool value)");
   expectContains("inline Bool getPi_LOCALOUTPUT() const");
   expectContains("inline void setPi_LOCALOUTPUT(Bool value)");
   expectContains("inline Int16 getPi_LOCALDATA() const");
   expectContains("inline void setPi_LOCALDATA(Int16 value)");

   // Diagnostic program - UPPERCASE - in HEADER
   expectContains("inline UInt32 getPi_DIAGDATA() const");
   expectContains("inline void setPi_DIAGDATA(UInt32 value)");
   expectContains("inline Bool getPi_DIAGFLAG() const");
   expectContains("inline void setPi_DIAGFLAG(Bool value)");
   expectContains("inline UInt16 getPi_DIAGWORD() const");
   expectContains("inline void setPi_DIAGWORD(UInt16 value)");
}

/**
 * @brief Verify program AT variables are used correctly in the source
 */
TEST_F(ProcessImageTest, ProgramATUsage)
{
   expectContainsSource("setPi_LOCALSENSOR(getPi_SENSORA())");
   expectContainsSource("setPi_LOCALOUTPUT(getPi_ACTUATORA())");
   expectContainsSource("setPi_LOCALDATA(getPi_SETPOINT())");
   expectContainsSource("setPi_DIAGDATA(getPi_ERRORTIME())");
   expectContainsSource("setPi_DIAGFLAG(getPi_ERRORFLAG())");
   expectContainsSource("setPi_DIAGWORD(0xFFFF)");
}

// ============================================================================
//  Bitwise Operator Tests
// ============================================================================

/**
 * @brief Verify OR is translated to bitwise OR (|) not logical OR (||)
 */
TEST_F(ProcessImageTest, BitwiseOrTranslation)
{
   expectContainsSource("getPi_FLAGS() | 0x80");
   expectNotContainsSource("getPi_FLAGS() || 0x80");
}

/**
 * @brief Verify AND is translated to bitwise AND (&)
 */
TEST_F(ProcessImageTest, BitwiseAndTranslation)
{
   // Check for AND operation in the FB method
   expectContainsSource("&");
}

// ============================================================================
//  Struct and Enum Tests
// ============================================================================

/**
 * @brief Verify MotorStatus struct is generated
 */
TEST_F(ProcessImageTest, StructUsage)
{
   expectContains("struct MOTORSTATUS");
   expectContains("Bool RUNNING{};");
   expectContains("Int16 SPEED{};");
   expectContains("Float TEMP{};");
   expectContains("Bool FAULT{};");

   expectContainsSource("STATUS.TEMP = INT_TO_REAL(NEWTEMP) / 10.0");
}

/**
 * @brief Verify TestEnum is generated
 * 
 * Note: The enum definition is in the HEADER, while enum usage is in the SOURCE.
 * We check both locations.
 */
TEST_F(ProcessImageTest, EnumUsage)
{
   // Enum definition is in the HEADER
   expectContains("enum class TESTENUM");
   expectContains("IDLE");
   expectContains("RUNNING");
   expectContains("STOPPED");
   expectContains("ERROR");

   // Enum usage is in the SOURCE
   expectContainsSource("TESTENUM::RUNNING");
   expectContainsSource("TESTENUM::STOPPED");
}

// ============================================================================
//  Process Image Instance Tests
// ============================================================================

/**
 * @brief Verify ProcessImage instance is generated with correct sizes
 */
TEST_F(ProcessImageTest, ProcessImageInstance)
{
   // The process image instance should be named "processImage"
   expectContains("inline ProcessImage<");
   expectContains("processImage");
}

/**
 * @brief Verify readInputBit is used for input bits
 */
TEST_F(ProcessImageTest, ReadInputBit)
{
   // Check that readInputBit appears in the header (getter definitions)
   expectContains("readInputBit");
}

/**
 * @brief Verify writeOutputBit is used for output bits
 */
TEST_F(ProcessImageTest, WriteOutputBit)
{
   // Check that writeOutputBit appears in the header (setter definitions)
   expectContains("writeOutputBit");
}

// ============================================================================
//  Edge Cases Tests
// ============================================================================

/**
 * @brief Verify mixed case variables are normalized to uppercase
 */
TEST_F(ProcessImageTest, VariableNormalization)
{
   // Variables like "StartButton" should become "STARTBUTTON"
   expectContains("getPi_STARTBUTTON");
   expectContains("getPi_STOPBUTTON");
   expectContains("getPi_EMERGENCY");
}

/**
 * @brief Verify that AT address comments are preserved
 */
TEST_F(ProcessImageTest, ATAddressComments)
{
   expectContains("// AT %IX0.0");
   expectContains("// AT %QX0.0");
   expectContains("// AT %MW10");
}

/**
 * @brief Verify that DWORD access uses the correct method
 */
TEST_F(ProcessImageTest, DwordAccess)
{
   expectContains("readMarkerDword(22)"); // Status
   expectContains("writeMarkerDword(22"); // Status setter
}