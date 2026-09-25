/*
 * Copyright 2017-2020,2025 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "app.h"
#include "fsl_debug_console.h"
#include "fsl_device_registers.h"
#include "pin_mux.h"
#include "board.h"
#include "fsl_semc.h"
#include "fsl_iomuxc.h"
#include "fsl_gpio.h"

/*******************************************************************************
 * Definitions
 ******************************************************************************/

/*******************************************************************************
 * Prototypes
 ******************************************************************************/
extern status_t BOARD_InitSEMC(void);
static void SEMC_SDRAMReadWrite32Bit(void);
static void SEMC_SDRAMReadWrite16Bit(void);
static void SEMC_SDRAMReadWrite8Bit(void);
/*******************************************************************************
 * Variables
 ******************************************************************************/

uint32_t sdram_writeBuffer[SEMC_EXAMPLE_DATALEN];
uint32_t sdram_readBuffer[SEMC_EXAMPLE_DATALEN];

/*******************************************************************************
 * Code
 ******************************************************************************/
#if defined(CACHE_MAINTAIN) && CACHE_MAINTAIN
#include "fsl_cache.h"
#endif

/*!
 * @brief Main function
 */
int main(void)
{
    /* Hardware initialize. */
    BOARD_InitHardware();
    const gpio_pin_config_t m2_reset = {kGPIO_DigitalOutput, 0, kGPIO_NoIntmode};
    GPIO_PinInit(GPIO9, 15U, &m2_reset);
    GPIO_PinInit(GPIO9, 30U, &m2_reset);
    IOMUXC_SetPinMux(IOMUXC_GPIO_AD_16_GPIO9_IO15, 0U);
    IOMUXC_SetPinMux(IOMUXC_GPIO_AD_31_GPIO9_IO30, 0U);
    IOMUXC_SetPinMux(IOMUXC_GPIO_AD_29_GPIO_MUX3_IO28, 0U);
    IOMUXC_GPR->GPR43 |= (1U << 12);

    PRINTF("MCUX SDK version: %s\r\n", MCUXSDK_VERSION_FULL_STR);

    PRINTF("\r\n SEMC SDRAM Example Start!\r\n");
    if (BOARD_InitSEMC() != kStatus_Success)
    {
        PRINTF("\r\n SEMC SDRAM Init Failed\r\n");
    }
    PRINTF("SEMC regs: MCR=%08x IOCR=%08x DCCR=%08x CR0=%08x CR1=%08x CR2=%08x CR3=%08x BR0=%08x CCR=%08x\r\n",
           (unsigned)SEMC->MCR, (unsigned)SEMC->IOCR, (unsigned)SEMC->DCCR,
           (unsigned)SEMC->SDRAMCR0, (unsigned)SEMC->SDRAMCR1,
           (unsigned)SEMC->SDRAMCR2, (unsigned)SEMC->SDRAMCR3,
           (unsigned)SEMC->BR[0], (unsigned)SCB->CCR);
    volatile uint32_t *a = (volatile uint32_t *)0x80000000U;
    volatile uint32_t *b = (volatile uint32_t *)0x80200000U;
    a[0] = 0x13579BDFU;
    b[0] = 0x2468ACE0U;
    __DSB();
    SDK_DelayAtLeastUs(1000, CLOCK_GetFreq(kCLOCK_CpuClk));
    PRINTF("Dual retention: A=%08x B=%08x\r\n", (unsigned)a[0], (unsigned)b[0]);
    volatile uint32_t *internal_lo = (volatile uint32_t *)0x20200000U;
    volatile uint32_t *internal_hi = (volatile uint32_t *)0x2037FFF0U;
    internal_lo[0] = 0xA13579BDU;
    internal_hi[0] = 0xB2468ACEU;
    __DSB();
    SDK_DelayAtLeastUs(1000, CLOCK_GetFreq(kCLOCK_CpuClk));
    PRINTF("OCRAM span: LO=%08x HI=%08x\r\n", (unsigned)internal_lo[0], (unsigned)internal_hi[0]);
    /* 32Bit data read and write. */
    SEMC_SDRAMReadWrite32Bit();
    /* 16Bit data read and write. */
    SEMC_SDRAMReadWrite16Bit();
    /* 8Bit data read and write. */
    SEMC_SDRAMReadWrite8Bit();

    volatile uint32_t *retention = (volatile uint32_t *)EXAMPLE_SEMC_START_ADDRESS;
    retention[0] = 0x13579BDFU;
    __DSB();
    SDK_DelayAtLeastUs(1000, CLOCK_GetFreq(kCLOCK_CpuClk));
    PRINTF("SDRAM retention after 1 ms: %08x\r\n", (unsigned)retention[0]);

    PRINTF("\r\n SEMC SDRAM Example End.\r\n");
    while (1)
    {
    }
}

