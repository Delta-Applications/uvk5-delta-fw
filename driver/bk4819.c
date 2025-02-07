/* Copyright 2023 Dual Tachyon
 * https://github.com/DualTachyon
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 */

#include "bk4819.h"
#include "../bsp/dp32g030/gpio.h"
#include "../bsp/dp32g030/portcon.h"
#include "../driver/gpio.h"
#include "../driver/system.h"
#include "../driver/systick.h"
#include "../driver/uart.h"
#include "../misc.h"

static const uint16_t FSK_RogerTable[7] = {
    0xF1A2, 0x7446, 0x61A4, 0x6544, 0x4E8A, 0xE044, 0xEA84,
};

static uint16_t gBK4819_GpioOutState;

bool gRxIdleMode;

__inline uint16_t scale_freq(const uint16_t freq)
{
//	return (((uint32_t)freq * 1032444u) + 50000u) / 100000u;   // with rounding
	return (((uint32_t)freq * 1353245u) + (1u << 16)) >> 17;   // with rounding
}



void BK4819_Init(void) {
  GPIO_SetBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SCN);
  GPIO_SetBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SCL);
  GPIO_SetBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SDA);

  BK4819_WriteRegister(BK4819_REG_00, 0x8000);
  BK4819_WriteRegister(BK4819_REG_00, 0x0000);
  BK4819_WriteRegister(BK4819_REG_37, 0x1D0F);
  BK4819_WriteRegister(BK4819_REG_36, 0x0022);
  BK4819_SetAGC(0);
  BK4819_WriteRegister(BK4819_REG_19, 0x1041);
  BK4819_WriteRegister(BK4819_REG_7D, 0xE94F);
  BK4819_WriteRegister(BK4819_REG_48, 0xB3A8);

  BK4819_WriteRegister(BK4819_REG_1F, 0x5454);
  BK4819_WriteRegister(BK4819_REG_3E, 0xA037);
  gBK4819_GpioOutState = 0x9000;
  BK4819_WriteRegister(BK4819_REG_33, 0x9000);
  BK4819_WriteRegister(BK4819_REG_3F, 0);
}

static uint16_t BK4819_ReadU16(void) {
  uint8_t i;
  uint16_t Value;

  PORTCON_PORTC_IE = (PORTCON_PORTC_IE & ~PORTCON_PORTC_IE_C2_MASK) |
                     PORTCON_PORTC_IE_C2_BITS_ENABLE;
  GPIOC->DIR = (GPIOC->DIR & ~GPIO_DIR_2_MASK) | GPIO_DIR_2_BITS_INPUT;
  SYSTICK_DelayUs(1);

  Value = 0;
  for (i = 0; i < 16; i++) {
    Value <<= 1;
    Value |= GPIO_CheckBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SDA);
    GPIO_SetBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SCL);
    SYSTICK_DelayUs(1);
    GPIO_ClearBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SCL);
    SYSTICK_DelayUs(1);
  }
  PORTCON_PORTC_IE = (PORTCON_PORTC_IE & ~PORTCON_PORTC_IE_C2_MASK) |
                     PORTCON_PORTC_IE_C2_BITS_DISABLE;
  GPIOC->DIR = (GPIOC->DIR & ~GPIO_DIR_2_MASK) | GPIO_DIR_2_BITS_OUTPUT;

  return Value;
}

uint16_t BK4819_ReadRegister(BK4819_REGISTER_t Register) {
  uint16_t Value;

  GPIO_SetBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SCN);
  GPIO_ClearBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SCL);
  SYSTICK_DelayUs(1);
  GPIO_ClearBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SCN);

  BK4819_WriteU8(Register | 0x80);

  Value = BK4819_ReadU16();

  GPIO_SetBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SCN);
  SYSTICK_DelayUs(1);
  GPIO_SetBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SCL);
  GPIO_SetBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SDA);

  return Value;
}

void BK4819_WriteRegister(BK4819_REGISTER_t Register, uint16_t Data) {
  GPIO_SetBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SCN);
  GPIO_ClearBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SCL);
  SYSTICK_DelayUs(1);
  GPIO_ClearBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SCN);
  BK4819_WriteU8(Register);
  SYSTICK_DelayUs(1);
  BK4819_WriteU16(Data);
  SYSTICK_DelayUs(1);
  GPIO_SetBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SCN);
  SYSTICK_DelayUs(1);
  GPIO_SetBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SCL);
  GPIO_SetBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SDA);
}

