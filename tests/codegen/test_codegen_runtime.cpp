/**
 * @file test_codegen_runtime.cpp
 * @brief C++ generation of the undoPLC runtime from a task configuration
 *
 * Verifies the RuntimeEmitter contract: the shape of the generated Master and
 * Worker classes, the order in which programs are called, per-task program
 * instances, the CPU resolution, the derived Master cycle and priority, and the
 * refusal to emit code that names a program the workspace does not declare.
 *
 * @author Salvatore Bamundo
 * @date 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#include <gtest/gtest.h>
#include "codegen/CodeGenerator.h"
#include "helpers/TestHelper.h"
#include "task/TaskConfigLoader.h"
#include <string>

#ifndef ST_SAMPLES_DIR
#define ST_SAMPLES_DIR "tests/st_samples"
#endif

namespace {

/**
 * @brief The workspace the runtime is generated against: two PROGRAM POUs with
 *        an FB each, which is the smallest thing that exercises programs,
 *        instances and program references.
 */
constexpr const char* kWorkspace = R"(FUNCTION_BLOCK FB_Motor
VAR_INPUT
   nSetpoint : INT;
END_VAR
VAR_OUTPUT
   nActual : INT;
END_VAR
nActual := nSetpoint;
END_FUNCTION_BLOCK

PROGRAM MAIN
VAR
   fbPump : FB_Motor;
END_VAR
fbPump(nSetpoint := 100);
END_PROGRAM

PROGRAM Control
VAR
   fbValve : FB_Motor;
END_VAR
fbValve(nSetpoint := 50);
END_PROGRAM
)";

/**
 * @brief A ready-to-use TranslationUnit for kWorkspace.
 */
TranslationUnit parseWorkspace()
{
   return TestHelper::parseST(kWorkspace);
}

/**
 * @brief A Task Configuration built in memory, bypassing the loader.
 */
st2cpp::task::TaskConfig makeConfig(std::vector<st2cpp::task::TaskEntry> tasks)
{
   st2cpp::task::TaskConfig config;
   config.tasks = std::move(tasks);
   return config;
}

/**
 * @brief One valid task, to be adjusted by the caller.
 */
st2cpp::task::TaskEntry task(std::string name, std::string plc, long long cycleMs, int priority)
{
   st2cpp::task::TaskEntry entry;
   entry.name = std::move(name);
   entry.plc = std::move(plc);
   entry.cycleMs = cycleMs;
   entry.priority = priority;
   return entry;
}

/**
 * @brief Generate the runtime for a configuration, failing the test on error.
 */
std::string generate(const st2cpp::task::TaskConfig& config)
{
   CodeGenerator gen;
   auto result = gen.generateRuntime(parseWorkspace(), config);
   EXPECT_TRUE(result.ok()) << (result.errors.empty() ? "" : result.errors.front());
   return result.content;
}

/**
 * @brief True when `code` contains `needle`.
 */
bool has(const std::string& code, const std::string& needle)
{
   return code.find(needle) != std::string::npos;
}

/**
 * @brief Index of `needle` in `code`, or npos.
 */
size_t where(const std::string& code, const std::string& needle)
{
   return code.find(needle);
}

/**
 * @brief How many times `needle` appears in `code`.
 */
size_t countOccurrences(const std::string& code, const std::string& needle)
{
   size_t count = 0;
   for (size_t at = code.find(needle); at != std::string::npos; at = code.find(needle, at + needle.size())) {
      ++count;
   }
   return count;
}

} // namespace

// ============================================================================
//  Entry point and file shape
// ============================================================================

/**
 * @brief The runtime is a translation unit with its own main()
 */
TEST(CodegenRuntime, EmitsStandaloneEntryPoint)
{
   const std::string code = generate(makeConfig({task("cycle", "Line1", 10, 40)}));

   EXPECT_TRUE(has(code, "#include \"undoTasks.hpp\""));
   EXPECT_TRUE(has(code, "int main(int argc, char* argv[])"));
   EXPECT_TRUE(has(code, "boost::asio::io_context"));
   EXPECT_TRUE(has(code, "boost::asio::signal_set"));
}

