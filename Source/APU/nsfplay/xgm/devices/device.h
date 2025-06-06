#ifndef _DEVICE_H_
#define _DEVICE_H_
#include <stdio.h>
#include <vector>
#include <optional>
#include <assert.h>
#include "../xtypes.h"
#include "devinfo.h"
#include "../debugout.h"

namespace xgm
{
  const double DEFAULT_CLOCK = 1789772.0;
  const int DEFAULT_RATE = 48000;

  /**
   * s
   */
  class IDevice
  {
  public:
    /**
     s
     */
    virtual void Reset () = 0;

    /**
     s
     */
    virtual bool Write (UINT32 adr, UINT32 val, UINT32 id=0)=0;

    /**
     s
     */
    virtual bool Read (UINT32 adr, UINT32 & val, UINT32 id=0)=0;

    /**
     * s
     */
    virtual void SetOption (int id, int val){};
    virtual ~IDevice() {};
  };

  /**
   * s
   */
  class IRenderable
  {
  public:
    /**
     * s
     */
    virtual UINT32 Render (INT32 b[2]) = 0;

    // When seeking, this replaces Render
    virtual void Skip () {}

    /**
     *  chip update/operation is now bound to CPU clocks
     *  Render() now simply mixes and outputs sound
     */
    virtual void Tick (UINT32 clocks) {}
    virtual ~IRenderable() {};
  };

  /**
   * tqt
   */
  class ISoundChip : public IDevice, virtual public IRenderable
  {
  public:
    /**
     * Soundchip clocked by M2 (NTSC = ~1.789MHz)
     */
    virtual void Tick (UINT32 clocks) = 0;

    /**
     * This interface only allows you to advance time (Tick)
     * and poll for the latest amplitude (Render).
     * If you call Tick() with an argument greater than 1, you lose precision,
     * and with a small argument, you burn more CPU time.
     *
     * As an optimization,
     * this function returns the number of clocks before the next level change.
     * If the chip does not know how to compute it, it returns a placeholder value.
     */
    virtual UINT32 ClocksUntilLevelChange () {
        constexpr int NSFPLAY_RENDER_STEP = 4;
        return NSFPLAY_RENDER_STEP;
    }

    /**
     s
     */
    virtual void SetClock (double clock) = 0;

    /**
     s
     */
    virtual void SetRate (double rate) = 0;

    /**
     * Channel mask.
     */
    virtual void SetMask (int mask)=0;

    /**
     * Stereo mix.
     *   mixl = 0-256
     *   mixr = 0-256
     *     128 = neutral
     *     256 = double
     *     0 = nil
     *    <0 = inverted
     */
    virtual void SetStereoMix(int trk, xgm::INT16 mixl, xgm::INT16 mixr) = 0;

    /**
     * Track info for keyboard view.
     */
    virtual ITrackInfo *GetTrackInfo(int trk){ return NULL; }
    virtual ~ISoundChip() {};
  };

  /**
   s
   */
  class Bus : public IDevice
  {
  protected:
    std::vector < IDevice * > vd;
  public:
    /**
     s
     */
    void Reset ()
    {
      std::vector < IDevice * >::iterator it;
      for (it = vd.begin (); it != vd.end (); it++)
        (*it)->Reset ();
    }

    /**
     s
     */
    void DetachAll ()
    {
      vd.clear ();
    }

    /**
     s
     */
    void Attach (IDevice * d)
    {
      vd.push_back (d);
    }

    /**
     s
     */
    bool Write (UINT32 adr, UINT32 val, UINT32 id=0)
    {
      bool ret = false;
      std::vector < IDevice * >::iterator it;
      for (it = vd.begin (); it != vd.end (); it++)
        ret |= (*it)->Write (adr, val);
      return ret;
    }

    /**
     s
     */
    bool Read (UINT32 adr, UINT32 & val, UINT32 id=0)
    {
      bool ret = false;
      UINT32 vtmp = 0;
      std::vector < IDevice * >::iterator it;

      val = 0;
      for (it = vd.begin (); it != vd.end (); it++)
      {
        if ((*it)->Read (adr, vtmp))
        {
          val |= vtmp;
          ret = true;
        }
      }
      return ret;
    }
  };

  /**
   s
   */
  class Layer : public Bus
  {
  protected:
  public:
    /**
    s
     */
    bool Write (UINT32 adr, UINT32 val, UINT32 id=0)
    {
      std::vector < IDevice * >::iterator it;
      for (it = vd.begin (); it != vd.end (); it++)
        if ((*it)->Write (adr, val))
          return true;
      return false;
    }

    /**
     s
     */
    bool Read (UINT32 adr, UINT32 & val, UINT32 id=0)
    {
      std::vector < IDevice * >::iterator it;
      val = 0;
      for (it = vd.begin (); it != vd.end (); it++)
        if ((*it)->Read (adr, val))
          return true;
      return false;
    }
  };

}

#endif
