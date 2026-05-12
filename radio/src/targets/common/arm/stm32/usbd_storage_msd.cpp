/*
 * Copyright (C) EdgeTX
 *
 * Based on code named
 *   opentx - https://github.com/opentx/opentx
 *   th9x - http://code.google.com/p/th9x
 *   er9x - http://code.google.com/p/er9x
 *   gruvin9x - http://code.google.com/p/gruvin9x
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

/* Includes ------------------------------------------------------------------*/

#include "usbd_def.h"
#include "usbd_msc.h"

#include "hal/fatfs_diskio.h"
#include "hal/storage.h"

#include "stm32_hal.h"
#include "stm32_hal_ll.h"

#include "fw_version.h"
#include "hal.h"
#include "debug.h"

#include "usb_descriptor.h"
#include "usbd_storage_msd.h"

#include <string.h>

#if FF_MAX_SS != FF_MIN_SS
#error "Variable sector size is not supported"
#endif

#define BLOCK_SIZE FF_MAX_SS

#if !defined(BOOT)
  #include "timers_driver.h"
  #define WATCHDOG_SUSPEND(x) watchdogSuspend(x)
#else
  #define WATCHDOG_SUSPEND(...)
#endif

#include "usb_conf.h"

#if defined(FIRMWARE_FORMAT_UF2) && defined(BOOT)
#define USE_UF2_DRIVE 1
#else
#define USE_UF2_DRIVE 0
#endif

#if USE_UF2_DRIVE
#include "drivers/uf2_ghostfat.h"
#endif

enum MassstorageLuns {
  STORAGE_SDCARD_LUN,

#if USE_UF2_DRIVE
  STORAGE_UF2_LUN,
#endif

  STORAGE_LUN_NBR
};

static_assert(BLOCK_SIZE == 512, "USB MSC SD-card path assumes 512-byte sectors");
static_assert(STORAGE_LUN_NBR <= MSC_BOT_MAX_LUN, "USB MSC LUN count exceeds BOT LUN storage");
static_assert(STORAGE_SDCARD_LUN == 0, "USB MSC SD-card LUN mapping changed; update parser");

namespace {

enum class UsbMscLun : uint8_t {
  SdCard = STORAGE_SDCARD_LUN,
#if USE_UF2_DRIVE
  Uf2 = STORAGE_UF2_LUN,
#endif
};

enum class UsbSdState : uint8_t {
  Closed,
  Ready,
  Failed,
};

struct UsbSdCapacity {
  uint32_t sectors = 0;
  uint16_t blockSize = BLOCK_SIZE;
  bool valid = false;
};

struct UsbBlockRange {
  uint32_t first;
  uint16_t count;
};

static bool parseUsbMscLun(uint8_t raw, UsbMscLun* out)
{
  if (raw == (uint8_t)UsbMscLun::SdCard) {
    *out = UsbMscLun::SdCard;
    return true;
  }
#if USE_UF2_DRIVE
  if (raw == (uint8_t)UsbMscLun::Uf2) {
    *out = UsbMscLun::Uf2;
    return true;
  }
#endif
  return false;
}

static bool makeBlockRange(uint32_t blk_addr, uint16_t blk_len,
                           uint32_t total_sectors, UsbBlockRange* out)
{
  if (blk_addr > total_sectors) return false;
  if ((uint32_t)blk_len > total_sectors - blk_addr) return false;
  out->first = blk_addr;
  out->count = blk_len;
  return true;
}

class UsbSdSession {
 public:
  int8_t init();
  int8_t isReady() const;
  int8_t getCapacity(uint32_t* block_num, uint16_t* block_size);
  int8_t read(uint8_t* buf, uint32_t blk_addr, uint16_t blk_len);
  int8_t write(uint8_t* buf, uint32_t blk_addr, uint16_t blk_len);
  int8_t sync();
  int8_t close();

 private:
  void reset();
  void fail();
  int8_t refreshCapacity();