/**
 * @brief The generated classes live in their own namespace, so they cannot
 *        collide with the namespace the ST types are generated in
 */
TEST(CodegenRuntime, TaskClassesAreNamespaced)
{
   const std::string code = generate(makeConfig({task("cycle", "Line1", 10, 40)}));

   EXPECT_TRUE(has(code, "namespace st2cpp_generated"));
   EXPECT_TRUE(has(code, "class Line1_cycle : public UndoWorkerTaskBase"));
   EXPECT_TRUE(has(code, "class Line1 : public UndoMasterTaskBase"));
}

/**
 * @brief The Master derives from UndoMasterTaskBase, the Workers from
 *        UndoWorkerTaskBase: that pair is what turns a PLC into cyclic code
 */
TEST(CodegenRuntime, UsesTheUndoPlaBaseClasses)
{
   const std::string code = generate(makeConfig({task("cycle", "Line1", 10, 40)}));

   EXPECT_TRUE(has(code, ": UndoMasterTaskBase(cycleTimeNs)"));
   EXPECT_TRUE(has(code, ": UndoWorkerTaskBase(master, cpuId, prio)"));
   EXPECT_TRUE(has(code, "bool runWork() override"));
}

// ============================================================================
//  Master derivation
// ============================================================================

/**
 * @brief The Master runs at the finest cycle its tasks asked for, since it is
 *        the single clock undoPLC drives them all from
 */
TEST(CodegenRuntime, MasterCycleIsTheFastestTaskCycle)
{
   std::vector<st2cpp::task::TaskEntry> tasks;
   tasks.push_back(task("slow", "Line1", 50, 40));
   tasks.push_back(task("fast", "Line1", 10, 40));
   tasks.push_back(task("mid", "Line1", 20, 40));

   const std::string code = generate(makeConfig(tasks));

   // 10 ms expressed in nanoseconds, as the undoPLC constructor wants it.
   EXPECT_TRUE(has(code, "Line1 plc_Line1(10000000ULL)"));
}

/**
 * @brief The Master preempted its tasks, so an overrun cannot delay the cycle
 */
TEST(CodegenRuntime, MasterPriorityIsAboveEveryTask)
{
   std::vector<st2cpp::task::TaskEntry> tasks;
   tasks.push_back(task("a", "Line1", 10, 30));
   tasks.push_back(task("b", "Line1", 10, 50));

   const std::string code = generate(makeConfig(tasks));

   EXPECT_TRUE(has(code, "plc_Line1.start(51u)"));
   EXPECT_TRUE(has(code, "make_unique<Line1_a>(this, reserveAutomaticCpu(isolated, takenCpus), 30u)"));
   EXPECT_TRUE(has(code, "make_unique<Line1_b>(this, reserveAutomaticCpu(isolated, takenCpus), 50u)"));
}

/**
 * @brief Each PLC instance gets its own Master with its own cycle
 */
TEST(CodegenRuntime, OneMasterPerPlaInstance)
{
   std::vector<st2cpp::task::TaskEntry> tasks;
   tasks.push_back(task("a", "Line1", 10, 40));
   tasks.push_back(task("b", "Line2", 25, 40));

   const std::string code = generate(makeConfig(tasks));

   EXPECT_TRUE(has(code, "Line1 plc_Line1(10000000ULL)"));
   EXPECT_TRUE(has(code, "Line2 plc_Line2(25000000ULL)"));
   EXPECT_TRUE(has(code, "plc_Line1.stop();"));
   EXPECT_TRUE(has(code, "plc_Line2.stop();"));
}

/**
 * @brief Workers are registered before the Master starts, because a Worker
 *        registers itself with the Master it is constructed with
 */
TEST(CodegenRuntime, WorkersAreCreatedBeforeTheMasterStarts)
{
   const std::string code = generate(makeConfig({task("cycle", "Line1", 10, 40)}));

   const size_t create = where(code, "plc_Line1.createTasks();");
   const size_t start = where(code, "plc_Line1.start(");
   const size_t workerStart = where(code, "task->start()");
   const size_t registered = where(code, "plc_Line1.waitAllRegistered();");

   ASSERT_NE(create, std::string::npos);
   ASSERT_NE(start, std::string::npos);
   ASSERT_NE(workerStart, std::string::npos);
   EXPECT_LT(create, start);
   EXPECT_LT(start, workerStart);
   EXPECT_LT(workerStart, registered);
}

