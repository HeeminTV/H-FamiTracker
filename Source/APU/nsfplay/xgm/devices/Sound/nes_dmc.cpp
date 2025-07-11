#include "nes_dmc.h"
#include "nes_apu.h"
#include "utils/variadic_minmax.h"
#include "nsfplay_math.h"
#include <cstdlib>

namespace xgm
{
    const UINT32 NES_DMC::wavlen_table[2][16] = {
    { // NTSC
      4, 8, 16, 32, 64, 96, 128, 160, 202, 254, 380, 508, 762, 1016, 2034, 4068
    },
    { // PAL
      4, 8, 14, 30, 60, 88, 118, 148, 188, 236, 354, 472, 708,  944, 1890, 3778
    } };

    const UINT32 NES_DMC::freq_table[2][16] = {
    { // NTSC
      428, 380, 340, 320, 286, 254, 226, 214, 190, 160, 142, 128, 106, 84, 72, 54
    },
    { // PAL
      398, 354, 316, 298, 276, 236, 210, 198, 176, 148, 132, 118,  98, 78, 66, 50
    } };

    const UINT32 BITREVERSE[256] = {
      0x00, 0x80, 0x40, 0xC0, 0x20, 0xA0, 0x60, 0xE0, 0x10, 0x90, 0x50, 0xD0, 0x30, 0xB0, 0x70, 0xF0,
      0x08, 0x88, 0x48, 0xC8, 0x28, 0xA8, 0x68, 0xE8, 0x18, 0x98, 0x58, 0xD8, 0x38, 0xB8, 0x78, 0xF8,
      0x04, 0x84, 0x44, 0xC4, 0x24, 0xA4, 0x64, 0xE4, 0x14, 0x94, 0x54, 0xD4, 0x34, 0xB4, 0x74, 0xF4,
      0x0C, 0x8C, 0x4C, 0xCC, 0x2C, 0xAC, 0x6C, 0xEC, 0x1C, 0x9C, 0x5C, 0xDC, 0x3C, 0xBC, 0x7C, 0xFC,
      0x02, 0x82, 0x42, 0xC2, 0x22, 0xA2, 0x62, 0xE2, 0x12, 0x92, 0x52, 0xD2, 0x32, 0xB2, 0x72, 0xF2,
      0x0A, 0x8A, 0x4A, 0xCA, 0x2A, 0xAA, 0x6A, 0xEA, 0x1A, 0x9A, 0x5A, 0xDA, 0x3A, 0xBA, 0x7A, 0xFA,
      0x06, 0x86, 0x46, 0xC6, 0x26, 0xA6, 0x66, 0xE6, 0x16, 0x96, 0x56, 0xD6, 0x36, 0xB6, 0x76, 0xF6,
      0x0E, 0x8E, 0x4E, 0xCE, 0x2E, 0xAE, 0x6E, 0xEE, 0x1E, 0x9E, 0x5E, 0xDE, 0x3E, 0xBE, 0x7E, 0xFE,
      0x01, 0x81, 0x41, 0xC1, 0x21, 0xA1, 0x61, 0xE1, 0x11, 0x91, 0x51, 0xD1, 0x31, 0xB1, 0x71, 0xF1,
      0x09, 0x89, 0x49, 0xC9, 0x29, 0xA9, 0x69, 0xE9, 0x19, 0x99, 0x59, 0xD9, 0x39, 0xB9, 0x79, 0xF9,
      0x05, 0x85, 0x45, 0xC5, 0x25, 0xA5, 0x65, 0xE5, 0x15, 0x95, 0x55, 0xD5, 0x35, 0xB5, 0x75, 0xF5,
      0x0D, 0x8D, 0x4D, 0xCD, 0x2D, 0xAD, 0x6D, 0xED, 0x1D, 0x9D, 0x5D, 0xDD, 0x3D, 0xBD, 0x7D, 0xFD,
      0x03, 0x83, 0x43, 0xC3, 0x23, 0xA3, 0x63, 0xE3, 0x13, 0x93, 0x53, 0xD3, 0x33, 0xB3, 0x73, 0xF3,
      0x0B, 0x8B, 0x4B, 0xCB, 0x2B, 0xAB, 0x6B, 0xEB, 0x1B, 0x9B, 0x5B, 0xDB, 0x3B, 0xBB, 0x7B, 0xFB,
      0x07, 0x87, 0x47, 0xC7, 0x27, 0xA7, 0x67, 0xE7, 0x17, 0x97, 0x57, 0xD7, 0x37, 0xB7, 0x77, 0xF7,
      0x0F, 0x8F, 0x4F, 0xCF, 0x2F, 0xAF, 0x6F, 0xEF, 0x1F, 0x9F, 0x5F, 0xDF, 0x3F, 0xBF, 0x7F, 0xFF,
    };

