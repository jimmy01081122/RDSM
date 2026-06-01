#include "dsm_object.h"
#include "latency_sampler.h"
#include "occ_engine.h"
#include "rdma_conn.h"
#include "server_arbitration.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

static_assert(!std::is_copy_constructible<RDMAConnection>::value,
              "RDMAConnection must not be copy constructible");
static_assert(!std::is_copy_assignable<RDMAConnection>::value,
              "RDMAConnection must not be copy assignable");

struct OCCEngineTestAccess {
    static int try_acquire_locks(OCCEngine& engine, Transaction& tx) {
        return engine.try_acquire_locks(tx);
    }

    static int apply_write_set(OCCEngine& engine, Transaction& tx) {
        return engine.apply_write_set(tx);
    }
};

struct ServerArbitratorTestAccess {
    static void process_request(ServerArbitrator& arbitrator, ArbitrationRequest& req) {
        arbitrator.process_request(req);
    }
};

namespace {

void expect(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void test_partial_lock_rollback() {
    DSMObjectStore store;
    expect(store.create_object(1, TYPE_PRODUCT_STOCK, 10) == 0, "create object 1");
    expect(store.create_object(2, TYPE_PRODUCT_STOCK, 20) == 0, "create object 2");

    OCCEngine engine(&store);
    Transaction tx;
    engine.begin_transaction(tx);
    engine.write_object(tx, 1, 11);
    engine.write_object(tx, 2, 21);

    ObjectHeader* blocked = store.get_object_header(2);
    blocked->lock_bit = 1;
    blocked->lock_owner = 999;

    expect(OCCEngineTestAccess::try_acquire_locks(engine, tx) == -1,
           "partial lock acquisition must fail");
    ObjectHeader* rolled_back = store.get_object_header(1);
    expect(rolled_back->lock_bit == 0 && rolled_back->lock_owner == 0,
           "earlier owned lock must be rolled back");
    expect(blocked->lock_bit == 1 && blocked->lock_owner == 999,
           "unowned blocking lock must remain unchanged");
}

void test_duplicate_application_detector() {
    DSMObjectStore store;
    expect(store.create_object(1, TYPE_PRODUCT_STOCK, 10) == 0, "create duplicate object");

    OCCEngine engine(&store);
    Transaction tx;
    engine.begin_transaction(tx);
    engine.write_object(tx, 1, 11);

    expect(OCCEngineTestAccess::apply_write_set(engine, tx) == 0, "first apply");
    expect(OCCEngineTestAccess::apply_write_set(engine, tx) == 0, "second apply");
    expect(store.get_global_stats()->duplicate_commit_count.load() == 1,
           "second application must increment duplicate detector");
}

void test_counter_semantics_on_retry_exhaustion() {
    DSMObjectStore store;
    expect(store.create_object(1, TYPE_PRODUCT_STOCK, 10) == 0, "create retry object");
    store.get_global_stats()->logical_tx++;

    ObjectHeader* blocked = store.get_object_header(1);
    blocked->lock_bit = 1;
    blocked->lock_owner = 999;

    OCCEngine engine(&store);
    constexpr uint64_t kAttempts = 3;
    for (uint64_t i = 0; i < kAttempts; ++i) {
        Transaction tx;
        engine.begin_transaction(tx);
        engine.write_object(tx, 1, 11);
        expect(engine.commit_transaction(tx) == -1, "blocked OCC commit must fail");
    }
    store.get_global_stats()->final_abort_tx++;

    auto* stats = store.get_global_stats();
    expect(stats->logical_tx.load() ==
               stats->committed_tx.load() + stats->final_abort_tx.load() +
                   stats->business_abort_tx.load(),
           "logical outcome relationship");
    expect(stats->occ_attempts.load() == kAttempts, "OCC attempt count");
    expect(stats->occ_failed_attempts.load() == kAttempts, "failed OCC attempt count");
    expect(stats->occ_attempts.load() ==
               stats->cold_path_tx.load() + stats->occ_failed_attempts.load(),
           "OCC attempt relationship");
}

void test_arbitrator_abort_counted_once() {
    DSMObjectStore store;
    ServerArbitrationConfig config{true, "fifo", 8, 1};
    ServerArbitrator arbitrator(config, &store);
    ArbitrationRequest req{};
    req.tx_id = 1;
    req.write_values[404] = 1;

    ServerArbitratorTestAccess::process_request(arbitrator, req);
    expect(!req.committed, "missing-object arbitration request must abort");
    expect(arbitrator.get_arbitrated_aborts() == 1, "arbitrator abort must be counted once");
}

LatencySample sample(uint64_t tx_id) {
    LatencySample result;
    result.tx_id = tx_id;
    result.tx_start_ns = tx_id * 1000;
    result.tx_end_ns = result.tx_start_ns + tx_id * 1000;
    result.final_status = "committed";
    return result;
}

void test_bounded_rotation_mode() {
    expect(LatencySampler::parse_mode("bounded_rotation") == LatencySamplingMode::BoundedRotation,
           "canonical bounded_rotation mode");
    expect(LatencySampler::parse_mode("reservoir") == LatencySamplingMode::BoundedRotation,
           "legacy reservoir alias");
    expect(LatencySampler::mode_name(LatencySamplingMode::BoundedRotation) == "bounded_rotation",
           "canonical mode metadata");

    LatencySampler sampler(LatencySamplingMode::BoundedRotation, 3);
    for (uint64_t tx_id = 1; tx_id <= 5; ++tx_id) {
        sampler.record(sample(tx_id));
    }
    auto samples = sampler.samples();
    std::vector<uint64_t> ids;
    for (const auto& entry : samples) {
        ids.push_back(entry.tx_id);
    }
    std::sort(ids.begin(), ids.end());
    expect(ids == std::vector<uint64_t>({3, 4, 5}), "rotation buffer retains latest samples");
    expect(sampler.summary().latency_sample_count == 3, "rotation buffer remains bounded");
}

} // namespace

int main() {
    try {
        test_partial_lock_rollback();
        test_duplicate_application_detector();
        test_counter_semantics_on_retry_exhaustion();
        test_arbitrator_abort_counted_once();
        test_bounded_rotation_mode();
    } catch (const std::exception& ex) {
        std::cerr << "FAIL: " << ex.what() << '\n';
        return 1;
    }
    std::cout << "PASS: deterministic RDSM regressions\n";
    return 0;
}