// ============================================================================
//  Program invocation
// ============================================================================

/**
 * @brief A Worker calls run() on the programs it lists
 */
TEST(CodegenRuntime, WorkerCallsItsPrograms)
{
   auto entry = task("cycle", "Line1", 10, 40);
   entry.programs = {"MAIN"};

   const std::string code = generate(makeConfig({entry}));

   EXPECT_TRUE(has(code, "_program_MAIN.run();"));
}

/**
 * @brief The declared order is the call order, the way TwinCAT runs the
 *        programs of a task
 */
TEST(CodegenRuntime, ProgramsAreCalledInDeclaredOrder)
{
   auto entry = task("cycle", "Line1", 10, 40);
   entry.programs = {"MAIN", "Control"};

   const std::string code = generate(makeConfig({entry}));

   const size_t first = where(code, "_program_MAIN.run();");
   const size_t second = where(code, "_program_CONTROL.run();");
   ASSERT_NE(first, std::string::npos);
   ASSERT_NE(second, std::string::npos);
   EXPECT_LT(first, second);
}

/**
 * @brief Program names are IEC identifiers, so they resolve case-insensitively
 */
TEST(CodegenRuntime, ProgramLookupIsCaseInsensitive)
{
   auto entry = task("cycle", "Line1", 10, 40);
   entry.programs = {"control"};

   const std::string code = generate(makeConfig({entry}));

   EXPECT_TRUE(has(code, "_program_CONTROL.run();"));
   EXPECT_FALSE(has(code, "error:"));
}

/**
 * @brief A task bound to two programs holding the same one gets one instance per
 *        task: a program called from two tasks must not share state between them
 */
TEST(CodegenRuntime, EachWorkerOwnsItsProgramInstances)
{
   auto first = task("a", "Line1", 10, 40);
   first.programs = {"MAIN"};
   auto second = task("b", "Line1", 10, 40);
   second.programs = {"MAIN"};

   const std::string code = generate(makeConfig({first, second}));

   EXPECT_TRUE(has(code, "class Line1_a"));
   EXPECT_TRUE(has(code, "class Line1_b"));
   // Two declarations, one inside each Worker: the program state is per task.
   EXPECT_EQ(countOccurrences(code, "MAIN _program_MAIN{};"), 2u);
}

/**
 * @brief A task with no program is legal and calls nothing
 */
TEST(CodegenRuntime, TaskWithoutProgramsIsEmitted)
{
   const std::string code = generate(makeConfig({task("safety", "Line1", 10, 30)}));

   EXPECT_TRUE(has(code, "class Line1_safety"));
   EXPECT_TRUE(has(code, "No program bound to this task."));
}

// ============================================================================
//  CPU assignment
// ============================================================================

/**
 * @brief An automatic task is given the first free isolated core, and the
 *        reserved cores are not handed out twice
 */
TEST(CodegenRuntime, AutomaticCpuIsResolvedPerPla)
{
   std::vector<st2cpp::task::TaskEntry> tasks;
   tasks.push_back(task("a", "Line1", 10, 40));
   tasks.push_back(task("b", "Line1", 10, 40));

   const std::string code = generate(makeConfig(tasks));

   EXPECT_TRUE(has(code, "reserveAutomaticCpu(isolated, takenCpus)"));
   EXPECT_TRUE(has(code, "UndoSys::getInstance().getIsolatedCpu()"));
   // Both tasks go through the resolver, which is what keeps them off each
   // other's core: takenCpus is appended to inside reserveAutomaticCpu.
   EXPECT_EQ(countOccurrences(code, "reserveAutomaticCpu(isolated, takenCpus), "), 2u);
   EXPECT_TRUE(has(code, "std::vector<uint16_t> takenCpus;"));
}

/**
 * @brief An explicit affinity is passed straight to the Worker constructor
 */
