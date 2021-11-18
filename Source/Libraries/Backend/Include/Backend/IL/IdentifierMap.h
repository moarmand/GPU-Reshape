#pragma once

// Backend
#include "OpaqueInstructionRef.h"

// Common
#include <Common/Assert.h>

// Std
#include <list>
#include <algorithm>

namespace IL {
    struct IdentifierMap {
        /// Allocate a new ID
        /// \return
        ID AllocID() {
            map.emplace_back();
            return map.size() - 1;
        }

        /// Set the number of bound ids
        /// \param id the capacity
        void SetBound(uint32_t bound) {
            if (map.size() > bound) {
                return;
            }

            map.resize(bound);
        }

        /// Get the maximum id
        ID GetMaxID() const {
            return map.size();
        }

        /// Add a mapped instruction
        /// \param ref the opaque reference, must be mutable
        /// \param result the resulting id
        void AddInstruction(const OpaqueInstructionRef& ref, ID result) {
            ASSERT(result != InvalidID, "Mapping instruction with invalid result");
            map.at(result) = ref;
        }

        /// Remove a mapped instruction
        /// \param result the resulting id
        void RemoveInstruction(ID result) {
            ASSERT(result != InvalidID, "Unmapping instruction with invalid result");
            map.at(result) = {};
        }

        /// Get a mapped instruction
        /// \param id the resulting id
        /// \return may be invalid if not mapped
        const OpaqueInstructionRef& Get(const ID& id) const {
            return map.at(id);
        }

    private:
        std::vector<OpaqueInstructionRef> map;
    };
}