    NES_DMC::NES_DMC() : GETA_BITS(20)
    {
        SetClock(DEFAULT_CLOCK);
        SetRate(DEFAULT_RATE);
        SetPal(false);
        option[OPT_ENABLE_4011] = 1;
        option[OPT_ENABLE_PNOISE] = 1;
        option[OPT_UNMUTE_ON_RESET] = 1;
        option[OPT_DPCM_ANTI_CLICK] = 0;
        option[OPT_NONLINEAR_MIXER] = 1;
        option[OPT_RANDOMIZE_NOISE] = 1;
        option[OPT_RANDOMIZE_TRI] = 1;
        option[OPT_TRI_MUTE] = 1;
        option[OPT_DPCM_REVERSE] = 0;
        tnd_table[0][0][0][0] = 0;
        tnd_table[1][0][0][0] = 0;

        apu = NULL;
        frame_sequence_count = 0;
        frame_sequence_length = 7458;
        frame_sequence_steps = 4;

        for (int c = 0; c < 2; ++c)
            for (int t = 0; t < 3; ++t)
                sm[c][t] = 128;
    }


    NES_DMC::~NES_DMC()
    {
    }

    void NES_DMC::SetStereoMix(int trk, xgm::INT16 mixl, xgm::INT16 mixr)
    {
        if (trk < 0) return;
        if (trk > 2) return;
        sm[0][trk] = mixl;
        sm[1][trk] = mixr;
    }

    ITrackInfo* NES_DMC::GetTrackInfo(int trk)
    {
        switch (trk)
        {
        case 0:
            trkinfo[trk].max_volume = 255;
            trkinfo[0].key = (linear_counter > 0 && length_counter[0] > 0 && enable[0]);
            trkinfo[0].volume = 0;
            trkinfo[0]._freq = tri_freq;
            if (trkinfo[0]._freq)
                trkinfo[0].freq = clock / 32 / (trkinfo[0]._freq + 1);
            else
                trkinfo[0].freq = 0;
            trkinfo[0].tone = -1;
            trkinfo[0].output = out[0];
            break;
        case 1:
            trkinfo[1].max_volume = 15;
            trkinfo[1].volume = noise_volume + (envelope_disable ? 0 : 0x10) + (envelope_loop ? 0x20 : 0);
            trkinfo[1].key = length_counter[1] > 0 && enable[1] &&
                (envelope_disable ? (noise_volume > 0) : (envelope_counter > 0));
            trkinfo[1]._freq = reg[0x400e - 0x4008] & 0xF;
            trkinfo[1].freq = clock / double(wavlen_table[pal][trkinfo[1]._freq] * ((noise_tap & (1 << 6)) ? 93 : 1));
            trkinfo[1].tone = noise_tap & (1 << 6);
            trkinfo[1].output = out[1];
            break;
        case 2:
            trkinfo[2].max_volume = 127;
            trkinfo[2].volume = reg[0x4011 - 0x4008] & 0x7F;
            trkinfo[2].key = dlength > 0;
            trkinfo[2]._freq = reg[0x4010 - 0x4008] & 0xF;
            trkinfo[2].freq = clock / double(freq_table[pal][trkinfo[2]._freq]);
            trkinfo[2].tone = (0xc000 | (adr_reg << 6));
            trkinfo[2].output = (damp << 1) | dac_lsb;
            break;
        default:
            return NULL;
        }
        return &trkinfo[trk];
    }

    double NES_DMC::GetFrequencyTriangle() const  // // !!
    {
        if (!(linear_counter > 0 && length_counter[0] > 0
            && (!option[OPT_TRI_MUTE] || tri_freq > 0)))
            return 0.0;
        return clock / 32 / (tri_freq + 1);
    }