TEST(CodegenRuntime, ExplicitCpuAffinityIsPassedToTheWorker)
{
   auto entry = task("a", "Line1", 10, 40);
   entry.cpuAffinity = 5;

   const std::string code = generate(makeConfig({entry}));

   EXPECT_TRUE(has(code, "make_unique<Line1_a>(this, 5u, 40u)"));
   EXPECT_FALSE(has(code, "make_unique<Line1_a>(this, reserveAutomaticCpu"));
}

/**
 * @brief A pinned core is reserved so no automatic task can land on it
 */
TEST(CodegenRuntime, PinnedCpuIsReservedAgainstAutomaticTasks)
{
   std::vector<st2cpp::task::TaskEntry> tasks;
   tasks.push_back(task("pinned", "Line1", 10, 40));
   tasks.back().cpuAffinity = 3;
   tasks.push_back(task("auto", "Line1", 10, 40));

   const std::string code = generate(makeConfig(tasks));

   const size_t reserve = where(code, "takenCpus.push_back(3u);");
   const size_t use = where(code, "make_unique<Line1_auto>(this, reserveAutomaticCpu");
   ASSERT_NE(reserve, std::string::npos);
   ASSERT_NE(use, std::string::npos);
   EXPECT_LT(reserve, use);
}

/**
 * @brief When every task pins its own core the generated code carries no
 *        automatic-resolution machinery at all
 */
TEST(CodegenRuntime, NoAutomaticCpuMachineryWhenAllCpusArePinned)
{
   std::vector<st2cpp::task::TaskEntry> tasks;
   tasks.push_back(task("a", "Line1", 10, 40));
   tasks.back().cpuAffinity = 3;
   tasks.push_back(task("b", "Line1", 10, 40));
   tasks.back().cpuAffinity = 4;

   const std::string code = generate(makeConfig(tasks));

   EXPECT_TRUE(has(code, "Every task of this PLC pins its own core."));
   EXPECT_FALSE(has(code, "const std::vector<int>& isolated = UndoSys::getInstance().getIsolatedCpu();\n      std::vector<uint16_t> takenCpus;\n\n      // \"a\""));
}

// ============================================================================
//  Refusals
// ============================================================================

/**
 * @brief Naming a program that is not a PROGRAM POU cannot be resolved to a
 *        call, so nothing is emitted
 */
TEST(CodegenRuntime, UnknownProgramIsRejected)
{
   auto entry = task("cycle", "Line1", 10, 40);
   entry.programs = {"NoSuchProgram"};

   CodeGenerator gen;
   auto result = gen.generateRuntime(parseWorkspace(), makeConfig({entry}));

   EXPECT_FALSE(result.ok());
   ASSERT_FALSE(result.errors.empty());
   EXPECT_NE(result.errors.front().find("NoSuchProgram"), std::string::npos);
   // Nothing half-generated is handed back.
   EXPECT_TRUE(result.content.empty());
}

/**
 * @brief A FUNCTION_BLOCK is not callable as a program
 */
TEST(CodegenRuntime, NonProgramPouIsRejected)
{
   auto entry = task("cycle", "Line1", 10, 40);
   entry.programs = {"FB_Motor"};

   CodeGenerator gen;
   auto result = gen.generateRuntime(parseWorkspace(), makeConfig({entry}));

   EXPECT_FALSE(result.ok());
   EXPECT_FALSE(result.errors.empty());
}

/**
 * @brief Every bad reference is reported in one pass, not just the first
 */
TEST(CodegenRuntime, AllBadReferencesAreReported)
{
   std::vector<st2cpp::task::TaskEntry> tasks;
   tasks.push_back(task("a", "Line1", 10, 40));
   tasks.back().programs = {"MissingOne"};
   tasks.push_back(task("b", "Line1", 10, 40));
   tasks.back().programs = {"MissingTwo"};

   CodeGenerator gen;
   auto result = gen.generateRuntime(parseWorkspace(), makeConfig(tasks));

   EXPECT_FALSE(result.ok());
   EXPECT_EQ(result.errors.size(), 2u);
}

/**
 * @brief A workspace without any PROGRAM yields a runtime with no program calls
 *        rather than a wrong one
 */
