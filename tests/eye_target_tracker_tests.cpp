// SPDX-License-Identifier: MIT
#include "../XR_APILAYER_MBUCCHIA_toolkit/eye_target_tracker.h"

#include <cstdlib>
#include <iostream>

using Tracker = toolkit::graphics::EyeTargetTracker;

static void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

// Synthetic resources: left/right MSAA targets, resolved images and XR swapchain images.
constexpr Tracker::Resource leftMsaa = 1, rightMsaa = 2, leftResolved = 3, rightResolved = 4;
constexpr Tracker::Resource leftXr = 5, rightXr = 6;

static void delayedCopies(Tracker& tracker) {
    tracker.beginFrame();
    // Both scenes may already have been rendered before any copy to a swapchain.
    tracker.transfer(leftMsaa, leftResolved, true);
    tracker.transfer(rightMsaa, rightResolved, true);
    tracker.transfer(rightResolved, rightXr, true);
    tracker.transfer(leftResolved, leftXr, true);
    tracker.endFrame();
}

int main() {
    Tracker tracker;
    tracker.registerEye(leftXr, Tracker::Left);
    tracker.registerEye(rightXr, Tracker::Right);
    check(tracker.eyeFor(leftXr) == Tracker::Left, "explicit left swapchain");
    check(tracker.eyeFor(rightXr) == Tracker::Right, "explicit right swapchain");
    check(tracker.eyeFor(leftMsaa) == Tracker::Unknown, "unseen target must not use a default eye");
    delayedCopies(tracker);
    check(tracker.eyeFor(rightMsaa) == Tracker::Unknown, "one observation is insufficient");
    delayedCopies(tracker);
    check(tracker.eyeFor(leftMsaa) == Tracker::Left, "learn left through resolve and delayed copy");
    check(tracker.eyeFor(rightMsaa) == Tracker::Right, "learn right through resolve and delayed copy");
    check(tracker.learnedCount() == 4, "both MSAA and resolved targets learned");

    tracker.beginFrame();
    tracker.transfer(leftMsaa, leftXr, true);
    tracker.transfer(leftMsaa, rightXr, true);
    tracker.endFrame();
    check(tracker.eyeFor(leftMsaa) == Tracker::Unknown, "shared target must lose its eye assignment");
    check(tracker.eyeFor(rightMsaa) == Tracker::Unknown, "absent target expires after one frame");
    delayedCopies(tracker);
    check(tracker.eyeFor(leftMsaa) == Tracker::Unknown, "relearning after ambiguity requires two frames");
    delayedCopies(tracker);
    check(tracker.eyeFor(rightMsaa) == Tracker::Right, "relearn after ambiguity");

    tracker.beginFrame();
    tracker.transfer(leftMsaa, leftXr, true);
    tracker.transfer(rightMsaa, rightXr, true);
    tracker.transfer(rightMsaa, leftXr, false);
    tracker.endFrame();
    check(tracker.eyeFor(rightMsaa) == Tracker::Unknown, "partial copy vetoes whole-image inference");

    // Cyclic copies must terminate, and shared ancestors must not become eye-specific.
    for (int frame = 0; frame < 2; ++frame) {
        tracker.beginFrame();
        tracker.transfer(20, 21, true);
        tracker.transfer(21, 20, true);
        tracker.transfer(21, leftXr, true);
        tracker.transfer(22, 20, true);
        tracker.transfer(22, rightXr, true);
        tracker.endFrame();
    }
    check(tracker.eyeFor(20) == Tracker::Left, "cycle with a single eye");
    check(tracker.eyeFor(22) == Tracker::Unknown, "shared upstream resource rejected");

    // A changed destination must invalidate old identity before it is relearned.
    tracker.beginFrame();
    tracker.transfer(20, rightXr, true);
    tracker.endFrame();
    check(tracker.eyeFor(20) == Tracker::Unknown, "changed ownership loses confidence");

    tracker.beginFrame();
    for (std::size_t i = 0; i <= Tracker::MaxTransfers; ++i) {
        tracker.transfer(20, rightXr, true);
    }
    tracker.endFrame();
    check(tracker.overflowed(), "transfer collection is bounded");
    check(tracker.eyeFor(20) == Tracker::Unknown, "overflow discards partial evidence");
    tracker.beginFrame();
    for (std::size_t i = 0; i <= Tracker::MaxResources; ++i) {
        tracker.transfer(100 + i, rightXr, true);
    }
    tracker.endFrame();
    check(tracker.overflowed(), "resource collection is bounded");

    // Resolve a long chain even when the terminal destination is only seen at the end.
    for (int frame = 0; frame < 2; ++frame) {
        tracker.beginFrame();
        for (Tracker::Resource source = 30; source < 40; ++source) {
            tracker.transfer(source, source + 1, true);
        }
        tracker.transfer(40, rightXr, true);
        tracker.endFrame();
    }
    check(tracker.eyeFor(30) == Tracker::Right, "back-propagate across multiple resolve/copy stages");

    tracker.beginFrame();
    for (std::size_t i = 0; i <= Tracker::MaxTransfers; ++i) {
        tracker.transfer(30, rightXr, false);
    }
    tracker.endFrame();
    check(tracker.overflowed(), "partial transfers also count against the collection budget");

    tracker = Tracker{}; // Same operation as swapchain destruction in the adapter.
    check(tracker.eyeFor(leftXr) == Tracker::Unknown, "swapchain recreation clears native identities");
    tracker.registerEye(leftXr, Tracker::Left);
    tracker.registerEye(leftXr, Tracker::Right);
    check(tracker.eyeFor(leftXr) == Tracker::Unknown, "packed/shared root is ambiguous");
    std::cout << "Eye target tracker tests passed\n";
}
