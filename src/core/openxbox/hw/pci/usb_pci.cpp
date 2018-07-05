/*
 * Portions of the code are based on Cxbx-Reloaded's OHCI LLE implementation.
 * The original copyright header is included below.
 */
// This is an open source non-commercial project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++ and C#: http://www.viva64.com
// ******************************************************************
// *
// *    .,-:::::    .,::      .::::::::.    .,::      .:
// *  ,;;;'````'    `;;;,  .,;;  ;;;'';;'   `;;;,  .,;;
// *  [[[             '[[,,[['   [[[__[[\.    '[[,,[['
// *  $$$              Y$$$P     $$""""Y$$     Y$$$P
// *  `88bo,__,o,    oP"``"Yo,  _88o,,od8P   oP"``"Yo,
// *    "YUMMMMMP",m"       "Mm,""YUMMMP" ,m"       "Mm,
// *
// *   Cxbx->devices->USBController->USBDevice.cpp
// *
// *  This file is part of the Cxbx project.
// *
// *  Cxbx and Cxbe are free software; you can redistribute them
// *  and/or modify them under the terms of the GNU General Public
// *  License as published by the Free Software Foundation; either
// *  version 2 of the license, or (at your option) any later version.
// *
// *  This program is distributed in the hope that it will be useful,
// *  but WITHOUT ANY WARRANTY; without even the implied warranty of
// *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// *  GNU General Public License for more details.
// *
// *  You should have recieved a copy of the GNU General Public License
// *  along with this program; see the file COPYING.
// *  If not, write to the Free Software Foundation, Inc.,
// *  59 Temple Place - Suite 330, Bostom, MA 02111-1307, USA.
// *
// *  (c) 2018 ergo720
// *  
// *  All rights reserved
// *
// ******************************************************************

#include <cassert>
#include <functional>

#include "openxbox/log.h"

#include "usb_pci.h"
#include "../ohci/ohci.h"

