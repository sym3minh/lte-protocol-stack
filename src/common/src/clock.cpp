#include "clock.h"

namespace lte {

// ============================================================
// RealClock::instance
//
// Meyers singleton — constructed on first call, destroyed at
// program exit.  Thread-safe initialization guaranteed by C++11
// static local variable rules (§6.7 [stmt.dcl]).
// ============================================================
RealClock& RealClock::instance()
{
    static RealClock inst;
    return inst;
}

// ============================================================
// RealClock::now_ns
//
// This is the ONLY place in the production stack allowed to call
// std::chrono::steady_clock::now() directly.
// ============================================================
uint64_t RealClock::now_ns() const
{
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<nanoseconds>(
            steady_clock::now().time_since_epoch()
        ).count()
    );
}

} // namespace lte
