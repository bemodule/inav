/*
 * This file is part of Cleanflight.
 *
 * Cleanflight is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Cleanflight is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Cleanflight.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "platform.h"

#ifdef USE_ADC

#include "build/build_config.h"

#include "system.h"
#include "sensor.h"
#include "accgyro.h"
#include "adc.h"
#include "adc_impl.h"
#include "io.h"
#include "rcc.h"

#ifndef ADC_INSTANCE
#define ADC_INSTANCE   ADC1
#endif

const adcDevice_t adcHardware[] = {
    { .ADCx = ADC1, .rccADC = RCC_APB2(ADC1), .rccDMA = RCC_AHB(DMA1), .DMAy_Channelx = DMA1_Channel1 }
};

ADCDevice adcDeviceByInstance(ADC_TypeDef *instance)
{
    if (instance == ADC1)
        return ADCDEV_1;

/* TODO -- ADC2 available on large 10x devices.
    if (instance == ADC2)
        return ADCDEV_2;
*/
    return ADCINVALID;
}

const adcTagMap_t adcTagMap[] = {
    { DEFIO_TAG_E__PA0, ADC_Channel_0 }, // ADC12
    { DEFIO_TAG_E__PA1, ADC_Channel_1 }, // ADC12
    { DEFIO_TAG_E__PA2, ADC_Channel_2 }, // ADC12
    { DEFIO_TAG_E__PA3, ADC_Channel_3 }, // ADC12
    { DEFIO_TAG_E__PA4, ADC_Channel_4 }, // ADC12
    { DEFIO_TAG_E__PA5, ADC_Channel_5 }, // ADC12
    { DEFIO_TAG_E__PA6, ADC_Channel_6 }, // ADC12
    { DEFIO_TAG_E__PA7, ADC_Channel_7 }, // ADC12
    { DEFIO_TAG_E__PB0, ADC_Channel_8 }, // ADC12
    { DEFIO_TAG_E__PB1, ADC_Channel_9 }, // ADC12
};

// Driver for STM32F103CB onboard ADC
//
// Naze32
// Battery Voltage (VBAT) is connected to PA4 (ADC1_IN4) with 10k:1k divider
// RSSI ADC uses CH2 (PA1, ADC1_IN1)
// Current ADC uses CH8 (PB1, ADC1_IN9)
//
// NAZE rev.5 hardware has PA5 (ADC1_IN5) on breakout pad on bottom of board
//

void adcInit(drv_adc_config_t *init)
{

#if !defined(VBAT_ADC_PIN) && !defined(EXTERNAL1_ADC_PIN) && !defined(RSSI_ADC_PIN) && !defined(CURRENT_METER_ADC_PIN)
    UNUSED(init);
#endif

    uint8_t configuredAdcChannels = 0;

    memset(&adcConfig, 0, sizeof(adcConfig));

#ifdef VBAT_ADC_PIN
    if (init->enableVBat) {
        adcConfig[ADC_BATTERY].tag = IO_TAG(VBAT_ADC_PIN);
    }
#endif

#ifdef RSSI_ADC_PIN
    if (init->enableRSSI) {
        adcConfig[ADC_RSSI].tag = IO_TAG(RSSI_ADC_PIN);
    }
#endif

#ifdef EXTERNAL1_ADC_PIN
    if (init->enableExternal1) {
        adcConfig[ADC_EXTERNAL1].tag = IO_TAG(EXTERNAL1_ADC_PIN);
    }
#endif

#ifdef CURRENT_METER_ADC_PIN
    if (init->enableCurrentMeter) {
        adcConfig[ADC_CURRENT].tag = IO_TAG(CURRENT_METER_ADC_PIN);
    }
#endif

    ADCDevice device = adcDeviceByInstance(ADC_INSTANCE);
    if (device == ADCINVALID)
        return;

    const adcDevice_t adc = adcHardware[device];

    for (int  i = 0; i < ADC_CHANNEL_COUNT; i++) {
        if (!adcConfig[i].tag)
            continue;

        IOInit(IOGetByTag(adcConfig[i].tag), OWNER_ADC, RESOURCE_ADC_BATTERY+i, 0);
        IOConfigGPIO(IOGetByTag(adcConfig[i].tag), IO_CONFIG(GPIO_Mode_AIN, 0));
        adcConfig[i].adcChannel = adcChannelByTag(adcConfig[i].tag);
        adcConfig[i].dmaIndex = configuredAdcChannels++;
        adcConfig[i].sampleTime = ADC_SampleTime_239Cycles5;
        adcConfig[i].enabled = true;
    }

    RCC_ADCCLKConfig(RCC_PCLK2_Div8);  // 9MHz from 72MHz APB2 clock(HSE), 8MHz from 64MHz (HSI)
    RCC_ClockCmd(adc.rccADC, ENABLE);
    RCC_ClockCmd(adc.rccDMA, ENABLE);

    DMA_DeInit(adc.DMAy_Channelx);
    DMA_InitTypeDef DMA_InitStructure;
    DMA_StructInit(&DMA_InitStructure);
    DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&adc.ADCx->DR;
    DMA_InitStructure.DMA_MemoryBaseAddr = (uint32_t)adcValues;
    DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralSRC;
    DMA_InitStructure.DMA_BufferSize = configuredAdcChannels;
    DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    DMA_InitStructure.DMA_MemoryInc = configuredAdcChannels > 1 ? DMA_MemoryInc_Enable : DMA_MemoryInc_Disable;
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
    DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_HalfWord;
    DMA_InitStructure.DMA_Mode = DMA_Mode_Circular;
    DMA_InitStructure.DMA_Priority = DMA_Priority_High;
    DMA_InitStructure.DMA_M2M = DMA_M2M_Disable;
    DMA_Init(adc.DMAy_Channelx, &DMA_InitStructure);
    DMA_Cmd(adc.DMAy_Channelx, ENABLE);

    ADC_InitTypeDef ADC_InitStructure;
    ADC_StructInit(&ADC_InitStructure);
    ADC_InitStructure.ADC_Mode = ADC_Mode_Independent;
    ADC_InitStructure.ADC_ScanConvMode = configuredAdcChannels > 1 ? ENABLE : DISABLE;
    ADC_InitStructure.ADC_ContinuousConvMode = ENABLE;
    ADC_InitStructure.ADC_ExternalTrigConv = ADC_ExternalTrigConv_None;
    ADC_InitStructure.ADC_DataAlign = ADC_DataAlign_Right;
    ADC_InitStructure.ADC_NbrOfChannel = configuredAdcChannels;
    ADC_Init(adc.ADCx, &ADC_InitStructure);

    uint8_t rank = 1;
    for (int i = 0; i < ADC_CHANNEL_COUNT; i++) {
        if (!adcConfig[i].enabled) {
            continue;
        }
        ADC_RegularChannelConfig(adc.ADCx, adcConfig[i].adcChannel, rank++, adcConfig[i].sampleTime);
    }

    ADC_DMACmd(adc.ADCx, ENABLE);
    ADC_Cmd(adc.ADCx, ENABLE);

    ADC_ResetCalibration(adc.ADCx);
    while (ADC_GetResetCalibrationStatus(adc.ADCx));
    ADC_StartCalibration(adc.ADCx);
    while (ADC_GetCalibrationStatus(adc.ADCx));

    ADC_SoftwareStartConvCmd(adc.ADCx, ENABLE);
}
#endif
