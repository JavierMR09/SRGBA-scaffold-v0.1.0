#include "srgba/core/scheduler.hpp"

#include <catch2/catch_test_macros.hpp>

using srgba::core::EventType;
using srgba::core::Scheduler;

TEST_CASE("The scheduler dispatches due events in deadline order", "[scheduler]") {
    Scheduler scheduler;
    scheduler.reset();
    scheduler.schedule(EventType::ScanlineEnd, 100);
    scheduler.schedule(EventType::HBlankStart, 40);

    REQUIRE(scheduler.next_deadline() == 40U);
    REQUIRE_FALSE(scheduler.pop_due().has_value());

    scheduler.advance(150);
    const auto first = scheduler.pop_due();
    REQUIRE(first.has_value());
    REQUIRE(first->type == EventType::HBlankStart);
    REQUIRE(first->timestamp == 40U);
    const auto second = scheduler.pop_due();
    REQUIRE(second.has_value());
    REQUIRE(second->type == EventType::ScanlineEnd);
    REQUIRE_FALSE(scheduler.pop_due().has_value());
    REQUIRE(scheduler.next_deadline() == Scheduler::kUnscheduled);
}

TEST_CASE("Halted time skips directly to the next event", "[scheduler]") {
    Scheduler scheduler;
    scheduler.reset();
    scheduler.schedule(EventType::HBlankStart, 1006);
    REQUIRE(scheduler.skip_to_next_event() == 1006U);
    REQUIRE(scheduler.now() == 1006U);
    REQUIRE(scheduler.skip_to_next_event() == 0U);

    scheduler.cancel(EventType::HBlankStart);
    REQUIRE_FALSE(scheduler.is_scheduled(EventType::HBlankStart));
}