void BK4819_WriteU8(uint8_t Data) {
  uint8_t i;

  GPIO_ClearBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SCL);
  for (i = 0; i < 8; i++) {
    if ((Data & 0x80U) == 0) {
      GPIO_ClearBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SDA);
    } else {
      GPIO_SetBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SDA);
    }
    SYSTICK_DelayUs(1);
    GPIO_SetBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SCL);
    SYSTICK_DelayUs(1);
    Data <<= 1;
    GPIO_ClearBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SCL);
    SYSTICK_DelayUs(1);
  }
}

void BK4819_WriteU16(uint16_t Data) {
  uint8_t i;

  GPIO_ClearBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SCL);
  for (i = 0; i < 16; i++) {
    if ((Data & 0x8000U) == 0U) {
      GPIO_ClearBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SDA);
    } else {
      GPIO_SetBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SDA);
    }
    SYSTICK_DelayUs(1);
    GPIO_SetBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SCL);
    Data <<= 1;
    SYSTICK_DelayUs(1);
    GPIO_ClearBit(&GPIOC->DATA, GPIOC_PIN_BK4819_SCL);
    SYSTICK_DelayUs(1);
  }
}

void BK4819_SetAGC(uint8_t Value) {
  if (Value == 0) {
    BK4819_WriteRegister(BK4819_REG_13, 0x03BE);
    BK4819_WriteRegister(BK4819_REG_12, 0x037B);
    BK4819_WriteRegister(BK4819_REG_11, 0x027B);
    BK4819_WriteRegister(BK4819_REG_10, 0x007A);
    BK4819_WriteRegister(BK4819_REG_14, 0x0019);
    BK4819_WriteRegister(BK4819_REG_49, 0x2A38);
    BK4819_WriteRegister(BK4819_REG_7B, 0x8420);
  } else if (Value == 1) {
    uint8_t i;

    BK4819_WriteRegister(BK4819_REG_13, 0x03BE);
    BK4819_WriteRegister(BK4819_REG_12, 0x037C);
    BK4819_WriteRegister(BK4819_REG_11, 0x027B);
    BK4819_WriteRegister(BK4819_REG_10, 0x007A);
    BK4819_WriteRegister(BK4819_REG_14, 0x0018);
    BK4819_WriteRegister(BK4819_REG_49, 0x2A38);
    BK4819_WriteRegister(BK4819_REG_7B, 0x318C);
    BK4819_WriteRegister(BK4819_REG_7C, 0x595E);
    BK4819_WriteRegister(BK4819_REG_20, 0x8DEF);
    for (i = 0; i < 8; i++) {
      BK4819_WriteRegister(0x06, (i & 7) << 13 | 0x4A << 7 | 0x36);
    }
    /* for (i = 0; i < 8; i++) {
      BK4819_WriteRegister(BK4819_REG_06, ((i << 13) | 0x2500U) + 0x36U);
    } */
  }
}

void BK4819_ToggleGpioOut(BK4819_GPIO_PIN_t Pin, bool bSet) {
  if (bSet) {
    gBK4819_GpioOutState |= (0x40U >> Pin);
  } else {
    gBK4819_GpioOutState &= ~(0x40U >> Pin);
  }

  BK4819_WriteRegister(BK4819_REG_33, gBK4819_GpioOutState);
}

