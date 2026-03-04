/**
 * @file PglJobScheduler_SingleCore.h
 * @brief Single-core (serial) job scheduler fallback.
 *
 * Executes all submitted jobs immediately on the calling core.
 * Zero overhead, no synchronisation primitives, no platform dependencies.
 *
 * Used for:
 *  - Single-core targets (RISC-V without second core, FPGA soft-CPU)
 *  - Desktop simulation / unit testing
 *  - Debugging (deterministic, sequential execution)
 */

#pragma once

#include "PglJobScheduler.h"

class PglJobScheduler_SingleCore : public PglJobScheduler {
public:
    uint8_t WorkerCount() const override { return 0; }

    void Submit(const PglJob* jobs, uint8_t count) override {
        // Execute all jobs serially on the calling core
        for (uint8_t i = 0; i < count; ++i) {
            if (jobs[i].func) {
                jobs[i].func(jobs[i].ctx);
            }
        }
    }

    void WaitAll(void (*/*idleFunc*/)()) override {
        // All jobs already completed in Submit()
    }
};