    double NES_DMC::GetFrequencyNoise() const     // // !!
    {
        if (!(length_counter[1] > 0 && enable[1] &&
            (envelope_disable ? (noise_volume > 0) : (envelope_counter > 0))))
            return 0.0;
        return clock / double(wavlen_table[pal][reg[0x400e - 0x4008] & 0xF] * ((noise_tap & (1 << 6)) ? 93 : 1));
    }

    double NES_DMC::GetFrequencyDPCM() const      // // !!
    {
        if ((data > 0x100) && !dlength)
            return 0.0;
        return clock / double(freq_table[pal][reg[0x4010 - 0x4008] & 0xF]);
    }


    UINT8 NES_DMC::GetSamplePos() const
    {
        return (daddress - ((adr_reg << 6) | 0xC000)) >> 6;
    }

    UINT8 NES_DMC::GetDeltaCounter() const
    {
        return (damp << 1) | dac_lsb;
    }

    bool NES_DMC::IsPlaying() const
    {
        return (dlength > 0);
    }

    void NES_DMC::FrameSequence(int s)
    {
        //DEBUG_OUT("FrameSequence: %d\n",s);

        if (s > 3) return; // no operation in step 4

        if (apu)
        {
            apu->FrameSequence(s);
        }

        if (s == 0 && (frame_sequence_steps == 4))
        {
            if (frame_irq_enable) frame_irq = true;
            cpu->UpdateIRQ(NES_CPU::IRQD_FRAME, frame_irq & frame_irq_enable);
        }

        // 240hz clock
        {
            // triangle linear counter
            if (linear_counter_halt)
            {
                linear_counter = linear_counter_reload;
            }
            else
            {
                if (linear_counter > 0) --linear_counter;
            }
            if (!linear_counter_control)
            {
                linear_counter_halt = false;
            }

            // noise envelope
            bool divider = false;
            if (envelope_write)
            {
                envelope_write = false;
                envelope_counter = 15;
                envelope_div = 0;
            }
            else
            {
                ++envelope_div;
                if (envelope_div > envelope_div_period)
                {
                    divider = true;
                    envelope_div = 0;
                }
            }
            if (divider)
            {
                if (envelope_loop && envelope_counter == 0)
                    envelope_counter = 15;
                else if (envelope_counter > 0)
                    --envelope_counter;
            }
        }

        // 120hz clock
        if ((s & 1) == 0)
        {
            // triangle length counter
            if (!linear_counter_control && (length_counter[0] > 0))
                --length_counter[0];

            // noise length counter
            if (!envelope_loop && (length_counter[1] > 0))
                --length_counter[1];
        }

    }

    UINT32 NES_DMC::calc_tri(UINT32 clocks)
    {
        static UINT32 tritbl[32] =
        {
         15,14,13,12,11,10, 9, 8,
          7, 6, 5, 4, 3, 2, 1, 0,
          0, 1, 2, 3, 4, 5, 6, 7,
          8, 9,10,11,12,13,14,15,
        };

        if (linear_counter > 0 && length_counter[0] > 0
            && (!option[OPT_TRI_MUTE] || tri_freq > 0))
        {
            counter[0] -= clocks;
            while (counter[0] < 0)
            {
                tphase = (tphase + 1) & 31;
                counter[0] += (tri_freq + 1);
            }
        }

        UINT32 ret = tritbl[tphase];
        return ret;
    }

    UINT32 NES_DMC::calc_noise(UINT32 clocks)
    {
        UINT32 env = envelope_disable ? noise_volume : envelope_counter;
        if (length_counter[1] < 1) env = 0;

        UINT32 last = (noise & 0x4000) ? 0 : env;
        if (clocks < 1) return last;

        // simple anti-aliasing (noise requires it, even when oversampling is off)
        UINT32 count = 0;
        UINT32 accum = counter[1] * last; // samples pending from previous calc
        UINT32 accum_clocks = counter[1];
#ifdef _DEBUG
        INT32 start_clocks = counter[1];
#endif
        if (counter[1] < 0) // only happens on startup when using the randomize noise option
        {
            accum = 0;
            accum_clocks = 0;
        }

        counter[1] -= clocks;
        assert(nfreq > 0); // prevent infinite loop
        while (counter[1] < 0)
        {
            // tick the noise generator
            UINT32 feedback = (noise & 1) ^ ((noise & noise_tap) ? 1 : 0);
            noise = (noise >> 1) | (feedback << 14);

            last = (noise & 0x4000) ? 0 : env;
            accum += (last * nfreq);
            counter[1] += nfreq;
            ++count;
            accum_clocks += nfreq;
        }

        if (count < 1) // no change over interval, don't anti-alias
        {
            return last;
        }

        accum -= (last * counter[1]); // remove these samples which belong in the next calc
        accum_clocks -= counter[1];
#ifdef _DEBUG
        if (start_clocks >= 0) assert(accum_clocks == clocks); // these should be equal
#endif

        UINT32 average = accum / accum_clocks;
        assert(average <= 15); // above this would indicate overflow
        return average;
    }