void BK4819_SetCDCSSCodeWord(uint32_t CodeWord) {
  // Enable CDCSS
  // Transmit positive CDCSS code
  // CDCSS Mode
  // CDCSS 23bit
  // Enable Auto CDCSS Bw Mode
  // Enable Auto CTCSS Bw Mode
  // CTCSS/CDCSS Tx Gain1 Tuning = 51
  BK4819_WriteRegister(
      BK4819_REG_51,
      0 | BK4819_REG_51_ENABLE_CxCSS | BK4819_REG_51_GPIO6_PIN2_NORMAL |
          BK4819_REG_51_TX_CDCSS_POSITIVE | BK4819_REG_51_MODE_CDCSS |
          BK4819_REG_51_CDCSS_23_BIT | BK4819_REG_51_1050HZ_NO_DETECTION |
          BK4819_REG_51_AUTO_CDCSS_BW_ENABLE |
          BK4819_REG_51_AUTO_CTCSS_BW_ENABLE |
          (51U << BK4819_REG_51_SHIFT_CxCSS_TX_GAIN1));

  // CTC1 Frequency Control Word = 2775
  BK4819_WriteRegister(BK4819_REG_07,
                       0 | BK4819_REG_07_MODE_CTC1 |
                           (2775U << BK4819_REG_07_SHIFT_FREQUENCY));

  // Set the code word
  BK4819_WriteRegister(BK4819_REG_08, 0x0000 | ((CodeWord >> 0) & 0xFFF));
  BK4819_WriteRegister(BK4819_REG_08, 0x8000 | ((CodeWord >> 12) & 0xFFF));
}

void BK4819_SetCTCSSFrequency(uint32_t FreqControlWord) {
  uint16_t Config;

  if (FreqControlWord == 2625) { // Enables 1050Hz detection mode
    // Enable TxCTCSS
    // CTCSS Mode
    // 1050/4 Detect Enable
    // Enable Auto CDCSS Bw Mode
    // Enable Auto CTCSS Bw Mode
    // CTCSS/CDCSS Tx Gain1 Tuning = 74
    Config = 0x944A;
  } else {
    // Enable TxCTCSS
    // CTCSS Mode
    // Enable Auto CDCSS Bw Mode
    // Enable Auto CTCSS Bw Mode
    // CTCSS/CDCSS Tx Gain1 Tuning = 74
    Config = 0x904A;
  }
  BK4819_WriteRegister(BK4819_REG_51, Config);
  // CTC1 Frequency Control Word
  BK4819_WriteRegister(BK4819_REG_07, 0 | BK4819_REG_07_MODE_CTC1 |
                                          ((FreqControlWord * 2065) / 1000)
                                              << BK4819_REG_07_SHIFT_FREQUENCY);
}

void BK4819_Set55HzTailDetection(void) {
  // CTC2 Frequency Control Word = round_nearest(25391 / 55) = 462
  BK4819_WriteRegister(BK4819_REG_07, (1U << 13) | 462);
}

void BK4819_EnableVox(uint16_t VoxEnableThreshold,
                      uint16_t VoxDisableThreshold) {
  // VOX Algorithm
  // if(voxamp>VoxEnableThreshold)       VOX = 1;
  // else if(voxamp<VoxDisableThreshold) (After Delay) VOX = 0;
  uint16_t REG_31_Value;

  REG_31_Value = BK4819_ReadRegister(BK4819_REG_31);
  // 0xA000 is undocumented?
  BK4819_WriteRegister(BK4819_REG_46, 0xA000 | (VoxEnableThreshold & 0x07FF));
  // 0x1800 is undocumented?
  BK4819_WriteRegister(BK4819_REG_79, 0x1800 | (VoxDisableThreshold & 0x07FF));
  // Bottom 12 bits are undocumented, 15:12 vox disable delay *128ms
  BK4819_WriteRegister(BK4819_REG_7A,
                       0x289A); // vox disable delay = 128*5 = 640ms
  // Enable VOX
  BK4819_WriteRegister(BK4819_REG_31, REG_31_Value | 4); // bit 2 - VOX Enable
}

const uint16_t listenBWRegValues[3] = {
    0x3028,             // 25
    0x4048,             // 12.5
    0b0000000000011000, // was 0x205C, // 6.25
};

void BK4819_SetFilterBandwidth(BK4819_FilterBandwidth_t Bandwidth) {
  BK4819_WriteRegister(BK4819_REG_43, listenBWRegValues[Bandwidth]);
}

void BK4819_SetupPowerAmplifier(uint16_t Bias, uint32_t Frequency) {
  uint8_t Gain;

  if (Bias > 255) {
    Bias = 255;
  }
  if (Frequency < 28000000) {
    // Gain 1 = 1
    // Gain 2 = 0
    Gain = 0x08U;
  } else {
    // Gain 1 = 4
    // Gain 2 = 2
    Gain = 0x22U;
  }
  // Enable PACTLoutput
  BK4819_WriteRegister(BK4819_REG_36, (Bias << 8) | 0x80U | Gain);
}

