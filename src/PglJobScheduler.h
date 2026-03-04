/**
 * @file PglJobScheduler.h
 * @brief Abstract job scheduler interface for platform-portable parallel dispatch.
 *
 * Provides a simple work-dispatch API that decouples the GPU pipeline from any
 * specific multi-core mechanism.  Platform implementations provide the actual
 * dispatch (RP2350 FIFO, FreeRTOS tasks, single-core serial, etc.).
 *
 * Usage:
 *   PglJob jobs[2] = {
 *       { RasterizeRange, &topCtx },
 *       { RasterizeRange, &bottomCtx }
 *   };
 *   scheduler->Submit(jobs, 2);
 *   scheduler->WaitAll(Hub75PollRefresh);
 *
 * The caller MAY be used as a worker — Submit() may execute one or more jobs
 * on the calling core before returning.
 *
 * See docs/Shader_Backend_And_Scheduler_Design.md §3 for design rationale.
 */

#pragma once

#include <cstdint>

// ─── Job Definition ─────────────────────────────────────────────────────────

/// A unit of parallel work.
struct PglJob {
    void (*func)(void* ctx);   ///< Job function pointer
    void* ctx;                 ///< Opaque context (caller-owned, must outlive execution)
};

// ─── Abstract Scheduler Interface ───────────────────────────────────────────

/// Platform-agnostic job scheduler.
///
/// All implementations must provide Submit() and WaitAll() with the following
/// contract:
///  - Submit(jobs, N) distributes N jobs across available workers.
///  - WaitAll() blocks until all submitted jobs complete.
///  - The scheduler may execute some jobs on the calling core.
///  - It is safe to Submit+WaitAll repeatedly (batch-per-phase model).
///  - Jobs within a single Submit() batch have no ordering guarantees.
class PglJobScheduler {
public:
    virtual ~PglJobScheduler() = default;

    /// Number of worker cores/threads available (excluding the calling core).
    /// A single-core scheduler returns 0.  RP2350 returns 1.
    virtual uint8_t WorkerCount() const = 0;

    /// Submit a batch of jobs for parallel execution.
    ///
    /// The scheduler distributes jobs across workers.  The implementation may
    /// also execute jobs on the caller's core (work-stealing / inline execution).
    ///
    /// @param jobs    Array of PglJob structs.  Must remain valid until WaitAll().
    /// @param count   Number of jobs (1..255).
    virtual void Submit(const PglJob* jobs, uint8_t count) = 0;

    /// Block until all jobs from the last Submit() batch are complete.
    ///
    /// @param idleFunc  Optional function called repeatedly while waiting
    ///                   (e.g. HUB75 display refresh).  May be nullptr.
    virtual void WaitAll(void (*idleFunc)() = nullptr) = 0;
};