  UsbSdState state_ = UsbSdState::Closed;
  const diskio_driver_t* drv_ = nullptr;
  UsbSdCapacity capacity_ = {};
};

void UsbSdSession::reset()
{
  state_ = UsbSdState::Closed;
  drv_ = nullptr;
  capacity_ = {};
}

void UsbSdSession::fail()
{
  state_ = UsbSdState::Failed;
  capacity_ = {};
}

int8_t UsbSdSession::refreshCapacity()
{
  if (state_ != UsbSdState::Ready) return USBD_FAIL;
  if (drv_ == nullptr || drv_->ioctl == nullptr) return USBD_FAIL;

  uint32_t sector_count = 0;
  if (drv_->ioctl(0, GET_SECTOR_COUNT, &sector_count) != RES_OK)
    return USBD_FAIL;
  if (sector_count == 0) return USBD_FAIL;

  uint32_t sector_size = 0;
  if (drv_->ioctl(0, GET_SECTOR_SIZE, &sector_size) != RES_OK)
    return USBD_FAIL;
  if (sector_size != BLOCK_SIZE) return USBD_FAIL;

  capacity_.sectors = sector_count;
  capacity_.blockSize = BLOCK_SIZE;
  capacity_.valid = true;
  return USBD_OK;
}

int8_t UsbSdSession::init()
{
  if (state_ == UsbSdState::Ready) {
    if (capacity_.valid) return USBD_OK;
    return refreshCapacity();
  }

  if (state_ == UsbSdState::Failed) {
    close();
  }
  // state_ is now Closed

  if (!storageIsPresent()) return USBD_FAIL;

  drv_ = storageGetDefaultDriver();
  if (drv_ == nullptr) return USBD_FAIL;
  if (drv_->initialize == nullptr) return USBD_FAIL;

  if (drv_->initialize(0) != RES_OK) {
    state_ = UsbSdState::Failed;
    return USBD_FAIL;
  }

  state_ = UsbSdState::Ready;

  if (refreshCapacity() != USBD_OK) {
    close();
    return USBD_FAIL;
  }

  return USBD_OK;
}

int8_t UsbSdSession::isReady() const
{
  if (state_ != UsbSdState::Ready) return USBD_FAIL;
  if (!capacity_.valid) return USBD_FAIL;
  if (!storageIsPresent()) return USBD_FAIL;
  return USBD_OK;
}

int8_t UsbSdSession::getCapacity(uint32_t* block_num, uint16_t* block_size)
{
  if (isReady() != USBD_OK) return USBD_FAIL;
  *block_num = capacity_.sectors;
  *block_size = capacity_.blockSize;
  return USBD_OK;
}

int8_t UsbSdSession::read(uint8_t* buf, uint32_t blk_addr, uint16_t blk_len)
{
  WATCHDOG_SUSPEND(100);

  if (isReady() != USBD_OK) return USBD_FAIL;

  if (blk_len == 0) return USBD_OK;

  UsbBlockRange range;
  if (!makeBlockRange(blk_addr, blk_len, capacity_.sectors, &range))
    return USBD_FAIL;

  if (drv_->read(0, buf, range.first, range.count) != RES_OK) {
    fail();
    return USBD_FAIL;
  }

  return USBD_OK;
}

int8_t UsbSdSession::write(uint8_t* buf, uint32_t blk_addr, uint16_t blk_len)
{
  WATCHDOG_SUSPEND(500);

  if (isReady() != USBD_OK) return USBD_FAIL;

  if (blk_len == 0) return USBD_OK;

  UsbBlockRange range;
  if (!makeBlockRange(blk_addr, blk_len, capacity_.sectors, &range))
    return USBD_FAIL;

  if (drv_->write(0, buf, range.first, range.count) != RES_OK) {
    fail();
    return USBD_FAIL;
  }

  return USBD_OK;
}

int8_t UsbSdSession::sync()
{
  if (state_ != UsbSdState::Ready) return USBD_FAIL;
  if (drv_ == nullptr || drv_->ioctl == nullptr) return USBD_FAIL;

  if (drv_->ioctl(0, CTRL_SYNC, nullptr) != RES_OK) {
    fail();
    return USBD_FAIL;
  }

  return USBD_OK;
}

int8_t UsbSdSession::close()
{
  int8_t result = USBD_OK;

  if (state_ == UsbSdState::Ready) {
    result = sync();
  }

  if (drv_ != nullptr && drv_->deinit != nullptr) {
    if (drv_->deinit(0) != RES_OK) {
      result = USBD_FAIL;
    }
  }

  reset();
  return result;
}

static UsbSdSession usbSdSession;

}  // namespace