TEST(CodegenRuntime, WorkspaceWithoutProgramsIsAccepted)
{
   CodeGenerator gen;
   auto tu = TestHelper::parseST("FUNCTION_BLOCK FB_Solo\nVAR_INPUT\n   i : INT;\nEND_VAR\nEND_FUNCTION_BLOCK\n");

   auto result = gen.generateRuntime(tu, makeConfig({task("cycle", "Line1", 10, 40)}));

   EXPECT_TRUE(result.ok());
   // Programs.hpp is not emitted when there is no PROGRAM, so it is not included.
   EXPECT_FALSE(has(result.content, "#include \"Programs.hpp\""));
}

/**
 * @brief Failing to pin the cpufreq governor is fatal: a PLC running with an
 *        unpinned governor would appear to work while its jitter is unbounded
 */
TEST(CodegenRuntime, FrequencyPinningFailureIsFatal)
{
   const std::string code = generate(makeConfig({task("cycle", "Line1", 10, 40)}));

   EXPECT_TRUE(has(code, "if (sys.setCpuNominalFrequency(isolated) == -1) {"));
   EXPECT_TRUE(has(code, "[ERROR] Cannot pin the isolated CPUs to their nominal frequency."));
   EXPECT_TRUE(has(code, "<< \"\\n          Root is needed to write cpufreq"));
   EXPECT_TRUE(has(code, "jitter is not bounded, so the PLC is not started."));
   // Fatal means the PLC does not come up at all, so there is nothing to warn
   // about afterwards.
   EXPECT_FALSE(has(code, "frequencyPinned"));
}

/**
 * @brief Not enough isolated cores stays fatal too: unlike the governor, the PLC
 *        cannot run at all without one core per Master plus one per task
 */
TEST(CodegenRuntime, TooFewIsolatedCpusIsStillFatal)
{
   const std::string code = generate(makeConfig({task("cycle", "Line1", 10, 40)}));

   EXPECT_TRUE(has(code, "[ERROR] This PLC needs 2 isolated CPUs but only "));
   EXPECT_TRUE(has(code, "Check your GRUB isolcpus= configuration."));
}

/**
 * @brief The startup path reports on plain streams, because logRT would drop it
 *
 * @details UndoLog::logRT only enqueues into a lock-free queue that
 * processLogs() drains from inside ioc.run(). A message logged before
 * ioc.run() starts is not displayed until later, and is lost for good if
 * main() returns first, since ~UndoLog() does not drain. A startup diagnostic
 * that disappears exactly when the next check fails is worse than none.
 */
TEST(CodegenRuntime, StartupDiagnosticsDoNotGoThroughTheLogger)
{
   const std::string code = generate(makeConfig({task("cycle", "Line1", 10, 40)}));

   // The PLC summary is the message most likely to be swallowed, because the
   // isolated-CPU and governor checks come right after it and can bail out.
   EXPECT_TRUE(has(code, "std::cout << \"[Main] PLC 'Line1': 1 task(s), cycle 10 ms, master priority 41\""));
   EXPECT_FALSE(has(code, "logger.logRT(LogDomain::PLC,\n                LOG_INFO,"));
}

/**
 * @brief The shutdown path cannot use logRT either: it runs on a thread that
 *        was never registered
 *
 * @details The async_wait handler executes on the io_context thread, which never
 * calls registerThread(), and logRT returns silently for an unregistered thread.
 * closeRegistration() happens before the handler is installed, so registering it
 * at that point is not possible either. A logRT call there vanishes with no
 * trace, which is what happened before this was fixed.
 */
TEST(CodegenRuntime, ShutdownPathDoesNotUseLogRtOnTheUnregisteredIoThread)
{
   const std::string code = generate(makeConfig({task("cycle", "Line1", 10, 40)}));

   EXPECT_TRUE(has(code, "[Main] Termination signal ("));
   EXPECT_FALSE(has(code, "Termination signal (%d) received"));
   // The reason is recorded in the generated source so nobody "fixes" it back.
   EXPECT_TRUE(has(code, "logRT returns silently for"));
}
