// NV2A emulation for the Original Xbox
// (C) Ivan "StrikerX3" Oliveira
//
// NV2A PMC (Master Control) definitions.
//
// Portions based on envytools documentation:
//   https://envytools.readthedocs.io/en/latest/index.html
#pragma once

#include <cstdint>

#include "nv2a_engine.h"

namespace vixen {
namespace hw {
namespace nv2a {

class NV2APMCEngine : public INV2AEngine {
public:
    NV2APMCEngine();
    ~NV2APMCEngine();

    void Stop() override;
    void Reset() override;

    void Read(uint32_t address, uint32_t *value, uint8_t size) override;
    void Write(uint32_t address, uint32_t value, uint8_t size) override;

private:
    // Card identification, returned on PMCRegID
    uint32_t m_cardID;

    // Enabled engines
    uint32_t m_enable;

    // Sets the enabled engines and propagates the settings to the engines
    void SetEnable(uint32_t value);
};

}
}
}