void BK4819_SetFrequency(uint32_t Frequency) {
  BK4819_WriteRegister(BK4819_REG_38, (Frequency >> 0) & 0xFFFF);
  BK4819_WriteRegister(BK4819_REG_39, (Frequency >> 16) & 0xFFFF);
}

uint32_t BK4819_GetFrequency() {
  return (BK4819_ReadRegister(BK4819_REG_39) << 16) |
         BK4819_ReadRegister(BK4819_REG_38);
}

void BK4819_SetupSquelch(uint8_t SquelchOpenRSSIThresh,
                         uint8_t SquelchCloseRSSIThresh,
                         uint8_t SquelchOpenNoiseThresh,
                         uint8_t SquelchCloseNoiseThresh,
                         uint8_t SquelchCloseGlitchThresh,
                         uint8_t SquelchOpenGlitchThresh) {
  BK4819_WriteRegister(BK4819_REG_70, 0);
  BK4819_WriteRegister(BK4819_REG_4D, 0xA000 | SquelchCloseGlitchThresh);
  BK4819_WriteRegister(BK4819_REG_4E, 0x6F00 | SquelchOpenGlitchThresh);
  BK4819_WriteRegister(BK4819_REG_4F,
                       (SquelchCloseNoiseThresh << 8) | SquelchOpenNoiseThresh);
  BK4819_WriteRegister(BK4819_REG_78,
                       (SquelchOpenRSSIThresh << 8) | SquelchCloseRSSIThresh);
  BK4819_SetAF(BK4819_AF_MUTE);
  BK4819_RX_TurnOn();
}

void BK4819_SetAF(BK4819_AF_Type_t AF) {
  // AF Output Inverse Mode = Inverse
  // Undocumented bits 0x2040
  BK4819_WriteRegister(BK4819_REG_47, 0x6040 | (AF << 8));
}

uint16_t BK4819_GetRegValue(RegisterSpec s) {
  return (BK4819_ReadRegister(s.num) >> s.offset) & s.mask;
}

void BK4819_SetRegValue(RegisterSpec s, uint16_t v) {
  uint16_t reg = BK4819_ReadRegister(s.num);
  reg &= ~(s.mask << s.offset);
  BK4819_WriteRegister(s.num, reg | (v << s.offset));
}

void BK4819_SetModulation(ModulationType type) {
  const uint8_t modTypeReg47Values[] = {1, 7, 5, 9, 4};

  BK4819_SetAF(modTypeReg47Values[type]);
  BK4819_SetRegValue(afDacGainRegSpec, 0xF);
  BK4819_WriteRegister(0x3D, type == MOD_USB ? 0 : 0x2AAB);
  BK4819_SetRegValue(afcDisableRegSpec, type != MOD_FM);
}

void BK4819_RX_TurnOn(void) {
  // DSP Voltage Setting = 1
  // ANA LDO = 2.7v
  // VCO LDO = 2.7v
  // RF LDO = 2.7v
  // PLL LDO = 2.7v
  // ANA LDO bypass
  // VCO LDO bypass
  // RF LDO bypass
  // PLL LDO bypass
  // Reserved bit is 1 instead of 0
  // Enable DSP
  // Enable XTAL
  // Enable Band Gap
  BK4819_WriteRegister(BK4819_REG_37, 0x1F0F);

  // Turn off everything
  BK4819_WriteRegister(BK4819_REG_30, 0);

  // Enable VCO Calibration
  // Enable RX Link
  // Enable AF DAC
  // Enable PLL/VCO
  // Disable PA Gain
  // Disable MIC ADC
  // Disable TX DSP
  // Enable RX DSP
  BK4819_WriteRegister(BK4819_REG_30, 0xBFF1);
}

void BK4819_SelectFilter(uint32_t Frequency) {
  if (Frequency < 28000000) {
    BK4819_ToggleGpioOut(BK4819_GPIO4_PIN32_VHF_LNA, true);
    BK4819_ToggleGpioOut(BK4819_GPIO3_PIN31_UHF_LNA, false);
  } else if (Frequency == 0xFFFFFFFF) {
    BK4819_ToggleGpioOut(BK4819_GPIO4_PIN32_VHF_LNA, false);
    BK4819_ToggleGpioOut(BK4819_GPIO3_PIN31_UHF_LNA, false);
  } else {
    BK4819_ToggleGpioOut(BK4819_GPIO4_PIN32_VHF_LNA, false);
    BK4819_ToggleGpioOut(BK4819_GPIO3_PIN31_UHF_LNA, true);
  }
}