/** USB Mass storage Standard Inquiry Data. */
const uint8_t STORAGE_Inquirydata[] = {/* 36 */
  /* LUN 0 */
  0x00,
  0x80,
  0x02,
  0x02,
  (STANDARD_INQUIRY_DATA_LEN - 5),
  0x00,
  0x00,
  0x00,
  USB_MANUFACTURER,                        /* Manufacturer : 8 bytes */
  USB_PRODUCT,                             /* Product      : 16 Bytes */
  'R', 'a', 'd', 'i', 'o', ' ', ' ', ' ',
  '1', '.', '0', '0',                      /* Version      : 4 Bytes */

#if USE_UF2_DRIVE
  /* LUN 1 */
  0x00,
  0x80,
  0x02,
  0x02,
  (STANDARD_INQUIRY_DATA_LEN - 5),
  0x00,
  0x00,
  0x00,
  USB_MANUFACTURER,                        /* Manufacturer : 8 bytes */
  USB_PRODUCT,                             /* Product      : 16 Bytes */
  'R', 'a', 'd', 'i', 'o', ' ', ' ', ' ',
  '1', '.', '0' ,'0',                      /* Version      : 4 Bytes */
#endif
};

static int8_t STORAGE_Init(uint8_t lun);
static int8_t STORAGE_GetCapacity(uint8_t lun, uint32_t *block_num, uint16_t *block_size);
static int8_t STORAGE_IsReady(uint8_t lun);
static int8_t STORAGE_IsWriteProtected(uint8_t lun);
static int8_t STORAGE_Read(uint8_t lun, uint8_t *buf, uint32_t blk_addr, uint16_t blk_len);
static int8_t STORAGE_Write(uint8_t lun, uint8_t *buf, uint32_t blk_addr, uint16_t blk_len);
static int8_t STORAGE_GetMaxLun(void);
static int8_t STORAGE_Sync(uint8_t lun);
static int8_t STORAGE_Close(uint8_t lun);

/* USER CODE BEGIN PRIVATE_FUNCTIONS_DECLARATION */

USBD_StorageTypeDef USBD_Storage_Interface_fops =
{
  STORAGE_Init,
  STORAGE_GetCapacity,
  STORAGE_IsReady,
  STORAGE_IsWriteProtected,
  STORAGE_Read,
  STORAGE_Write,
  STORAGE_Sync,
  STORAGE_Close,
  STORAGE_GetMaxLun,
  (int8_t *)STORAGE_Inquirydata
};

int8_t STORAGE_Init(uint8_t lun)
{
  UsbMscLun parsed;
  if (!parseUsbMscLun(lun, &parsed)) return USBD_FAIL;

#if USE_UF2_DRIVE
  if (parsed == UsbMscLun::Uf2) {
    uf2_fat_reset_state();
    return USBD_OK;
  }
#endif

  return usbSdSession.init();
}

/**
  * @brief  return medium capacity and block size
  * @param  lun : logical unit number
  * @param  block_num :  number of physical block
  * @param  block_size : size of a physical block
  * @retval Status
  */