void SEMC_SDRAMReadWrite32Bit(void)
{
    uint32_t index;
    uint32_t datalen = SEMC_EXAMPLE_DATALEN;
    uint32_t *sdram  = (uint32_t *)EXAMPLE_SEMC_START_ADDRESS; /* SDRAM start address. */
    bool result      = true;

    PRINTF("\r\n SEMC SDRAM Memory 32 bit Write Start, Start Address 0x%x, Data Length %d !\r\n", sdram, datalen);
    /* Prepare data and write to SDRAM. */
    for (index = 0; index < datalen; index++)
    {
        sdram_writeBuffer[index] = index;
        sdram[index]             = sdram_writeBuffer[index];
    }

    PRINTF("\r\n SEMC SDRAM Read 32 bit Data Start, Start Address 0x%x, Data Length %d !\r\n", sdram, datalen);

#if defined(CACHE_MAINTAIN) && CACHE_MAINTAIN
    DCACHE_InvalidateByRange(EXAMPLE_SEMC_START_ADDRESS, 4U * SEMC_EXAMPLE_DATALEN);
#endif

    /* Read data from the SDRAM. */
    for (index = 0; index < datalen; index++)
    {
        sdram_readBuffer[index] = sdram[index];
    }

    PRINTF("\r\n SEMC SDRAM 32 bit Data Write and Read Compare Start!\r\n");
    /* Compare the two buffers. */
    while (datalen--)
    {
        if (sdram_writeBuffer[datalen] != sdram_readBuffer[datalen])
        {
            result = false;
            break;
        }
    }

    if (!result)
    {
        PRINTF("\r\n SEMC SDRAM 32 bit Data Write and Read Compare Failed!\r\n");
    }
    else
    {
        PRINTF("\r\n SEMC SDRAM 32 bit Data Write and Read Compare Succeed!\r\n");
    }
}

static void SEMC_SDRAMReadWrite16Bit(void)
{
    uint32_t index;
    uint32_t datalen = SEMC_EXAMPLE_DATALEN;
    uint16_t *sdram  = (uint16_t *)EXAMPLE_SEMC_START_ADDRESS; /* SDRAM start address. */
    bool result      = true;

    PRINTF("\r\n SEMC SDRAM Memory 16 bit Write Start, Start Address 0x%x, Data Length %d !\r\n", sdram, datalen);

    memset(sdram_writeBuffer, 0, sizeof(sdram_writeBuffer));
    memset(sdram_readBuffer, 0, sizeof(sdram_readBuffer));

    /* Prepare data and write to SDRAM. */
    for (index = 0; index < datalen; index++)
    {
        sdram_writeBuffer[index] = index % 0xFFFF;
        sdram[index]             = sdram_writeBuffer[index];
    }

    PRINTF("\r\n SEMC SDRAM Read 16 bit Data Start, Start Address 0x%x, Data Length %d !\r\n", sdram, datalen);

#if defined(CACHE_MAINTAIN) && CACHE_MAINTAIN
    DCACHE_InvalidateByRange(EXAMPLE_SEMC_START_ADDRESS, 4U * SEMC_EXAMPLE_DATALEN);
#endif

    /* Read data from the SDRAM. */
    for (index = 0; index < datalen; index++)
    {
        sdram_readBuffer[index] = sdram[index];
    }

    PRINTF("\r\n SEMC SDRAM 16 bit Data Write and Read Compare Start!\r\n");
    /* Compare the two buffers. */
    while (datalen--)
    {
        if (sdram_writeBuffer[datalen] != sdram_readBuffer[datalen])
        {
            result = false;
            break;
        }
    }

    if (!result)
    {
        PRINTF("\r\n SEMC SDRAM 16 bit Data Write and Read Compare Failed!\r\n");
    }
    else
    {
        PRINTF("\r\n SEMC SDRAM 16 bit Data Write and Read Compare Succeed!\r\n");
    }
}

static void SEMC_SDRAMReadWrite8Bit(void)
{
    uint32_t index;
    uint32_t datalen = SEMC_EXAMPLE_DATALEN;
    uint8_t *sdram   = (uint8_t *)EXAMPLE_SEMC_START_ADDRESS; /* SDRAM start address. */
    bool result      = true;

    PRINTF("\r\n SEMC SDRAM Memory 8 bit Write Start, Start Address 0x%x, Data Length %d !\r\n", sdram, datalen);

    memset(sdram_writeBuffer, 0, sizeof(sdram_writeBuffer));
    memset(sdram_readBuffer, 0, sizeof(sdram_readBuffer));

    /* Prepare data and write to SDRAM. */
    for (index = 0; index < datalen; index++)
    {
        sdram_writeBuffer[index] = index % 0x100;
        sdram[index]             = sdram_writeBuffer[index];
    }

    PRINTF("\r\n SEMC SDRAM Read 8 bit Data Start, Start Address 0x%x, Data Length %d !\r\n", sdram, datalen);

#if defined(CACHE_MAINTAIN) && CACHE_MAINTAIN
    DCACHE_InvalidateByRange(EXAMPLE_SEMC_START_ADDRESS, 4U * SEMC_EXAMPLE_DATALEN);
#endif

    /* Read data from the SDRAM. */
    for (index = 0; index < datalen; index++)
    {
        sdram_readBuffer[index] = sdram[index];
    }

    PRINTF("\r\n SEMC SDRAM 8 bit Data Write and Read Compare Start!\r\n");
    /* Compare the two buffers. */
    while (datalen--)
    {
        if (sdram_writeBuffer[datalen] != sdram_readBuffer[datalen])
        {
            result = false;
            break;
        }
    }

    if (!result)
    {
        PRINTF("\r\n SEMC SDRAM 8 bit Data Write and Read Compare Failed!\r\n");
    }
    else
    {
        PRINTF("\r\n SEMC SDRAM 8 bit Data Write and Read Compare Succeed!\r\n");
    }
}