void BK4819_DisableScramble(void) {
  uint16_t Value;

  Value = BK4819_ReadRegister(BK4819_REG_31);
  BK4819_WriteRegister(BK4819_REG_31, Value & 0xFFFD);
}

void BK4819_EnableScramble(uint8_t Type) {
  uint16_t Value;

  Value = BK4819_ReadRegister(BK4819_REG_31);
  BK4819_WriteRegister(BK4819_REG_31, Value | 2);
  BK4819_WriteRegister(BK4819_REG_71, (Type * 0x0408) + 0x68DC);
}

void BK4819_DisableVox(void) {
  uint16_t Value;

  Value = BK4819_ReadRegister(BK4819_REG_31);
  BK4819_WriteRegister(BK4819_REG_31, Value & 0xFFFB);
}


void BK4819_PlayTone(uint16_t Frequency, bool bTuningGainSwitch) {
  uint16_t ToneConfig;

  BK4819_EnterTxMute();
  BK4819_SetAF(BK4819_AF_BEEP);

  if (bTuningGainSwitch == 0) {
    ToneConfig = 0 | BK4819_REG_70_ENABLE_TONE1 |
                 (96U << BK4819_REG_70_SHIFT_TONE1_TUNING_GAIN);
  } else {
    ToneConfig = 0 | BK4819_REG_70_ENABLE_TONE1 |
                 (28U << BK4819_REG_70_SHIFT_TONE1_TUNING_GAIN);
  }
  BK4819_WriteRegister(BK4819_REG_70, ToneConfig);

  BK4819_WriteRegister(BK4819_REG_30, 0);
  BK4819_WriteRegister(BK4819_REG_30, 0 | BK4819_REG_30_ENABLE_AF_DAC |
                                          BK4819_REG_30_ENABLE_DISC_MODE |
                                          BK4819_REG_30_ENABLE_TX_DSP);

  BK4819_SetToneFrequency(Frequency);
}

/*void BK4819_PlaySingleTone(const unsigned int tone_Hz, const unsigned int delay, const unsigned int level, const bool play_speaker)
{
	BK4819_EnterTxMute();
	
	if (play_speaker)
	{
		AUDIO_AudioPathOn();
		BK4819_SetAF(BK4819_AF_BEEP);
	}
	else
		BK4819_SetAF(BK4819_AF_MUTE);
	// level 0 ~ 127
//	BK4819_WriteRegister(BK4819_REG_70, BK4819_REG_70_ENABLE_TONE1 | (96u << BK4819_REG_70_SHIFT_TONE1_TUNING_GAIN));
//	BK4819_WriteRegister(BK4819_REG_70, BK4819_REG_70_ENABLE_TONE1 | (28u << BK4819_REG_70_SHIFT_TONE1_TUNING_GAIN));
	BK4819_WriteRegister(BK4819_REG_70, BK4819_REG_70_ENABLE_TONE1 | ((level & 0x7f) << BK4819_REG_70_SHIFT_TONE1_TUNING_GAIN));
	BK4819_EnableTXLink();
	SYSTEM_DelayMs(50);
	BK4819_WriteRegister(BK4819_REG_71, scale_freq(tone_Hz));
	BK4819_ExitTxMute();
	SYSTEM_DelayMs(delay);
	BK4819_EnterTxMute();
	if (play_speaker)
	{
		AUDIO_AudioPathOff();
		BK4819_SetAF(BK4819_AF_MUTE);
	}

	BK4819_WriteRegister(BK4819_REG_70, 0x0000);
	BK4819_WriteRegister(BK4819_REG_30, 0xC1FE);
	BK4819_ExitTxMute();
}*/

void BK4819_EnterTxMute(void) { BK4819_WriteRegister(BK4819_REG_50, 0xBB20); }

void BK4819_ExitTxMute(void) { BK4819_WriteRegister(BK4819_REG_50, 0x3B20); }

void BK4819_Sleep(void) {
  BK4819_WriteRegister(BK4819_REG_30, 0);
  BK4819_WriteRegister(BK4819_REG_37, 0x1D00);
}