namespace openxbox {

#define SETUP_STATE_IDLE    0
#define SETUP_STATE_SETUP   1
#define SETUP_STATE_DATA    2
#define SETUP_STATE_ACK     3
#define SETUP_STATE_PARAM   4

USBPCIDevice::USBPCIDevice(uint16_t vendorID, uint16_t deviceID, uint8_t revisionID, uint8_t irqn, Cpu* cpu)
    : PCIDevice(PCI_HEADER_TYPE_NORMAL, vendorID, deviceID, revisionID,
        0x0c, 0x03, 0x10) // USB OHCI
    , m_irqn(irqn)
    , m_cpu(cpu)
{
}

USBPCIDevice::~USBPCIDevice() {
}

// PCI Device functions

void USBPCIDevice::Init() {
    RegisterBAR(0, 0x1000, PCI_BAR_TYPE_MEMORY); // 0xFED00000 - 0xFED00FFF  and  0xFED08000 - 0xFED08FFF

    if (m_irqn == 1) {
        m_PciPath = "pci.0:02.0";
    }
    else {
        m_PciPath = "pci.0:03.0";
    }

    m_HostController = new OHCI(m_cpu, m_irqn, this);
}

void USBPCIDevice::Reset() {
}

void USBPCIDevice::PCIMMIORead(int barIndex, uint32_t addr, uint32_t *value, uint8_t size) {
    // barIndex must be zero since we created the USB devices with a zero index in Init()
    assert(barIndex == 0);

    // read the register of the corresponding HC
    *value = m_HostController->OHCI_ReadRegister(addr);
}

void USBPCIDevice::PCIMMIOWrite(int barIndex, uint32_t addr, uint32_t value, uint8_t size) {
    // barIndex must be zero since we created the USB devices with a zero index in Init()
    assert(barIndex == 0);

    // write the register of the corresponding HC
    m_HostController->OHCI_WriteRegister(addr, value);
}


void USBPCIDevice::USB_RegisterPort(USBPort* Port, int Index, int SpeedMask, USBPortOps* Ops) {
    Port->PortIndex = Index;
    Port->SpeedMask = SpeedMask;
    Port->Operations = Ops;
    USB_PortLocation(Port, nullptr, Index + 1);
}

void USBPCIDevice::USB_PortReset(USBPort* Port) {
    XboxDeviceState* dev = Port->Dev;

    assert(dev != nullptr);
    USB_Detach(Port);
    USB_Attach(Port);
    USB_DeviceReset(dev);
}

void USBPCIDevice::USB_Detach(USBPort* Port) {
    XboxDeviceState* dev = Port->Dev;

    assert(dev != nullptr);
    assert(dev->State != USB_STATE_NOTATTACHED);
    m_HostController->OHCI_Detach(Port);
    dev->State = USB_STATE_NOTATTACHED;
}

void USBPCIDevice::USB_Attach(USBPort* Port) {
    XboxDeviceState* dev = Port->Dev;

    assert(dev != nullptr);
    assert(dev->Attached);
    assert(dev->State == USB_STATE_NOTATTACHED);
    Port->Operations->attach(Port);
    dev->State = USB_STATE_ATTACHED;
    USB_DeviceHandleAttach(dev);
}

void USBPCIDevice::USB_DeviceReset(XboxDeviceState* dev) {
    if (dev == nullptr || !dev->Attached) {
        return;
    }

    dev->RemoteWakeup = 0;
    dev->Addr = 0;
    dev->State = USB_STATE_DEFAULT;
    usb_device_handle_reset(dev);
}

XboxDeviceState* USBPCIDevice::USB_FindDevice(USBPort* Port, uint8_t Addr) {
    XboxDeviceState* dev = Port->Dev;

    if (dev == nullptr || !dev->Attached || dev->State != USB_STATE_DEFAULT) {
        return nullptr;
    }
    if (dev->Addr == Addr) {
        return dev;
    }

    return USB_DeviceFindDevice(dev, Addr);
}

USBEndpoint* USBPCIDevice::USB_GetEP(XboxDeviceState* Dev, int Pid, int Ep) {
    USBEndpoint* eps;

    if (Dev == nullptr) {
        return nullptr;
    }
    eps = (Pid == USB_TOKEN_IN) ? Dev->EP_in : Dev->EP_out;
    if (Ep == 0) {
        return &Dev->EP_ctl; // EndpointNumber zero represents the default control endpoint
    }
    assert(Pid == USB_TOKEN_IN || Pid == USB_TOKEN_OUT);
    assert(Ep > 0 && Ep <= USB_MAX_ENDPOINTS);

    return eps + Ep - 1;
}

void USBPCIDevice::USB_PacketSetup(USBPacket* p, int Pid, USBEndpoint* Ep, unsigned int Stream,
    uint64_t Id, bool ShortNotOK, bool IntReq) {
    assert(!USB_IsPacketInflight(p));
    assert(p->IoVec.IoVecStruct != nullptr);
    p->Id = Id;
    p->Pid = Pid;
    p->Endpoint = Ep;
    p->Stream = Stream;
    p->Status = USB_RET_SUCCESS;
    p->ActualLength = 0;
    p->Parameter = 0;
    p->ShortNotOK = ShortNotOK;
    p->IntReq = IntReq;
    p->Combined = nullptr;
    IoVecReset(&p->IoVec);
    p->State = USB_PACKET_SETUP;
}

bool USBPCIDevice::USB_IsPacketInflight(USBPacket* p) {
    return (p->State == USB_PACKET_QUEUED || p->State == USB_PACKET_ASYNC);
}

void USBPCIDevice::USB_PacketAddBuffer(USBPacket* p, void* ptr, size_t len) {
    IoVecAdd(&p->IoVec, ptr, len);
}

void USBPCIDevice::USB_HandlePacket(XboxDeviceState* dev, USBPacket* p) {
    if (dev == nullptr) {
        p->Status = USB_RET_NODEV;
        return;
    }
    assert(dev == p->Endpoint->Dev);
    assert(dev->State == USB_STATE_DEFAULT);
    USB_PacketCheckState(p, USB_PACKET_SETUP);
    assert(p->Endpoint != nullptr);

    // Submitting a new packet clears halt
    if (p->Endpoint->Halted) {
        assert(QTAILQ_EMPTY(&p->Endpoint->Queue));
        p->Endpoint->Halted = false;
    }

    if (QTAILQ_EMPTY(&p->Endpoint->Queue) || p->Endpoint->Pipeline || p->Stream) {
        USB_ProcessOne(p);
        if (p->Status == USB_RET_ASYNC) {
            // hcd drivers cannot handle async for isoc
            assert(p->Endpoint->Type != USB_ENDPOINT_XFER_ISOC);
            // using async for interrupt packets breaks migration
            assert(p->Endpoint->Type != USB_ENDPOINT_XFER_INT ||
                (dev->flags & (1 << USB_DEV_FLAG_IS_HOST)));
            p->State = USB_PACKET_ASYNC;
            QTAILQ_INSERT_TAIL(&p->Endpoint->Queue, p, Queue);
        }
        else if (p->Status == USB_RET_ADD_TO_QUEUE) {
            USB_QueueOne(p);
        }
        else {
            // When pipelining is enabled usb-devices must always return async,
            // otherwise packets can complete out of order!
            assert(p->Stream || !p->Endpoint->Pipeline ||
                QTAILQ_EMPTY(&p->Endpoint->Queue));
            if (p->Status != USB_RET_NAK) {
                p->State = USB_PACKET_COMPLETE;
            }
        }
    }
    else {
        USB_QueueOne(p);
    }
}

void USBPCIDevice::USB_QueueOne(USBPacket* p) {
    p->State = USB_PACKET_QUEUED;
    QTAILQ_INSERT_TAIL(&p->Endpoint->Queue, p, Queue);
    p->Status = USB_RET_ASYNC;
}

void USBPCIDevice::USB_PacketCheckState(USBPacket* p, USBPacketState expected) {
    if (p->State == expected) {
        return;
    }

    log_warning("USB: packet state check failed!\n");
    assert(0);
}

void USBPCIDevice::USB_ProcessOne(USBPacket* p) {
    XboxDeviceState* dev = p->Endpoint->Dev;

    // Handlers expect status to be initialized to USB_RET_SUCCESS, but it
    // can be USB_RET_NAK here from a previous usb_process_one() call,
    // or USB_RET_ASYNC from going through usb_queue_one().
    p->Status = USB_RET_SUCCESS;

    if (p->Endpoint->Num == 0) {
        // Info: All devices must support endpoint zero. This is the endpoint which receives all of the devices control 
        // and status requests during enumeration and throughout the duration while the device is operational on the bus
        if (p->Parameter) {
            USB_DoParameter(dev, p);
            return;
        }
        switch (p->Pid) {
        case USB_TOKEN_SETUP:
            USB_DoTokenSetup(dev, p);
            break;
        case USB_TOKEN_IN:
            DoTokenIn(dev, p);
            break;
        case USB_TOKEN_OUT:
            DoTokenOut(dev, p);
            break;
        default:
            p->Status = USB_RET_STALL;
        }
    }
    else {
        // data pipe
        USB_DeviceHandleData(dev, p);
    }
}

void USBPCIDevice::USB_DoParameter(XboxDeviceState* s, USBPacket* p) {
    int i, request, value, index;

    for (i = 0; i < 8; i++) {
        s->SetupBuffer[i] = p->Parameter >> (i * 8);
    }

    s->SetupState = SETUP_STATE_PARAM;
    s->SetupLength = (s->SetupBuffer[7] << 8) | s->SetupBuffer[6];
    s->SetupIndex = 0;

    request = (s->SetupBuffer[0] << 8) | s->SetupBuffer[1];
    value = (s->SetupBuffer[3] << 8) | s->SetupBuffer[2];
    index = (s->SetupBuffer[5] << 8) | s->SetupBuffer[4];

    if (s->SetupLength > sizeof(s->data_buf)) {
        log_debug("USB: ctrl buffer too small (%d > %zu)\n", s->SetupLength, sizeof(s->data_buf));
        p->Status = USB_RET_STALL;
        return;
    }

    if (p->Pid == USB_TOKEN_OUT) {
        USB_PacketCopy(p, s->data_buf, s->SetupLength);
    }

    USB_DeviceHandleControl(s, p, request, value, index, s->SetupLength, s->data_buf);
    if (p->Status == USB_RET_ASYNC) {
        return;
    }

    if (p->ActualLength < s->SetupLength) {
        s->SetupLength = p->ActualLength;
    }
    if (p->Pid == USB_TOKEN_IN) {
        p->ActualLength = 0;
        USB_PacketCopy(p, s->data_buf, s->SetupLength);
    }
}

void USBPCIDevice::USB_DoTokenSetup(XboxDeviceState* s, USBPacket* p) {
    int request, value, index;

    // From the standard "Every Setup packet has eight bytes."
    if (p->IoVec.Size != 8) {
        p->Status = USB_RET_STALL;
        return;
    }

    // Info: name, offset, size, info (sizes are in bytes)
    // bmRequestType, 1, 1, determines the direction of the request, type of request and designated recipient
    // bRequest, 1, 1, determines the request being made
    // wValue, 2, 2, it is used to pass a parameter to the device, specific to the request
    // wIndex, 4, 2, often used in requests to specify an endpoint or an interface
    // wLength, 6, 2, number of bytes to transfer if there is a data phase
    // The wValue and wIndex fields allow parameters to be passed with the request

    USB_PacketCopy(p, s->SetupBuffer, p->IoVec.Size);
    p->ActualLength = 0;
    s->SetupLength = (s->SetupBuffer[7] << 8) | s->SetupBuffer[6];
    s->SetupIndex = 0;

    request = (s->SetupBuffer[0] << 8) | s->SetupBuffer[1];
    value = (s->SetupBuffer[3] << 8) | s->SetupBuffer[2];
    index = (s->SetupBuffer[5] << 8) | s->SetupBuffer[4];

    if (s->SetupBuffer[0] & USB_DIR_IN) {
        USB_DeviceHandleControl(s, p, request, value, index, s->SetupLength, s->data_buf);
        if (p->Status == USB_RET_ASYNC) {
            s->SetupState = SETUP_STATE_SETUP;
        }
        if (p->Status != USB_RET_SUCCESS) {
            return;
        }

        if (p->ActualLength < s->SetupLength) {
            s->SetupLength = p->ActualLength;
        }
        s->SetupState = SETUP_STATE_DATA;
    }
    else {
        if (s->SetupLength > sizeof(s->data_buf)) {
            log_debug("USB: ctrl buffer too small (%d > %zu)\n", s->SetupLength, sizeof(s->data_buf));
            p->Status = USB_RET_STALL;
            return;
        }
        if (s->SetupLength == 0) {
            s->SetupState = SETUP_STATE_ACK;
        }
        else {
            s->SetupState = SETUP_STATE_DATA;
        }
    }

    p->ActualLength = 8;
}

void USBPCIDevice::DoTokenIn(XboxDeviceState* s, USBPacket* p) {
    int request, value, index;

    assert(p->Endpoint->Num == 0);

    request = (s->SetupBuffer[0] << 8) | s->SetupBuffer[1];
    value = (s->SetupBuffer[3] << 8) | s->SetupBuffer[2];
    index = (s->SetupBuffer[5] << 8) | s->SetupBuffer[4];

    switch (s->SetupState) {
    case SETUP_STATE_ACK:
        if (!(s->SetupBuffer[0] & USB_DIR_IN)) {
            USB_DeviceHandleControl(s, p, request, value, index, s->SetupLength, s->data_buf);
            if (p->Status == USB_RET_ASYNC) {
                return;
            }
            s->SetupState = SETUP_STATE_IDLE;
            p->ActualLength = 0;
        }
        break;

    case SETUP_STATE_DATA:
        if (s->SetupBuffer[0] & USB_DIR_IN) {
            int len = s->SetupLength - s->SetupIndex;
            if (len > p->IoVec.Size) {
                len = p->IoVec.Size;
            }
            USB_PacketCopy(p, s->data_buf + s->SetupIndex, len);
            s->SetupIndex += len;
            if (s->SetupIndex >= s->SetupLength) {
                s->SetupState = SETUP_STATE_ACK;
            }
            return;
        }
        s->SetupState = SETUP_STATE_IDLE;
        p->Status = USB_RET_STALL;
        break;

    default:
        p->Status = USB_RET_STALL;
    }
}

void USBPCIDevice::DoTokenOut(XboxDeviceState* s, USBPacket* p) {
    assert(p->Endpoint->Num == 0);

    switch (s->SetupState) {
    case SETUP_STATE_ACK:
        if (s->SetupBuffer[0] & USB_DIR_IN) {
            s->SetupState = SETUP_STATE_IDLE;
            /* transfer OK */
        }
        else {
            /* ignore additional output */
        }
        break;

    case SETUP_STATE_DATA:
        if (!(s->SetupBuffer[0] & USB_DIR_IN)) {
            int len = s->SetupLength - s->SetupIndex;
            if (len > p->IoVec.Size) {
                len = p->IoVec.Size;
            }
            USB_PacketCopy(p, s->data_buf + s->SetupIndex, len);
            s->SetupIndex += len;
            if (s->SetupIndex >= s->SetupLength) {
                s->SetupState = SETUP_STATE_ACK;
            }
            return;
        }
        s->SetupState = SETUP_STATE_IDLE;
        p->Status = USB_RET_STALL;
        break;

    default:
        p->Status = USB_RET_STALL;
    }
}

void USBPCIDevice::USB_PacketCopy(USBPacket* p, void* ptr, size_t bytes) {
    IOVector* iov = p->Combined ? &p->Combined->IoVec : &p->IoVec;

    assert(p->ActualLength >= 0);
    assert(p->ActualLength + bytes <= iov->Size);
    switch (p->Pid) {
    case USB_TOKEN_SETUP:
    case USB_TOKEN_OUT:
        IoVecTobuffer(iov->IoVecStruct, iov->IoVecNumber, p->ActualLength, ptr, bytes);
        break;
    case USB_TOKEN_IN:
        IoVecFromBuffer(iov->IoVecStruct, iov->IoVecNumber, p->ActualLength, ptr, bytes);
        break;
    default:
        log_fatal("USB: %s has an invalid pid: %x\n", __func__, p->Pid);
        break;
    }
    p->ActualLength += bytes;
}

int USBPCIDevice::USB_DeviceInit(XboxDeviceState* dev) {
    USBDeviceClass * klass = dev->klass;
    if (klass->init) {
        return klass->init(dev);

    }

    return 0;
}

XboxDeviceState* USBPCIDevice::USB_DeviceFindDevice(XboxDeviceState* dev, uint8_t Addr) {
    USBDeviceClass* klass = dev->klass;
    if (klass->find_device) {
        return klass->find_device(dev, Addr);
    }

    return nullptr;
}

void USBPCIDevice::USB_DeviceCancelPacket(XboxDeviceState* dev, USBPacket* p) {
    USBDeviceClass* klass = dev->klass;
    if (klass->cancel_packet) {
        klass->cancel_packet(dev, p);
    }
}

void USBPCIDevice::USB_DeviceHandleDestroy(XboxDeviceState* dev) {
    USBDeviceClass* klass = dev->klass;
    if (klass->handle_destroy) {
        klass->handle_destroy(dev);
    }
}

void USBPCIDevice::USB_DeviceHandleAttach(XboxDeviceState* dev) {
    USBDeviceClass* klass = dev->klass;
    if (klass->handle_attach) {
        klass->handle_attach(dev);
    }
}

void USBPCIDevice::USB_DeviceHandleReset(XboxDeviceState* dev) {
    USBDeviceClass* klass = dev->klass;
    if (klass->handle_reset) {
        klass->handle_reset(dev);
    }
}

void USBPCIDevice::USB_DeviceHandleControl(XboxDeviceState* dev, USBPacket* p, int request, int value, int index, int length, uint8_t* data) {
    USBDeviceClass* klass = dev->klass;
    if (klass->handle_control) {
        klass->handle_control(dev, p, request, value, index, length, data);
    }
}

void USBPCIDevice::USB_DeviceHandleData(XboxDeviceState* dev, USBPacket* p) {
    USBDeviceClass* klass = dev->klass;
    if (klass->handle_data) {
        klass->handle_data(dev, p);
    }
}

void USBPCIDevice::USB_DeviceSetInterface(XboxDeviceState* dev, int iface, int alt_old, int alt_new) {
    USBDeviceClass* klass = dev->klass;
    if (klass->set_interface) {
        klass->set_interface(dev, iface, alt_old, alt_new);
    }
}

void USBPCIDevice::USB_DeviceFlushEPqueue(XboxDeviceState* dev, USBEndpoint* ep) {
    USBDeviceClass *klass = dev->klass;
    if (klass->flush_ep_queue) {
        klass->flush_ep_queue(dev, ep);
    }
}


void USBPCIDevice::USB_DeviceEPstopped(XboxDeviceState* dev, USBEndpoint* EP) {
    USBDeviceClass* klass = dev->klass;
    if (klass->ep_stopped) {
        klass->ep_stopped(dev, EP);

    }
}

void USBPCIDevice::USB_CancelPacket(USBPacket* p) {
    bool callback = (p->State == USB_PACKET_ASYNC);
    assert(USB_IsPacketInflight(p));
    p->State = USB_PACKET_CANCELED;
    QTAILQ_REMOVE(&p->Endpoint->Queue, p, Queue);
    if (callback) {
        USB_DeviceCancelPacket(p->Endpoint->Dev, p);
    }
}

void USBPCIDevice::USB_EPsetType(XboxDeviceState* dev, int pid, int ep, uint8_t type) {
    USBEndpoint* uep = USB_GetEP(dev, pid, ep);
    uep->Type = type;
}

uint8_t USBPCIDevice::USB_EPsetIfnum(XboxDeviceState* dev, int pid, int ep, uint8_t ifnum) {
    USBEndpoint* uep = USB_GetEP(dev, pid, ep);
    uep->IfNum = ifnum;
}

void USBPCIDevice::USB_EPsetMaxPacketSize(XboxDeviceState* dev, int pid, int ep, uint16_t raw) {
    USBEndpoint* uep = USB_GetEP(dev, pid, ep);

    // Dropped from XQEMU the calculation max_packet_size = size * microframes since that's only true
    // for high speed (usb 2.0) devices
    uep->MaxPacketSize = raw & 0x7FF;
}

void USBPCIDevice::USB_PortLocation(USBPort* downstream, USBPort* upstream, int portnr) {
    if (upstream) {
        std::snprintf(downstream->Path, sizeof(downstream->Path), "%s.%d",
            upstream->Path, portnr);
        downstream->HubCount = upstream->HubCount + 1;
    }
    else {
        std::snprintf(downstream->Path, sizeof(downstream->Path), "%d", portnr);
        downstream->HubCount = 0;
    }
}

void USBPCIDevice::USB_DeviceAttach(XboxDeviceState* dev) {
    USBPort * port = dev->Port;

    assert(port != nullptr);
    assert(!dev->Attached);

    dev->Attached++;
    USB_Attach(port);
}

}