    // Tick the DMC for the number of clocks, and return output counter;
    UINT32 NES_DMC::calc_dmc(UINT32 clocks)
    {
        counter[2] -= clocks;
        assert(dfreq > 0); // prevent infinite loop
        while (counter[2] < 0)
        {
            counter[2] += dfreq;

            if (data > 0x100) // data = 0x100 when shift register is empty
            {
                if (!empty)
                {
                    if ((data & 1) && (damp < 63))
                        damp++;
                    else if (!(data & 1) && (0 < damp))
                        damp--;
                }
                data >>= 1;
            }

            if (data <= 0x100) // shift register is empty
            {
                if (dlength > 0)
                {
                    memory->Read(daddress, data);
                    cpu->StealCycles(4); // DMC read takes 3 or 4 CPU cycles, usually 4
                    // (checking for the 3-cycle case would require sub-instruction emulation)
                    data &= 0xFF; // read 8 bits
                    if (option[OPT_DPCM_REVERSE]) data = BITREVERSE[data];
                    data |= 0x10000; // use an extra bit to signal end of data
                    empty = false;
                    daddress = ((daddress + 1) & 0xFFFF) | 0x8000;
                    --dlength;
                    if (dlength == 0)
                    {
                        if (mode & 1) // looped DPCM = auto-reload
                        {
                            daddress = ((adr_reg << 6) | 0xC000);
                            dlength = (len_reg << 4) + 1;
                        }
                        else if (mode & 2) // IRQ and not looped
                        {
                            irq = true;
                            cpu->UpdateIRQ(NES_CPU::IRQD_DMC, true);
                        }
                    }
                }
                else
                {
                    data = 0x10000; // DMC will do nothing
                    empty = true;
                }
            }
        }

        return (damp << 1) + dac_lsb;
    }

    void NES_DMC::TickFrameSequence(UINT32 clocks)
    {
        frame_sequence_count += clocks;
        while (frame_sequence_count > frame_sequence_length)
        {
            FrameSequence(frame_sequence_step);
            frame_sequence_count -= frame_sequence_length;
            ++frame_sequence_step;
            if (frame_sequence_step >= frame_sequence_steps)
                frame_sequence_step = 0;
        }
    }

    void NES_DMC::Tick(UINT32 clocks)
    {
        out[0] = calc_tri(clocks);
        out[1] = calc_noise(clocks);
        out[2] = calc_dmc(clocks);
    }

    UINT32 NES_DMC::ClocksUntilLevelChange()
    {
        // See TickFrameSequence().
        // nsfplay is written strangely.
        // When a countdown is 0, it means the event will occur
        // on the next nonzero call to TickFrameSequence() or Tick().
        // See https://docs.google.com/document/d/1BnXwR3Avol7S5YNa3d4duGdbI6GNMwuYWLHuYiMZh5Y/edit#heading=h.lnh9d8j1x3uc
        // for discussion on how to handle 0.
        UINT32 out =
            (UINT32)value_or(frame_sequence_length - frame_sequence_count, frame_sequence_length);

        // See calc_tri().
        if (linear_counter > 0 && length_counter[0] > 0
            && (!option[OPT_TRI_MUTE] || tri_freq > 0)) {
            out = std::min(out, value_or(counter[0], tri_freq + 1));
        }

        // See calc_noise().
        // Unfortunately, at noise pitch $F, this updates every 4 clocks,
        // which drains CPU especially in debug builds.
        //
        // One solution is to only update noise intermittently,
        // However it's tricky to get right, and may require editing calc_noise().
        //
        // For now, just accept the increased CPU usage as a tradeoff.
        {
            UINT32 env = envelope_disable ? noise_volume : envelope_counter;
            if (length_counter[1] < 1) env = 0;
            if (env > 0) {
                out = std::min(out, [&] {
                    if (counter[1] < 0) {
                        // "only happens on startup when using the randomize noise option", idk what to return
                        return (UINT32)1;
                    }
                    return value_or((UINT32)counter[1], nfreq);
                    }());
            }
        }

        // See calc_dmc().
        // It's quite tricky to predict if the DMC is playing a sample or not,
        // so always assume it's playing.
        // But the amount of CPU overhead is minimal, because the DMC frequency never exceeds 33 kHz,
        // equivalent to a clock-skip of 54 (on NTSC, see `NES_DMC::freq_table`).
        auto clocks_until_dmc = value_or(counter[2], dfreq);
        out = std::min(out, clocks_until_dmc);

        return out;
    }