void BK4819_TurnsOffTones_TurnsOnRX(void) {
  BK4819_WriteRegister(BK4819_REG_70, 0);
  BK4819_SetAF(BK4819_AF_MUTE);
  BK4819_ExitTxMute();
  BK4819_WriteRegister(BK4819_REG_30, 0);
  BK4819_WriteRegister(
      BK4819_REG_30,
      0 | BK4819_REG_30_ENABLE_VCO_CALIB | BK4819_REG_30_ENABLE_RX_LINK |
          BK4819_REG_30_ENABLE_AF_DAC | BK4819_REG_30_ENABLE_DISC_MODE |
          BK4819_REG_30_ENABLE_PLL_VCO | BK4819_REG_30_ENABLE_RX_DSP);
}



void BK4819_Idle(void) { BK4819_WriteRegister(BK4819_REG_30, 0x0000); }

void BK4819_ExitBypass(void) {
  BK4819_SetAF(BK4819_AF_MUTE);
  BK4819_WriteRegister(BK4819_REG_7E, 0x302E);
}

void BK4819_PrepareTransmit(void) {
  BK4819_ExitBypass();
  BK4819_ExitTxMute();
  BK4819_TxOn_Beep();
}

void BK4819_TxOn_Beep(void) {
  BK4819_WriteRegister(BK4819_REG_37, 0x1D0F);
  BK4819_WriteRegister(BK4819_REG_52, 0x028F);
  BK4819_WriteRegister(BK4819_REG_30, 0x0000);
  BK4819_WriteRegister(BK4819_REG_30, 0xC1FE);
}

void BK4819_ExitSubAu(void) { BK4819_WriteRegister(BK4819_REG_51, 0x0000); }

void BK4819_EnableRX(void) {
  if (gRxIdleMode) {
    BK4819_ToggleGpioOut(BK4819_GPIO0_PIN28_RX_ENABLE, true);
    BK4819_RX_TurnOn();
  }
}


void BK4819_EnableTXLink(void) {
  BK4819_WriteRegister(
      BK4819_REG_30,
      0 | BK4819_REG_30_ENABLE_VCO_CALIB | BK4819_REG_30_ENABLE_UNKNOWN |
          BK4819_REG_30_DISABLE_RX_LINK | BK4819_REG_30_ENABLE_AF_DAC |
          BK4819_REG_30_ENABLE_DISC_MODE | BK4819_REG_30_ENABLE_PLL_VCO |
          BK4819_REG_30_ENABLE_PA_GAIN | BK4819_REG_30_DISABLE_MIC_ADC |
          BK4819_REG_30_ENABLE_TX_DSP | BK4819_REG_30_DISABLE_RX_DSP);
}


void BK4819_TransmitTone(bool bLocalLoopback, uint32_t Frequency) {
  BK4819_EnterTxMute();
  BK4819_WriteRegister(BK4819_REG_70,
                       BK4819_REG_70_MASK_ENABLE_TONE1 |
                           (96U << BK4819_REG_70_SHIFT_TONE1_TUNING_GAIN));
  BK4819_SetToneFrequency(Frequency);

  BK4819_SetAF(bLocalLoopback ? BK4819_AF_BEEP : BK4819_AF_MUTE);

  BK4819_EnableTXLink();
  SYSTEM_DelayMs(50);
  BK4819_ExitTxMute();
}

void BK4819_GenTail(uint8_t Tail) {
  switch (Tail) {
  case 0: // CTC134
    BK4819_WriteRegister(BK4819_REG_52, 0x828F);
    break;
  case 1: // CTC120
    BK4819_WriteRegister(BK4819_REG_52, 0xA28F);
    break;
  case 2: // CTC180
    BK4819_WriteRegister(BK4819_REG_52, 0xC28F);
    break;
  case 3: // CTC240
    BK4819_WriteRegister(BK4819_REG_52, 0xE28F);
    break;
  case 4: // CTC55
    BK4819_WriteRegister(BK4819_REG_07, 0x046f);
    break;
  }
}

void BK4819_EnableCDCSS(void) {
  BK4819_GenTail(0); // CTC134
  BK4819_WriteRegister(BK4819_REG_51, 0x804A);
}

