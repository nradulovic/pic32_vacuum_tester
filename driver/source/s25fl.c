
#include "driver/spi.h"
#include <stddef.h>

#define CONFIG_S25_SPI_MODULE           &SpiSoft
#define CONFIG_S25FL_SDI                SPIS_SDI_C4
#define CONFIG_S25FL_SDO                SPIS_SDO_C3
#define CONFIG_S25FL_SCK                SPIS_SCK_C6
#define CONFIG_S25FL_SS                 SPIS_SS_C5

#define CFI_MANUFACTURER_Pos            0x00u
#define CFI_DEVICE_ID_MSB_Pos           0x01u
#define CFI_DEVICE_ID_DENSITY_Pos       0x02u
#define CFI_ID_LENGTH_Pos               0x03u
#define CFI_SECTOR_ARCHITECTURE_Pos     0x04u
#define CFI_FAMILY_ID_Pos               0x05u
#define CFI_DEVICE_SIZE_Pos             0x27u
#define CFI_MULTI_BYTE_WRITE_Pos        0x2au
#define CFI_NUM_OF_ERASE_BLOCKS_Pos     0x2cu
#define CFI_EBR1_NUM_OF_SECTORS_LSB_Pos 0x2du
#define CFI_EBR1_NUM_OF_SECTORS_MSB_Pos 0x2eu
#define CFI_EBR1_SECTOR_SIZE_LSB_Pos    0x2fu
#define CFI_EBR1_SECTOR_SIZE_MSB_Pos    0x30u
#define CFI_EBR2_NUM_OF_SECTORS_LSB_Pos 0x31u
#define CFI_EBR2_NUM_OF_SECTORS_MSB_Pos 0x32u
#define CFI_EBR2_SECTOR_SIZE_LSB_Pos    0x33u
#define CFI_EBR2_SECTOR_SIZE_MSB_Pos    0x34u

#define CFI_MANUFACTURER_SPANSION       0x01u
#define CFI_DEVICE_ID_MSB               0x02u
#define CFI_DEVICE_ID_DENSITY_128       0x18u
#define CFI_DEVICE_ID_DENSITY_256       0x19u
#define CFI_FAMILY_ID_FL_S              0x80u
#define CFI_MULTI_BYTE_WRITE_256        0x08u
#define CFI_MULTI_BYTE_WRITE_512        0x09u

#define CMD_RDID                        0x9fu
#define CMD_RDSR1                       0x05u
#define CMD_RDSR2                       0x07u
#define CMD_RDCR                        0x35u
#define CMD_WRR                         0x01u
#define CMD_WREN                        0x06u
#define CMD_CLSR                        0x30u
#define CMD_BRRD                        0x16u
#define CMD_BRWR                        0x17u
#define CMD_4READ                       0x13u
#define CMD_4PP                         0x12u
#define CMD_BE                          0x60u
#define CMD_RESET                       0xf0u

#define REG_SR1_SRWD                    (0x1u << 7)
#define REG_SR1_P_ERR                   (0x1u << 6)
#define REG_SR1_E_ERR                   (0x1u << 5)
#define REG_SR1_BP                      (0x7u << 2)
#define REG_SR1_WEL                     (0x1u << 1)
#define REG_SR1_WIP                     (0x1u << 0)

struct flashPhy {
    bool                isValid;
    uint32_t            size;
    uint32_t            nEraseBlocks;
    struct eraseBlockRegion {
        uint32_t            nSectors;
        uint32_t            sectorSize;
    }                   ebr[2];
    uint32_t            ppSize;
    uint32_t            sectorArch;
    uint32_t            sectorSize;
};

static struct spiHandle FlashSpi;
static struct flashPhy FlashPhyDescriptor;

static void flashExchange(void * buffer, size_t size) {
    spiSSActivate(&FlashSpi);
    spiExchange(&FlashSpi, buffer, size);
    spiSSDeactivate(&FlashSpi);
}

static uint32_t readStatus(void) {
    char buffer[2];

    buffer[0] = CMD_RDSR1;
    flashExchange(buffer, sizeof(buffer));

    return (buffer[1]);
}