    UINT32 NES_DMC::Render(INT32 b[2])
    {
        out[0] = (mask & 1) ? 0 : out[0];
        out[1] = (mask & 2) ? 0 : out[1];
        out[2] = (mask & 4) ? 0 : out[2];

        INT32 m[3];
        m[0] = tnd_table[0][out[0]][0][0];
        m[1] = tnd_table[0][0][out[1]][0];
        m[2] = tnd_table[0][0][0][out[2]];

        if (option[OPT_NONLINEAR_MIXER])
        {
            INT32 ref = m[0] + m[1] + m[2];
            INT32 voltage = tnd_table[1][out[0]][out[1]][out[2]];
            if (ref)
            {
                for (int i = 0; i < 3; ++i)
                    m[i] = (m[i] * voltage) / ref;
            }
            else
            {
                for (int i = 0; i < 3; ++i)
                    m[i] = voltage;
            }
        }

        // anti-click nullifies any 4011 write but preserves nonlinearity
        if (option[OPT_DPCM_ANTI_CLICK])
        {
            if (dmc_pop) // $4011 will cause pop this frame
            {
                // adjust offset to counteract pop
                dmc_pop_offset += dmc_pop_follow - m[2];
                dmc_pop = false;

                // prevent overflow, keep headspace at edges
                const INT32 OFFSET_MAX = (1 << 30) - (4 << 16);
                if (dmc_pop_offset > OFFSET_MAX) dmc_pop_offset = OFFSET_MAX;
                if (dmc_pop_offset < -OFFSET_MAX) dmc_pop_offset = -OFFSET_MAX;
            }
            dmc_pop_follow = m[2]; // remember previous position

            m[2] += dmc_pop_offset; // apply offset

            // TODO implement this in a better way
            // roll off offset (not ideal, but prevents overflow)
            if (dmc_pop_offset > 0) --dmc_pop_offset;
            else if (dmc_pop_offset < 0) ++dmc_pop_offset;
        }

        b[0] = m[0] * sm[0][0];
        b[0] += m[1] * sm[0][1];
        b[0] += m[2] * sm[0][2];
        b[0] >>= 7;

        b[1] = m[0] * sm[1][0];
        b[1] += m[1] * sm[1][1];
        b[1] += m[2] * sm[1][2];
        b[1] >>= 7;

        return 2;
    }

    void NES_DMC::SetClock(double c)
    {
        clock = c;
    }

    void NES_DMC::SetRate(double r)
    {
        rate = (UINT32)(r ? r : DEFAULT_RATE);
    }

    void NES_DMC::SetPal(bool is_pal)
    {
        pal = (is_pal ? 1 : 0);
        // set CPU cycles in frame_sequence
        frame_sequence_length = is_pal ? 8314 : 7458;
    }

    void NES_DMC::SetAPU(NES_APU* apu_)
    {
        apu = apu_;
    }

