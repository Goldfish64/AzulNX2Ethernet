/*
 *
 * Copyright (c) 2021 Goldfish64
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef __PHY_H__
#define __PHY_H__

#define MBit 1000000

#define BIT(x)              (1 << (x))

#define NX2_MIREG(x)        ((x & 0x1F) << 16)
#define NX2_MIPHY(x)        ((x & 0x1F) << 21)
#define NX2_PHY_TIMEOUT     50

#define PHY_FAIL            UINT32_MAX

//
// PHY registers.
//
#define PHY_MII_CONTROL                     0x00
#define PHY_MII_CONTROL_DUPLEX_FULL         BIT(8)
#define PHY_MII_CONTROL_AUTO_NEG_RESTART    BIT(9)
#define PHY_MII_CONTROL_POWER_DOWN          BIT(11)
#define PHY_MII_CONTROL_AUTO_NEG_ENABLE     BIT(12)
#define PHY_MII_CONTROL_LOOPBACK            BIT(14)
#define PHY_MII_CONTROL_RESET               BIT(15)
#define PHY_MII_CONTROL_SPEED_10            0
#define PHY_MII_CONTROL_SPEED_100           BIT(13)
#define PHY_MII_CONTROL_SPEED_1000          BIT(6)

#define PHY_MII_STATUS                      0x01
#define PHY_MII_STATUS_LINK_UP              BIT(2)
#define PHY_MII_STATUS_AUTO_NEG_COMPLETE    BIT(5)

#define PHY_ID_HIGH                         0x02
#define PHY_ID_LOW                          0x03

#define PHY_AUTO_NEG_ADVERT                 0x04
#define PHY_AUTO_NEG_ADVERT_802_3           BIT(0)
#define PHY_AUTO_NEG_ADVERT_10HD            BIT(5)
#define PHY_AUTO_NEG_ADVERT_10FD            BIT(6)
#define PHY_AUTO_NEG_ADVERT_100HD           BIT(7)
#define PHY_AUTO_NEG_ADVERT_100FD           BIT(8)
#define PHY_AUTO_NEG_ADVERT_PAUSE_CAP       BIT(10)
#define PHY_AUTO_NEG_ADVERT_PAUSE_ASYM      BIT(11)

#define PHY_AUTO_NEG_PARTNER                0x05
#define PHY_AUTO_NEG_PARTNER_10HD           BIT(5)
#define PHY_AUTO_NEG_PARTNER_10FD           BIT(6)
#define PHY_AUTO_NEG_PARTNER_100HD          BIT(7)
#define PHY_AUTO_NEG_PARTNER_100FD          BIT(8)
#define PHY_AUTO_NEG_PARTNER_PAUSE_CAP      BIT(10)
#define PHY_AUTO_NEG_PARTNER_PAUSE_ASYM     BIT(11)

#define PHY_1000BASET_CONTROL                   0x09
#define PHY_1000BASET_CONTROL_ADVERT_1000HD     BIT(8)
#define PHY_1000BASET_CONTROL_ADVERT_1000FD     BIT(9)

#define PHY_1000BASET_STATUS                    0x0A
#define PHY_1000BASET_STATUS_PARTNER_1000HD     BIT(8)
#define PHY_1000BASET_STATUS_PARTNER_1000FD     BIT(9)

#define PHY_EXTENDED_CONTROL                        0x10
#define PHY_EXTENDED_CONTROL_DISABLE_AUTO_MDIX      BIT(14)

#define PHY_AUX_CONTROL_SHADOW                      0x18
#define PHY_AUX_CONTROL_SHADOW_SELECT_MISC_CONTROL  0x7007

#define PHY_MISC_CONTROL_WIRESPEED_ENABLE           BIT(4)
#define PHY_MISC_CONTROL_FORCE_AUTO_MDIX            BIT(9)
#define PHY_MISC_CONTROL_WRITE_ENABLE               BIT(15)

#define PHY_AUX_STATUS                      0x19
#define PHY_AUX_STATUS_LINK_UP              BIT(2)
#define PHY_AUX_STATUS_SPEED_10HD           BIT(8)
#define PHY_AUX_STATUS_SPEED_10FD           BIT(9)
#define PHY_AUX_STATUS_SPEED_100HD          (BIT(8) | BIT(9))
#define PHY_AUX_STATUS_SPEED_100FD          (BIT(8) | BIT(10))
#define PHY_AUX_STATUS_SPEED_1000HD         (BIT(9) | BIT(10))
#define PHY_AUX_STATUS_SPEED_1000FD         (BIT(8) | BIT(9) | BIT(10))
#define PHY_AUX_STATUS_AUTO_NEG_COMPLETE    BIT(15)
#define PHY_AUX_STATUS_SPEED_MASK           (BIT(8) | BIT(9) | BIT(10))

#define PHY_INTERRUPT_MASK                  0x1B
#define PHY_INTERRUPT_MASK_LINK_INTS         0xFF00

//
// Medium types.
//
enum {
  kMediumTypeIndexAuto = 0,
  kMediumTypeIndex10HD,
  kMediumTypeIndex10FD,
  kMediumTypeIndex100HD,
  kMediumTypeIndex100FD,
  kMediumTypeIndex1000HD,
  kMediumTypeIndex1000FD,
  kMediumTypeCount
};

//
// Link duplex.
//
enum link_duplex
{
  kLinkDuplexFull,
  kLinkDuplexHalf,
  kLinkDuplexNegotiate,
  kLinkDuplexNone
};

//
// Link speed.
//
enum link_speed {
  kLinkSpeedNone = 0,
  kLinkSpeed10 = 10,
  kLinkSpeed100 = 100,
  kLinkSpeed1000 = 1000,
  kLinkSpeedNegotiate
};

typedef struct {
  link_duplex   duplex;
  link_speed    speed;
  bool          linkUp;
} phy_media_state_t;

#endif
