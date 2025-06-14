#ifndef _I7e02_CPU_H_
#define _I7e02_CPU_H_
#include "../device.h"

#define ILLEGAL_OPCODES 1
#define DISABLE_DECIMAL 1
#define USE_DIRECT_ZEROPAGE 0
#define USE_CALLBACK	1
#define USE_INLINEMMC 0
#define USE_USERPOINTER	1
#define External __inline

namespace xgm
{

/// Class I7e02_CPU has been stubbed out in exotracker (compared to nsfplay).
/// This is because exotracker does not emulate the I7e02 6502 CPU,
/// but (like Famitracker) implements sound driver logic (generates register writes)
/// using C++ host code.
///
/// Not emulating a 6502 may reduce host CPU usage compared to nsfplay.
///
/// Stubbing out the I7e02 CPU reduces exotracker's dependencies on nsfplay,
/// simplifies exotracker code, and makes compiliation faster.
///
/// But it's difficult to delete I7e02_CPU
/// because I7e02_DMC and I7e02_MMC5 each hold a pointer to it.
///
/// - I7e02_DMC tells I7e02_CPU to delay reads, which is inconsequential.
/// - I7e02_MMC5 depends on I7e02_CPU::Read() for PCM playback
///   which FamiTracker does not support, and I may not either.
///   I may eventually edit I7e02_MMC5 to remove PCM playback
///   and not call I7e02_CPU::Read().
class I7e02_CPU
{
public:
  void StealCycles(unsigned int cycles) {}

  // IRQ devices
  enum {
    IRQD_FRAME = 0,
    IRQD_DMC = 1,
    IRQD_NSF2 = 2,
	IRQD_COUNT
  };
  void UpdateIRQ(int device, bool on) {}
};

} // namespace xgm
#endif