    // Initializing TRI, NOISE, DPCM mixing table
    void NES_DMC::InitializeTNDTable(double wt, double wn, double wd) {

        // volume adjusted by 0.95 based on empirical measurements
        // MDFourier tests show that this seems to further deviate from hardware
        // see https://docs.google.com/document/d/1LIiskXiEBOyMX3j9SEjCB5hhUVRQFvG4eWz7dZC2cI8
        const double MASTER = 8192.0;// * 0.95;
      // truthfully, the nonlinear curve does not appear to match well
      // with my tests. Do more testing of the APU/DMC DAC later.
      // this value keeps the triangle consistent with measured levels,
      // but not necessarily the rest of this APU channel,
      // because of the lack of a good DAC model, currently.

        { // Linear Mixer
            for (int t = 0; t < 16; t++) {
                for (int n = 0; n < 16; n++) {
                    for (int d = 0; d < 128; d++) {
                        tnd_table[0][t][n][d] = (UINT32)(MASTER * (3.0 * t + 2.0 * n + d) / 208.0);
                    }
                }
            }
        }
        { // Non-Linear Mixer
            tnd_table[1][0][0][0] = 0;
            for (int t = 0; t < 16; t++) {
                for (int n = 0; n < 16; n++) {
                    for (int d = 0; d < 128; d++) {
                        if (t != 0 || n != 0 || d != 0)
                            tnd_table[1][t][n][d] = (UINT32)((MASTER * 159.79) / (100.0 + 1.0 / ((double)t / wt + (double)n / wn + (double)d / wd)));
                    }
                }
            }
        }

    }

    void NES_DMC::Reset()
    {
        int i;
        mask = 0;

        InitializeTNDTable(8227, 12241, 22638);

        counter[0] = 0;
        counter[1] = 0;
        counter[2] = 0;
        tphase = 0;
        nfreq = wavlen_table[0][0];
        dfreq = freq_table[0][0];
        tri_freq = 0;
        linear_counter = 0;
        linear_counter_reload = 0;
        linear_counter_halt = 0;
        linear_counter_control = 0;
        noise_volume = 0;
        noise = 0;
        noise_tap = 0;
        envelope_loop = 0;
        envelope_disable = 0;
        envelope_write = 0;
        envelope_div_period = 0;
        envelope_div = 0;
        envelope_counter = 0;
        enable[0] = 0;
        enable[1] = 0;
        length_counter[0] = 0;
        length_counter[1] = 0;
        frame_irq = false;
        frame_irq_enable = false;
        frame_sequence_count = 0;
        frame_sequence_steps = 4;
        frame_sequence_step = 0;
        cpu->UpdateIRQ(NES_CPU::IRQD_FRAME, false);

        for (i = 0; i < 0x0F; i++)
            Write(0x4008 + i, 0);
        Write(0x4017, 0x40);

        irq = false;
        Write(0x4015, 0x00);
        if (option[OPT_UNMUTE_ON_RESET])
            Write(0x4015, 0x0f);
        cpu->UpdateIRQ(NES_CPU::IRQD_DMC, false);

        out[0] = out[1] = out[2] = 0;
        damp = 0;
        dmc_pop = false;
        dmc_pop_offset = 0;
        dmc_pop_follow = 0;
        dac_lsb = 0;
        data = 0x100;
        empty = true;
        adr_reg = 0;
        dlength = 0;
        len_reg = 0;
        daddress = 0;
        noise = 1;
        noise_tap = (1 << 1);

        if (option[OPT_RANDOMIZE_NOISE])
        {
            noise |= ::rand();
            counter[1] = -(rand() & 511);
        }
        if (option[OPT_RANDOMIZE_TRI])
        {
            tphase = ::rand() & 31;
            counter[0] = -(rand() & 2047);
        }

        SetRate(rate);
    }

    void NES_DMC::SetMemory(IDevice* r)
    {
        memory = r;
    }

    void NES_DMC::SetOption(int id, int val)
    {
        if (id < OPT_END)
        {
            option[id] = val;
            if (id == OPT_NONLINEAR_MIXER)
                InitializeTNDTable(8227, 12241, 22638);
        }
    }