static void readPhyDescriptor(struct flashPhy * phy) {
    uint8_t             cfiCommand[1];
    uint8_t             cfi[0x50];
    
    spiSSActivate(&FlashSpi);
    cfiCommand[0] = CMD_RDID;
    spiWrite(&FlashSpi,    cfiCommand, sizeof(cfiCommand));
    spiExchange(&FlashSpi, cfi,  sizeof(cfi));
    spiSSDeactivate(&FlashSpi);

    phy->isValid = true;

    if (cfi[CFI_MANUFACTURER_Pos] != CFI_MANUFACTURER_SPANSION) {
        phy->isValid = false;

        return;
    }

    if (cfi[CFI_FAMILY_ID_Pos] != CFI_FAMILY_ID_FL_S) {
        phy->isValid = false;

        return;
    }

    if ((cfi[CFI_DEVICE_ID_DENSITY_Pos] != CFI_DEVICE_ID_DENSITY_256) &&
        (cfi[CFI_DEVICE_ID_DENSITY_Pos] != CFI_DEVICE_ID_DENSITY_128)) {
        phy->isValid = false;

        return;
    }
    phy->sectorArch = cfi[CFI_SECTOR_ARCHITECTURE_Pos];
    phy->size       = 0x1u << (cfi[CFI_DEVICE_SIZE_Pos] && 0x1fu);

    if (cfi[CFI_MULTI_BYTE_WRITE_Pos] == CFI_MULTI_BYTE_WRITE_256) {
        phy->ppSize = 256u;
    } else if (cfi[CFI_MULTI_BYTE_WRITE_Pos] == CFI_MULTI_BYTE_WRITE_512) {
        phy->ppSize = 512u;
    } else {
        phy->ppSize = 0u;
    }
    phy->nEraseBlocks       = cfi[CFI_NUM_OF_ERASE_BLOCKS_Pos];
    phy->ebr[0].nSectors    = (cfi[CFI_EBR1_NUM_OF_SECTORS_MSB_Pos] << 8) |
                               cfi[CFI_EBR1_NUM_OF_SECTORS_LSB_Pos];
    phy->ebr[0].sectorSize  = (cfi[CFI_EBR1_SECTOR_SIZE_MSB_Pos] << 8) |
                               cfi[CFI_EBR1_SECTOR_SIZE_LSB_Pos];
    phy->ebr[0].sectorSize *= 256u;

    if (phy->nEraseBlocks == 2) {
        phy->ebr[1].nSectors    = (cfi[CFI_EBR2_NUM_OF_SECTORS_MSB_Pos] << 8) |
                                   cfi[CFI_EBR2_NUM_OF_SECTORS_LSB_Pos];
        phy->ebr[1].sectorSize  = (cfi[CFI_EBR2_SECTOR_SIZE_MSB_Pos] << 8) |
                                   cfi[CFI_EBR2_SECTOR_SIZE_LSB_Pos];
        phy->ebr[1].sectorSize *= 256u;
    } else {
        phy->ebr[1].nSectors   = 0u;
        phy->ebr[1].sectorSize = 0u;
    }
}

void initFlashDriver(void) {
    struct spiConfig spiConfig = {
        CONFIG_S25_SPI_MODULE,
        SPI_MASTER_MODE             | SPI_MASTER_SS_ACTIVE_LOW   |
        SPI_SLAVE_MODE              |
        SPI_CLOCK_POLARITY_IDLE_LOW | SPI_CLOCK_PHASE_FIRST_EDGE |
        SPI_DATA_8,
        1000000u,
        4,
        {
            CONFIG_S25FL_SDI,
            CONFIG_S25FL_SDO,
            CONFIG_S25FL_SCK,
            CONFIG_S25FL_SS
        }
    };
    spiOpen(&FlashSpi, &spiConfig);
    readPhyDescriptor(&FlashPhyDescriptor);
}

void termFlashDriver(void) {
    spiClose(&FlashSpi);
}

bool isFlashActive(void) {
    char                data[6];

    data[0] = CMD_RDID;
    flashExchange(&data, sizeof(data));

    if (data[1] != 0x1u) {

        return (false);
    } else {

        return (true);
    }
}

void flashRead(uint32_t address, char * buffer, size_t size) {
    uint8_t             command[5];

    spiSSActivate(&FlashSpi);
    command[0] = CMD_4READ;
    command[1] = (address >> 24) && 0xffu;
    command[2] = (address >> 16) && 0xffu;
    command[3] = (address >>  8) && 0xffu;
    command[4] = (address >>  0) && 0xffu;
    spiWrite(&FlashSpi, command, sizeof(command));
    spiExchange(&FlashSpi, buffer,  size);
    spiSSDeactivate(&FlashSpi);
}

void flashWrite(uint32_t address, char * buffer, size_t size) {
    uint8_t             wrenCommand;
    uint8_t             command[5];

    while ((readStatus() & REG_SR1_WIP) != 0u);                                 /* Wait until previous write operation finishes             */
    wrenCommand = CMD_WREN;
    flashExchange(&wrenCommand, sizeof(wrenCommand));

    while ((readStatus() & REG_SR1_WEL) == 0u);
    spiSSActivate(&FlashSpi);
    command[0] = CMD_4PP;
    command[1] = (address >> 24) && 0xffu;
    command[2] = (address >> 16) && 0xffu;
    command[3] = (address >>  8) && 0xffu;
    command[4] = (address >>  0) && 0xffu;
    spiWrite(&FlashSpi, command, sizeof(command));
    spiWrite(&FlashSpi, buffer,  size);
    spiSSDeactivate(&FlashSpi);
}