void BK4819_EnableCTCSS(void) {
  BK4819_GenTail(4); // CTC55
  BK4819_WriteRegister(BK4819_REG_51, 0x904A);
}

uint16_t BK4819_GetRSSI(void) {
  return BK4819_ReadRegister(BK4819_REG_67) & 0x01FF;
}

bool BK4819_GetFrequencyScanResult(uint32_t *pFrequency) {
  uint16_t High = BK4819_ReadRegister(BK4819_REG_0D);
  bool Finished = (High & 0x8000) == 0;

  if (Finished) {
    uint16_t Low = BK4819_ReadRegister(BK4819_REG_0E);
    *pFrequency = (uint32_t)((High & 0x7FF) << 16) | Low;
  }

  return Finished;
}

BK4819_CssScanResult_t BK4819_GetCxCSSScanResult(uint32_t *pCdcssFreq,
                                                 uint16_t *pCtcssFreq) {
  uint16_t High, Low;

  High = BK4819_ReadRegister(BK4819_REG_69);
  if ((High & 0x8000) == 0) {
    Low = BK4819_ReadRegister(BK4819_REG_6A);
    *pCdcssFreq = ((High & 0xFFF) << 12) | (Low & 0xFFF);
    return BK4819_CSS_RESULT_CDCSS;
  }

  Low = BK4819_ReadRegister(BK4819_REG_68);
  if ((Low & 0x8000) == 0) {
    *pCtcssFreq = (Low & 0x1FFF) * 4843 / 10000;
    return BK4819_CSS_RESULT_CTCSS;
  }

  return BK4819_CSS_RESULT_NOT_FOUND;
}

void BK4819_DisableFrequencyScan(void) {
  BK4819_WriteRegister(BK4819_REG_32, 0x0244);
}

void BK4819_EnableFrequencyScan(void) {
  BK4819_WriteRegister(BK4819_REG_32, 0x0245);
}

void BK4819_SetScanFrequency(uint32_t Frequency) {
  BK4819_SetFrequency(Frequency);
  BK4819_WriteRegister(
      BK4819_REG_51,
      0 | BK4819_REG_51_DISABLE_CxCSS | BK4819_REG_51_GPIO6_PIN2_NORMAL |
          BK4819_REG_51_TX_CDCSS_POSITIVE | BK4819_REG_51_MODE_CDCSS |
          BK4819_REG_51_CDCSS_23_BIT | BK4819_REG_51_1050HZ_NO_DETECTION |
          BK4819_REG_51_AUTO_CDCSS_BW_DISABLE |
          BK4819_REG_51_AUTO_CTCSS_BW_DISABLE);
  BK4819_RX_TurnOn();
}

void BK4819_Disable(void) { BK4819_WriteRegister(BK4819_REG_30, 0); }

void BK4819_StopScan(void) {
  BK4819_DisableFrequencyScan();
  BK4819_Disable();
}


uint8_t BK4819_GetCDCSSCodeType(void) {
  return (BK4819_ReadRegister(BK4819_REG_0C) >> 14) & 3;
}

uint8_t BK4819_GetCTCType(void) {
  return (BK4819_ReadRegister(BK4819_REG_0C) >> 10) & 3;
}

void BK4819_PlayRoger(bool mt)
{
	const uint32_t tone1_Hz = mt ? 1540 : 500;
	const uint32_t tone2_Hz = mt ? 1310 : 700;
	BK4819_EnterTxMute();
	BK4819_SetAF(BK4819_AF_MUTE);
//	BK4819_WriteRegister(BK4819_REG_70, BK4819_REG_70_ENABLE_TONE1 | (96u << BK4819_REG_70_SHIFT_TONE1_TUNING_GAIN));
	BK4819_WriteRegister(BK4819_REG_70, BK4819_REG_70_ENABLE_TONE1 | (28u << BK4819_REG_70_SHIFT_TONE1_TUNING_GAIN));
	BK4819_EnableTXLink();
	SYSTEM_DelayMs(50);
	BK4819_WriteRegister(BK4819_REG_71, scale_freq(tone1_Hz));
	BK4819_ExitTxMute();
	SYSTEM_DelayMs(80);
	BK4819_EnterTxMute();
	BK4819_WriteRegister(BK4819_REG_71, scale_freq(tone2_Hz));
	BK4819_ExitTxMute();
	SYSTEM_DelayMs(80);
	BK4819_EnterTxMute();
	BK4819_WriteRegister(BK4819_REG_70, 0x0000);
	BK4819_WriteRegister(BK4819_REG_30, 0xC1FE);   // 1 1 0000 0 1 1111 1 1 1 0
}