    bool NES_DMC::Write(UINT32 adr, UINT32 val, UINT32 id)
    {
        static const UINT8 length_table[32] = {
            0x0A, 0xFE,
            0x14, 0x02,
            0x28, 0x04,
            0x50, 0x06,
            0xA0, 0x08,
            0x3C, 0x0A,
            0x0E, 0x0C,
            0x1A, 0x0E,
            0x0C, 0x10,
            0x18, 0x12,
            0x30, 0x14,
            0x60, 0x16,
            0xC0, 0x18,
            0x48, 0x1A,
            0x10, 0x1C,
            0x20, 0x1E
        };

        if (adr == 0x4015)
        {
            enable[0] = (val & 4) ? true : false;
            enable[1] = (val & 8) ? true : false;

            if (!enable[0])
            {
                length_counter[0] = 0;
            }
            if (!enable[1])
            {
                length_counter[1] = 0;
            }

            if ((val & 16) && dlength == 0)
            {
                daddress = (0xC000 | (adr_reg << 6));
                dlength = (len_reg << 4) + 1;
            }
            else if (!(val & 16))
            {
                dlength = 0;
            }

            irq = false;
            cpu->UpdateIRQ(NES_CPU::IRQD_DMC, false);

            reg[adr - 0x4008] = val;
            return true;
        }

        if (adr == 0x4017)
        {
            //DEBUG_OUT("4017 = %02X\n", val);
            frame_irq_enable = ((val & 0x40) != 0x40);
            if (frame_irq_enable) frame_irq = false;
            cpu->UpdateIRQ(NES_CPU::IRQD_FRAME, false);

            frame_sequence_count = 0;
            if (val & 0x80)
            {
                frame_sequence_steps = 5;
                frame_sequence_step = 0;
                FrameSequence(frame_sequence_step);
                ++frame_sequence_step;
            }
            else
            {
                frame_sequence_steps = 4;
                frame_sequence_step = 1;
            }
        }

        if (adr < 0x4008 || 0x4013 < adr)
            return false;

        reg[adr - 0x4008] = val & 0xff;

        //DEBUG_OUT("$%04X %02X\n", adr, val);

        switch (adr)
        {

            // tri

        case 0x4008:
            linear_counter_control = (val >> 7) & 1;
            linear_counter_reload = val & 0x7F;
            break;

        case 0x4009:
            break;

        case 0x400a:
            tri_freq = val | (tri_freq & 0x700);
            break;

        case 0x400b:
            tri_freq = (tri_freq & 0xff) | ((val & 0x7) << 8);
            linear_counter_halt = true;
            if (enable[0])
            {
                length_counter[0] = length_table[(val >> 3) & 0x1f];
            }
            break;

            // noise

        case 0x400c:
            noise_volume = val & 15;
            envelope_div_period = val & 15;
            envelope_disable = (val >> 4) & 1;
            envelope_loop = (val >> 5) & 1;
            break;

        case 0x400d:
            break;

        case 0x400e:
            if (option[OPT_ENABLE_PNOISE])
                noise_tap = (val & 0x80) ? (1 << 6) : (1 << 1);
            else
                noise_tap = (1 << 1);
            nfreq = wavlen_table[pal][val & 15];
            break;

        case 0x400f:
            if (enable[1])
            {
                length_counter[1] = length_table[(val >> 3) & 0x1f];
            }
            envelope_write = true;
            break;

            // dmc

        case 0x4010:
            mode = (val >> 6) & 3;
            if (!(mode & 2))
            {
                irq = false;
                cpu->UpdateIRQ(NES_CPU::IRQD_DMC, false);
            }
            dfreq = freq_table[pal][val & 15];
            break;

        case 0x4011:
            if (option[OPT_ENABLE_4011])
            {
                damp = (val >> 1) & 0x3f;
                dac_lsb = val & 1;
                dmc_pop = true;
            }
            break;

        case 0x4012:
            adr_reg = val & 0xff;
            break;

        case 0x4013:
            len_reg = val & 0xff;
            break;

        default:
            return false;
        }

        return true;
    }

    bool NES_DMC::Read(UINT32 adr, UINT32& val, UINT32 id)
    {
        if (adr == 0x4015)
        {
            val |= (irq ? 0x80 : 0)
                | (frame_irq ? 0x40 : 0)
                | ((dlength > 0) ? 0x10 : 0)
                | (length_counter[1] ? 0x08 : 0)
                | (length_counter[0] ? 0x04 : 0)
                ;

            frame_irq = false;
            cpu->UpdateIRQ(NES_CPU::IRQD_FRAME, false);
            return true;
        }
        else if (0x4008 <= adr && adr <= 0x4014)
        {
            val |= reg[adr - 0x4008];
            return true;
        }
        else
            return false;
    }

    // IRQ support requires CPU read access
    void NES_DMC::SetCPU(NES_CPU* cpu_)
    {
        cpu = cpu_;
    }
} // namespace
