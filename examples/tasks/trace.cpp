// Demo-only observer for the tasks example. st2cpp does NOT generate this file.
//
// The PLC increments a VAR_GLOBAL counter inside each PROGRAM body, so counting
// how much each counter grows per second tells you exactly how the task
// configuration was turned into calls. This file prints them.
//
// Real PLCs do not do this. Sampling RT-written state from an ordinary thread is
// fine for a demo but is not something to copy into production code: go through
// the process image or a dedicated diagnostic buffer instead.
//
// The generated Runtime.cpp owns main(), so this file must not define one. The
// thread is started from a static initialiser and the process exiting tears it
// down, which is all a demo needs.

#include <chrono>
#include <cstdio>
#include <thread>

#include "GVLs.hpp"

namespace {

struct Counters {
   int32_t mainCalls;
   int32_t controlCalls;
   int32_t reportCalls;
};

Counters readCounters() {
   return {undoCore::NMAINCALLS, undoCore::NCONTROLCALLS, undoCore::NREPORTCALLS};
}

int32_t perSecond(int32_t now, int32_t before) {
   return now - before;
}

void observe() {
   // Give the Master and Worker threads a moment to start before sampling.
   std::this_thread::sleep_for(std::chrono::seconds(1));

   Counters previous = readCounters();
   std::printf("[trace] --- sampling once a second, Ctrl+C to stop ---\n");

   for (;;) {
      std::this_thread::sleep_for(std::chrono::seconds(1));

      const Counters current = readCounters();
      std::printf("[trace] last second: MAIN %d/s, Control %d/s, Report %d/s   (totals: %d, %d, %d)\n",
                  perSecond(current.mainCalls, previous.mainCalls),
                  perSecond(current.controlCalls, previous.controlCalls),
                  perSecond(current.reportCalls, previous.reportCalls),
                  current.mainCalls, current.controlCalls, current.reportCalls);
      std::fflush(stdout);

      previous = current;
   }
}

struct Observer {
   Observer() {
      std::printf("[trace] expected: MAIN, Control and Report all at 100/s"
                  " (one 10 ms task calls all three)\n");
      std::fflush(stdout);
      // Detached: the process exiting is what stops this, and a joinable thread
      // still running at exit would abort instead.
      std::thread(observe).detach();
   }
};

const Observer g_observer;

}  // namespace
