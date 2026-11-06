#pragma once
namespace ld {
// Results in microseconds: p50,p95,p99,maximum,mean. Call on a diagnostic
// worker, never inside the 100 Hz rules tick.
void benchmarkDecisions(double results[5]);
}
