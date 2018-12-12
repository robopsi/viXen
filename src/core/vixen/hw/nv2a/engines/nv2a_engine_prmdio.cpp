// NV2A emulation for the Original Xbox
// (C) Ivan "StrikerX3" Oliveira
//
// NV2A PRMDIO (VGA DAC Registers) definitions.
//
// Portions based on envytools documentation:
//   https://envytools.readthedocs.io/en/latest/index.html
#pragma once

#include "nv2a_engine_prmdio.h"

namespace vixen {
namespace hw {
namespace nv2a {

NV2APRMDIOEngine::NV2APRMDIOEngine()
    : INV2AEngine(kEngine_PRMDIO)
{
}

NV2APRMDIOEngine::~NV2APRMDIOEngine() {
}

void NV2APRMDIOEngine::Stop() {
}

void NV2APRMDIOEngine::Reset() {
}

void NV2APRMDIOEngine::Read(uint32_t address, uint32_t *value, uint8_t size) {
    log_spew("NV2APRMDIOEngine::Read:  Unhandled read!   address = 0x%x,  size = %u\n", address, size);
    *value = 0;
}

void NV2APRMDIOEngine::Write(uint32_t address, uint32_t value, uint8_t size) {
    log_spew("NV2APRMDIOEngine::Write:  Unhandled write!   address = 0x%x,  value = 0x%x,  size = %u\n", address, value, size);
}

}
}
}