void BK4819_PlayRogerMDC(void) {
  uint8_t i;

  BK4819_SetAF(BK4819_AF_MUTE);
  BK4819_WriteRegister(
      BK4819_REG_58,
      0x37C3); // FSK Enable, RX Bandwidth FFSK1200/1800, 0xAA or 0x55 Preamble,
               // 11 RX Gain, 101 RX Mode, FFSK1200/1800 TX
  BK4819_WriteRegister(BK4819_REG_72, 0x3065); // Set Tone2 to 1200Hz
  BK4819_WriteRegister(BK4819_REG_70,
                       0x00E0); // Enable Tone2 and Set Tone2 Gain
  BK4819_WriteRegister(BK4819_REG_5D,
                       0x0D00); // Set FSK data length to 13 bytes
  BK4819_WriteRegister(
      BK4819_REG_59,
      0x8068); // 4 byte sync length, 6 byte preamble, clear TX FIFO
  BK4819_WriteRegister(
      BK4819_REG_59,
      0x0068); // Same, but clear TX FIFO is now unset (clearing done)
  BK4819_WriteRegister(BK4819_REG_5A, 0x5555); // First two sync bytes
  BK4819_WriteRegister(BK4819_REG_5B,
                       0x55AA); // End of sync bytes. Total 4 bytes: 555555aa
  BK4819_WriteRegister(BK4819_REG_5C, 0xAA30); // Disable CRC
  for (i = 0; i < 7; i++) {
    BK4819_WriteRegister(
        BK4819_REG_5F, FSK_RogerTable[i]); // Send the data from the roger table
  }
  SYSTEM_DelayMs(20);
  BK4819_WriteRegister(BK4819_REG_59,
                       0x0868); // 4 sync bytes, 6 byte preamble, Enable FSK TX
  SYSTEM_DelayMs(180);
  // Stop FSK TX, reset Tone2, disable FSK.
  BK4819_WriteRegister(BK4819_REG_59, 0x0068);
  BK4819_WriteRegister(BK4819_REG_70, 0x0000);
  BK4819_WriteRegister(BK4819_REG_58, 0x0000);
}

void BK4819_Enable_AfDac_DiscMode_TxDsp(void) {
  BK4819_WriteRegister(BK4819_REG_30, 0x0000);
  BK4819_WriteRegister(BK4819_REG_30, 0x0302);
}

void BK4819_GetVoxAmp(uint16_t *pResult) {
  *pResult = BK4819_ReadRegister(BK4819_REG_64) & 0x7FFF;
}



void BK4819_ToggleAFBit(bool on) {
  uint16_t reg = BK4819_ReadRegister(BK4819_REG_47);
  reg &= ~(1 << 8);
  if (on)
    reg |= on << 8;
  BK4819_WriteRegister(BK4819_REG_47, reg);
}

void BK4819_ToggleAFDAC(bool on) {
  uint32_t Reg = BK4819_ReadRegister(BK4819_REG_30);
  Reg &= ~(1 << 9);
  if (on)
    Reg |= (1 << 9);
  BK4819_WriteRegister(BK4819_REG_30, Reg);
}

void BK4819_TuneTo(uint32_t f, bool precise) {
  BK4819_SelectFilter(f);
  BK4819_SetFrequency(f);
  uint16_t reg = BK4819_ReadRegister(BK4819_REG_30);
  if (precise) {
    // BK4819_WriteRegister(BK4819_REG_30, 0);
    BK4819_WriteRegister(BK4819_REG_30, 0x0200); // from radtel-rt-890-oefw
  } else {
    BK4819_WriteRegister(BK4819_REG_30, reg & ~BK4819_REG_30_ENABLE_VCO_CALIB);
  }
  BK4819_WriteRegister(BK4819_REG_30, reg);
}

void BK4819_SetToneFrequency(uint16_t f) {
  BK4819_WriteRegister(0x71, (f * 103U) / 10U);
}