int8_t STORAGE_GetCapacity(uint8_t lun, uint32_t *block_num, uint16_t *block_size)
{
  UsbMscLun parsed;
  if (!parseUsbMscLun(lun, &parsed)) return USBD_FAIL;

#if USE_UF2_DRIVE
  if (parsed == UsbMscLun::Uf2) {
    *block_size = BLOCK_SIZE;
    *block_num = UF2_NUM_BLOCKS;
    return USBD_OK;
  }
#endif

  return usbSdSession.getCapacity(block_num, block_size);
}

void usbInitLUNs()
{
  usbSdSession.close();
}

/**
  * @brief  check whether the medium is ready
  * @param  lun : logical unit number
  * @retval Status
  */
int8_t STORAGE_IsReady(uint8_t lun)
{
  UsbMscLun parsed;
  if (!parseUsbMscLun(lun, &parsed)) return USBD_FAIL;

#if USE_UF2_DRIVE
  if (parsed == UsbMscLun::Uf2) {
    return USBD_OK;
  }
#endif

  return usbSdSession.isReady();
}

/**
  * @brief  check whether the medium is write-protected
  * @param  lun : logical unit number
  * @retval Status
  */
int8_t STORAGE_IsWriteProtected(uint8_t lun)
{
  return (USBD_OK);
}

/**
  * @brief  Read data from the medium
  * @param  lun : logical unit number
  * @param  buf : Pointer to the buffer to save data
  * @param  blk_addr :  address of 1st block to be read
  * @param  blk_len : nmber of blocks to be read
  * @retval Status
  */

int8_t STORAGE_Read (uint8_t lun,
                   uint8_t *buf,
                   uint32_t blk_addr,
                   uint16_t blk_len)
{
  WATCHDOG_SUSPEND(100/*1s*/);

  UsbMscLun parsed;
  if (!parseUsbMscLun(lun, &parsed)) return USBD_FAIL;

#if USE_UF2_DRIVE
  if (parsed == UsbMscLun::Uf2) {
    uf2_fat_read_block(blk_addr, buf);
    return 0;
  }
#endif

  return usbSdSession.read(buf, blk_addr, blk_len);
}
/**
  * @brief  Write data to the medium
  * @param  lun : logical unit number
  * @param  buf : Pointer to the buffer to write from
  * @param  blk_addr :  address of 1st block to be written
  * @param  blk_len : nmber of blocks to be read
  * @retval Status
  */
int8_t STORAGE_Write(uint8_t lun, uint8_t *buf, uint32_t blk_addr, uint16_t blk_len)
{
  WATCHDOG_SUSPEND(500/*5s*/);

  UsbMscLun parsed;
  if (!parseUsbMscLun(lun, &parsed)) return USBD_FAIL;

#if USE_UF2_DRIVE
  if (parsed == UsbMscLun::Uf2) {
    int wr_ret;
    while ((blk_len--) && (wr_ret = uf2_fat_write_block(blk_addr, buf)) > 0) {
      blk_addr += 512;
      buf += 512;
    }
    return USBD_OK;
  }
#endif

  return usbSdSession.write(buf, blk_addr, blk_len);
}

/**
  * @brief  Return number of supported logical unit
  * @param  None
  * @retval number of logical unit
  */

int8_t STORAGE_Sync(uint8_t lun)
{
  UsbMscLun parsed;
  if (!parseUsbMscLun(lun, &parsed)) return USBD_FAIL;

#if USE_UF2_DRIVE
  if (parsed == UsbMscLun::Uf2) {
    return USBD_OK;
  }
#endif

  return usbSdSession.sync();
}

int8_t STORAGE_Close(uint8_t lun)
{
  UsbMscLun parsed;
  if (!parseUsbMscLun(lun, &parsed)) return USBD_FAIL;

#if USE_UF2_DRIVE
  if (parsed == UsbMscLun::Uf2) {
    return USBD_OK;
  }
#endif

  return usbSdSession.close();
}

int8_t STORAGE_GetMaxLun(void)
{
  return (STORAGE_LUN_NBR - 1);
}
