// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <utility>
#include <vector>

namespace toolkit::graphics {

    // Experimental whole-image ownership inference. The caller keeps native resources alive
    // through the following frame, and registers only separate, full-eye swapchain images.
    // A resource shared by both eyes is deliberately not assigned an eye.
    class EyeTargetTracker {
      public:
        using Resource = std::uintptr_t;
        enum Ownership : unsigned { Unknown = 0, Left = 1, Right = 2, Ambiguous = 3 };
        static constexpr std::size_t MaxResources = 128;
        static constexpr std::size_t MaxTransfers = 256;

        void registerEye(Resource resource, Ownership eye) {
            m_roots[resource] |= eye;
            m_previous.clear();
            m_confirmed.clear();
        }

        void beginFrame() {
            m_transfers.clear();
            m_resources.clear();
            m_unsupported.clear();
            m_overflow = false;
            m_transferCount = 0;
        }

        // Returns false if this frame exceeded the bounded collection budget.
        bool transfer(Resource source, Resource destination, bool wholeImage) {
            if (m_overflow) {
                return false;
            }
            m_resources.insert(source);
            m_resources.insert(destination);
            if (m_resources.size() > MaxResources || ++m_transferCount > MaxTransfers) {
                m_overflow = true;
                return false;
            }
            if (wholeImage) {
                m_transfers.emplace_back(source, destination);
            } else {
                // A partial copy, array/mip operation or deferred-context recording cannot
                // establish whole-image ownership, even if another full copy was observed.
                m_unsupported.insert(source);
                m_unsupported.insert(destination);
            }
            return true;
        }

        void endFrame() {
            if (m_overflow) {
                m_previous.clear();
                m_confirmed.clear();
                return;
            }

            std::map<Resource, unsigned> owners = m_roots;
            for (const auto resource : m_unsupported) {
                owners[resource] = Ambiguous;
            }

            // Propagate backwards through every copy/resolve chain. Unioning both eye
            // labels rejects reused intermediate buffers irrespective of copy order.
            bool changed;
            do {
                changed = false;
                for (const auto& transfer : m_transfers) {
                    const unsigned destination = owners[transfer.second];
                    auto& source = owners[transfer.first];
                    const unsigned combined = source | destination;
                    changed |= source != combined;
                    source = combined;
                }
            } while (changed);

            m_confirmed.clear();
            for (const auto& entry : owners) {
                const auto previous = m_previous.find(entry.first);
                if (isSingleEye(entry.second) && previous != m_previous.end() && previous->second == entry.second) {
                    m_confirmed.emplace(entry);
                }
            }
            m_previous = std::move(owners);
        }

        Ownership eyeFor(Resource resource) const {
            // Direct swapchain images have an explicit OpenXR eye, with no learning delay.
            const auto root = m_roots.find(resource);
            if (root != m_roots.end()) {
                return isSingleEye(root->second) ? static_cast<Ownership>(root->second) : Unknown;
            }
            const auto found = m_confirmed.find(resource);
            return found != m_confirmed.end() ? static_cast<Ownership>(found->second) : Unknown;
        }

        std::size_t learnedCount() const {
            std::size_t count = 0;
            for (const auto& entry : m_confirmed) {
                count += !m_roots.count(entry.first);
            }
            return count;
        }

        std::size_t transferCount() const { return m_transfers.size(); }
        bool overflowed() const { return m_overflow; }

      private:
        static bool isSingleEye(unsigned value) { return value == Left || value == Right; }

        std::map<Resource, unsigned> m_roots;
        std::map<Resource, unsigned> m_previous;
        std::map<Resource, unsigned> m_confirmed;
        std::vector<std::pair<Resource, Resource>> m_transfers;
        std::set<Resource> m_resources;
        std::set<Resource> m_unsupported;
        bool m_overflow{false};
        std::size_t m_transferCount{0};
    };

} // namespace toolkit::graphics
