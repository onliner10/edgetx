/**
  ******************************************************************************
  * @file    usbd_msc_scsi.c
  * @author  MCD Application Team
  * @brief   This file provides all the USBD SCSI layer functions.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2015 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */

/* BSPDependencies
- "stm32xxxxx_{eval}{discovery}{nucleo_144}.c"
- "stm32xxxxx_{eval}{discovery}_io.c"
- "stm32xxxxx_{eval}{discovery}{adafruit}_sd.c"
EndBSPDependencies */

/* Includes ------------------------------------------------------------------*/
#include "usbd_msc_bot.h"
#include "usbd_msc_scsi.h"
#include "usbd_msc.h"
#include "usbd_msc_data.h"

#if READ_CAPACITY16_DATA_LEN > MSC_MEDIA_PACKET
#error "READ CAPACITY(16) response must fit in MSC BOT buffer"
#endif

/** @addtogroup STM32_USB_DEVICE_LIBRARY
  * @{
  */


/** @defgroup MSC_SCSI
  * @brief Mass storage SCSI layer module
  * @{
  */

/** @defgroup MSC_SCSI_Private_TypesDefinitions
  * @{
  */
/**
  * @}
  */


/** @defgroup MSC_SCSI_Private_Defines
  * @{
  */

/**
  * @}
  */


/** @defgroup MSC_SCSI_Private_Macros
  * @{
  */
/**
  * @}
  */


/** @defgroup MSC_SCSI_Private_Variables
  * @{
  */
extern uint8_t MSCInEpAdd;
extern uint8_t MSCOutEpAdd;
/**
  * @}
  */


/** @defgroup MSC_SCSI_Private_FunctionPrototypes
  * @{
  */
static int8_t SCSI_TestUnitReady(USBD_HandleTypeDef *pdev, uint8_t lun, uint8_t *params);
static int8_t SCSI_Inquiry(USBD_HandleTypeDef *pdev, uint8_t lun, uint8_t *params);
static int8_t SCSI_ReadFormatCapacity(USBD_HandleTypeDef *pdev, uint8_t lun, uint8_t *params);
static int8_t SCSI_ReadCapacity10(USBD_HandleTypeDef *pdev, uint8_t lun, uint8_t *params);
static int8_t SCSI_ReadCapacity16(USBD_HandleTypeDef *pdev, uint8_t lun, uint8_t *params);
static int8_t SCSI_RequestSense(USBD_HandleTypeDef *pdev, uint8_t lun, uint8_t *params);
static int8_t SCSI_StartStopUnit(USBD_HandleTypeDef *pdev, uint8_t lun, uint8_t *params);
static int8_t SCSI_AllowPreventRemovable(USBD_HandleTypeDef *pdev, uint8_t lun, uint8_t *params);
static int8_t SCSI_ModeSense6(USBD_HandleTypeDef *pdev, uint8_t lun, uint8_t *params);
static int8_t SCSI_ModeSense10(USBD_HandleTypeDef *pdev, uint8_t lun, uint8_t *params);
static int8_t SCSI_Write10(USBD_HandleTypeDef *pdev, uint8_t lun, uint8_t *params);
static int8_t SCSI_Write12(USBD_HandleTypeDef *pdev, uint8_t lun, uint8_t *params);
static int8_t SCSI_Read10(USBD_HandleTypeDef *pdev, uint8_t lun, uint8_t *params);
static int8_t SCSI_Read12(USBD_HandleTypeDef *pdev, uint8_t lun, uint8_t *params);
static int8_t SCSI_Verify10(USBD_HandleTypeDef *pdev, uint8_t lun, uint8_t *params);
static int8_t SCSI_ReportLuns(USBD_HandleTypeDef *pdev, uint8_t lun, uint8_t *params);
static int8_t SCSI_ReceiveDiagnosticResults(USBD_HandleTypeDef *pdev, uint8_t lun, uint8_t *params);
static int8_t SCSI_SynchronizeCache10(USBD_HandleTypeDef *pdev, uint8_t lun, uint8_t *params);
static int8_t SCSI_CheckAddressRange(USBD_HandleTypeDef *pdev, uint8_t lun,
                                     uint32_t blk_offset, uint32_t blk_nbr);

static int8_t SCSI_ProcessRead(USBD_HandleTypeDef *pdev, uint8_t lun);
static int8_t SCSI_ProcessWrite(USBD_HandleTypeDef *pdev, uint8_t lun);

static int8_t SCSI_UpdateBotData(USBD_MSC_BOT_HandleTypeDef *hmsc,
                                 const uint8_t *pBuff, uint32_t length);
static USBD_MSC_BOT_HandleTypeDef *SCSI_GetBot(USBD_HandleTypeDef *pdev);
static int8_t SCSI_GetLunBlock(USBD_HandleTypeDef *pdev, uint8_t lun,
                               USBD_MSC_BOT_HandleTypeDef **out_hmsc,
                               USBD_MSC_BOT_LUN_TypeDef **out_blk);
static int8_t SCSI_BlocksToBytes(uint32_t blocks, uint16_t block_size, uint32_t *out);
/**
  * @}
  */


/** @defgroup MSC_SCSI_Private_Functions
  * @{
  */


static USBD_MSC_BOT_HandleTypeDef *SCSI_GetBot(USBD_HandleTypeDef *pdev)
{
  if (pdev == NULL) return NULL;
  USBD_MSC_BOT_HandleTypeDef *hmsc = (USBD_MSC_BOT_HandleTypeDef *)pdev->pClassDataCmsit[pdev->classId];
  return hmsc;
}

static int8_t SCSI_GetLunBlock(USBD_HandleTypeDef *pdev, uint8_t lun,
                               USBD_MSC_BOT_HandleTypeDef **out_hmsc,
                               USBD_MSC_BOT_LUN_TypeDef **out_blk)
{
  if (pdev == NULL || out_hmsc == NULL || out_blk == NULL) return -1;
  USBD_MSC_BOT_HandleTypeDef *hmsc = SCSI_GetBot(pdev);
  if (hmsc == NULL)
  {
    return -1;
  }
  if (lun > hmsc->max_lun || lun >= MSC_BOT_MAX_LUN)
  {
    SCSI_SenseCode(pdev, lun, ILLEGAL_REQUEST, INVALID_FIELD_IN_COMMAND);
    return -1;
  }
  *out_hmsc = hmsc;
  *out_blk = &hmsc->scsi_blk[lun];
  return 0;
}

static int8_t SCSI_BlocksToBytes(uint32_t blocks, uint16_t block_size, uint32_t *out)
{
  if (out == NULL) return -1;
  if (block_size == 0U) return -1;
  if (blocks > UINT32_MAX / block_size) return -1;
  *out = blocks * block_size;
  return 0;
}

/**
  * @brief  SCSI_ProcessCmd
  *         Process SCSI commands
  * @param  pdev: device instance
  * @param  lun: Logical unit number
  * @param  params: Command parameters
  * @retval status
  */
int8_t SCSI_ProcessCmd(USBD_HandleTypeDef *pdev, uint8_t lun, uint8_t *cmd)
{
  int8_t ret;
  USBD_MSC_BOT_HandleTypeDef *hmsc = (USBD_MSC_BOT_HandleTypeDef *)pdev->pClassDataCmsit[pdev->classId];

  if (hmsc == NULL)
  {
    return -1;
  }

  switch (cmd[0])
  {
    case SCSI_TEST_UNIT_READY:
      ret = SCSI_TestUnitReady(pdev, lun, cmd);
      break;

    case SCSI_REQUEST_SENSE:
      ret = SCSI_RequestSense(pdev, lun, cmd);
      break;

    case SCSI_INQUIRY:
      ret = SCSI_Inquiry(pdev, lun, cmd);
      break;

    case SCSI_START_STOP_UNIT:
      ret = SCSI_StartStopUnit(pdev, lun, cmd);
      break;

    case SCSI_ALLOW_MEDIUM_REMOVAL:
      ret = SCSI_AllowPreventRemovable(pdev, lun, cmd);
      break;

    case SCSI_MODE_SENSE6:
      ret = SCSI_ModeSense6(pdev, lun, cmd);
      break;

    case SCSI_MODE_SENSE10:
      ret = SCSI_ModeSense10(pdev, lun, cmd);
      break;

    case SCSI_READ_FORMAT_CAPACITIES:
      ret = SCSI_ReadFormatCapacity(pdev, lun, cmd);
      break;

    case SCSI_READ_CAPACITY10:
      ret = SCSI_ReadCapacity10(pdev, lun, cmd);
      break;

    case SCSI_READ_CAPACITY16:
      ret = SCSI_ReadCapacity16(pdev, lun, cmd);
      break;

    case SCSI_READ10:
      ret = SCSI_Read10(pdev, lun, cmd);
      break;

    case SCSI_READ12:
      ret = SCSI_Read12(pdev, lun, cmd);
      break;

    case SCSI_WRITE10:
      ret = SCSI_Write10(pdev, lun, cmd);
      break;

    case SCSI_WRITE12:
      ret = SCSI_Write12(pdev, lun, cmd);
      break;

    case SCSI_VERIFY10:
      ret = SCSI_Verify10(pdev, lun, cmd);
      break;

    case SCSI_REPORT_LUNS:
      ret = SCSI_ReportLuns(pdev, lun, cmd);
      break;

    case SCSI_RECEIVE_DIAGNOSTIC_RESULTS:
      ret = SCSI_ReceiveDiagnosticResults(pdev, lun, cmd);
      break;

    case SCSI_SYNCHRONIZE_CACHE10:
      ret = SCSI_SynchronizeCache10(pdev, lun, cmd);
      break;

    default:
      SCSI_SenseCode(pdev, lun, ILLEGAL_REQUEST, INVALID_CDB);
      ret = -1;
      break;
  }

  return ret;
}


/**
  * @brief  SCSI_TestUnitReady
  *         Process SCSI Test Unit Ready Command
  * @param  lun: Logical unit number
  * @param  params: Command parameters
  * @retval status
  */
static int8_t SCSI_TestUnitReady(USBD_HandleTypeDef *pdev, uint8_t lun, uint8_t *params)
{
  UNUSED(params);
  USBD_MSC_BOT_HandleTypeDef *hmsc = (USBD_MSC_BOT_HandleTypeDef *)pdev->pClassDataCmsit[pdev->classId];

  if (hmsc == NULL)
  {
    return -1;
  }

  /* case 9 : Hi > D0 */
  if (hmsc->cbw.dDataLength != 0U)
  {
    SCSI_SenseCode(pdev, hmsc->cbw.bLUN, ILLEGAL_REQUEST, INVALID_CDB);

    return -1;
  }

  if (hmsc->scsi_medium_state == SCSI_MEDIUM_EJECTED)
  {
    SCSI_SenseCode(pdev, lun, NOT_READY, MEDIUM_NOT_PRESENT);
    hmsc->bot_state = USBD_BOT_NO_DATA;
    return -1;
  }

  if (((USBD_StorageTypeDef *)pdev->pUserData[pdev->classId])->IsReady(lun) != 0)
  {
    SCSI_SenseCode(pdev, lun, NOT_READY, MEDIUM_NOT_PRESENT);
    hmsc->bot_state = USBD_BOT_NO_DATA;

    return -1;
  }
  hmsc->bot_data_length = 0U;

  return 0;
}


/**
  * @brief  SCSI_Inquiry
  *         Process Inquiry command
  * @param  lun: Logical unit number
  * @param  params: Command parameters
  * @retval status
  */
static int8_t SCSI_Inquiry(USBD_HandleTypeDef *pdev, uint8_t lun, uint8_t *params)
{
  uint8_t *pPage;
  uint16_t len;
  USBD_MSC_BOT_HandleTypeDef *hmsc = (USBD_MSC_BOT_HandleTypeDef *)pdev->pClassDataCmsit[pdev->classId];

  if (hmsc == NULL)
  {
    return -1;
  }

  if (hmsc->cbw.dDataLength == 0U)
  {
    SCSI_SenseCode(pdev, hmsc->cbw.bLUN, ILLEGAL_REQUEST, INVALID_CDB);
    return -1;
  }

  if ((params[1] & 0x01U) != 0U) /* Evpd is set */
  {
    if (params[2] == 0U) /* Request for Supported Vital Product Data Pages*/
    {
      (void)SCSI_UpdateBotData(hmsc, MSC_Page00_Inquiry_Data, LENGTH_INQUIRY_PAGE00);
    }
    else if (params[2] == 0x80U) /* Request for VPD page 0x80 Unit Serial Number */
    {
      (void)SCSI_UpdateBotData(hmsc, MSC_Page80_Inquiry_Data, LENGTH_INQUIRY_PAGE80);
    }
    else /* Request Not supported */
    {
      SCSI_SenseCode(pdev, hmsc->cbw.bLUN, ILLEGAL_REQUEST,
                     INVALID_FIELD_IN_COMMAND);

      return -1;
    }
  }
  else
  {

    pPage = (uint8_t *) & ((USBD_StorageTypeDef *)pdev->pUserData[pdev->classId]) \
            ->pInquiry[lun * STANDARD_INQUIRY_DATA_LEN];
    len = (uint16_t)pPage[4] + 5U;

    if (params[4] <= len)
    {
      len = params[4];
    }

    (void)SCSI_UpdateBotData(hmsc, pPage, len);
  }

  return 0;
}


/**
  * @brief  SCSI_ReadCapacity10
  *         Process Read Capacity 10 command
  * @param  lun: Logical unit number
  * @param  params: Command parameters
  * @retval status
  */
static int8_t SCSI_ReadCapacity10(USBD_HandleTypeDef *pdev, uint8_t lun, uint8_t *params)
{
  UNUSED(params);
  int8_t ret;
  USBD_MSC_BOT_HandleTypeDef *hmsc;
  USBD_MSC_BOT_LUN_TypeDef *p_scsi_blk;

  if (SCSI_GetLunBlock(pdev, lun, &hmsc, &p_scsi_blk) < 0) return -1;

  ret = ((USBD_StorageTypeDef *)pdev->pUserData[pdev->classId])->GetCapacity(lun, &p_scsi_blk->nbr,
                                                                             &p_scsi_blk->size);

  if ((ret != 0) || (hmsc->scsi_medium_state == SCSI_MEDIUM_EJECTED))
  {
    SCSI_SenseCode(pdev, lun, NOT_READY, MEDIUM_NOT_PRESENT);
    return -1;
  }

  uint8_t response[8];
  response[0] = (uint8_t)((p_scsi_blk->nbr - 1U) >> 24);
  response[1] = (uint8_t)((p_scsi_blk->nbr - 1U) >> 16);
  response[2] = (uint8_t)((p_scsi_blk->nbr - 1U) >>  8);
  response[3] = (uint8_t)(p_scsi_blk->nbr - 1U);
  response[4] = (uint8_t)(p_scsi_blk->size >> 24);
  response[5] = (uint8_t)(p_scsi_blk->size >> 16);
  response[6] = (uint8_t)(p_scsi_blk->size >> 8);
  response[7] = (uint8_t)(p_scsi_blk->size);

  (void)SCSI_UpdateBotData(hmsc, response, 8U);

  return 0;
}


/**
  * @brief  SCSI_ReadCapacity16
  *         Process Read Capacity 16 command
  * @param  lun: Logical unit number
  * @param  params: Command parameters
  * @retval status
  */
static int8_t SCSI_ReadCapacity16(USBD_HandleTypeDef *pdev, uint8_t lun, uint8_t *params)
{
  int8_t ret;
  USBD_MSC_BOT_HandleTypeDef *hmsc;
  USBD_MSC_BOT_LUN_TypeDef *p_scsi_blk;

  if (SCSI_GetLunBlock(pdev, lun, &hmsc, &p_scsi_blk) < 0) return -1;

  /* Validate service action */
  if ((params[1] & 0x1FU) != 0x10U)
  {
    SCSI_SenseCode(pdev, lun, ILLEGAL_REQUEST, INVALID_FIELD_IN_COMMAND);
    return -1;
  }

  ret = ((USBD_StorageTypeDef *)pdev->pUserData[pdev->classId])->GetCapacity(lun, &p_scsi_blk->nbr,
                                                                             &p_scsi_blk->size);

  if ((ret != 0) || (hmsc->scsi_medium_state == SCSI_MEDIUM_EJECTED))
  {
    SCSI_SenseCode(pdev, lun, NOT_READY, MEDIUM_NOT_PRESENT);
    return -1;
  }

  uint8_t response[READ_CAPACITY16_DATA_LEN] = {0};

  response[4] = (uint8_t)((p_scsi_blk->nbr - 1U) >> 24);
  response[5] = (uint8_t)((p_scsi_blk->nbr - 1U) >> 16);
  response[6] = (uint8_t)((p_scsi_blk->nbr - 1U) >>  8);
  response[7] = (uint8_t)(p_scsi_blk->nbr - 1U);

  response[8] = (uint8_t)(p_scsi_blk->size >>  24);
  response[9] = (uint8_t)(p_scsi_blk->size >>  16);
  response[10] = (uint8_t)(p_scsi_blk->size >>  8);
  response[11] = (uint8_t)(p_scsi_blk->size);

  uint32_t allocation_length = ((uint32_t)params[10] << 24) |
                                ((uint32_t)params[11] << 16) |
                                ((uint32_t)params[12] <<  8) |
                                (uint32_t)params[13];

  SCSI_UpdateBotData(hmsc, response,
                     MIN(allocation_length, READ_CAPACITY16_DATA_LEN));

  return 0;
}


/**
  * @brief  SCSI_ReadFormatCapacity
  *         Process Read Format Capacity command
  * @param  lun: Logical unit number
  * @param  params: Command parameters
  * @retval status
  */
static int8_t SCSI_ReadFormatCapacity(USBD_HandleTypeDef *pdev, uint8_t lun, uint8_t *params)
{
  UNUSED(params);
  uint16_t blk_size;
  uint32_t blk_nbr;
  int8_t ret;
  USBD_MSC_BOT_HandleTypeDef *hmsc;
  USBD_MSC_BOT_LUN_TypeDef *p_scsi_blk;

  if (SCSI_GetLunBlock(pdev, lun, &hmsc, &p_scsi_blk) < 0) return -1;

  ret = ((USBD_StorageTypeDef *)pdev->pUserData[pdev->classId])->GetCapacity(lun, &blk_nbr, &blk_size);

  if ((ret != 0) || (hmsc->scsi_medium_state == SCSI_MEDIUM_EJECTED))
  {
    SCSI_SenseCode(pdev, lun, NOT_READY, MEDIUM_NOT_PRESENT);
    return -1;
  }

  uint8_t response[12];
  (void)USBD_memset(response, 0, sizeof(response));
  response[3] = 0x08U;
  response[4] = (uint8_t)((blk_nbr - 1U) >> 24);
  response[5] = (uint8_t)((blk_nbr - 1U) >> 16);
  response[6] = (uint8_t)((blk_nbr - 1U) >>  8);
  response[7] = (uint8_t)(blk_nbr - 1U);
  response[8] = 0x02U;
  response[9] = (uint8_t)(blk_size >>  16);
  response[10] = (uint8_t)(blk_size >>  8);
  response[11] = (uint8_t)(blk_size);

  (void)SCSI_UpdateBotData(hmsc, response, 12U);

  return 0;
}


/**
  * @brief  SCSI_ModeSense6
  *         Process Mode Sense6 command
  * @param  lun: Logical unit number
  * @param  params: Command parameters
  * @retval status
  */
static int8_t SCSI_ModeSense6(USBD_HandleTypeDef *pdev, uint8_t lun, uint8_t *params)
{
  UNUSED(lun);
  USBD_MSC_BOT_HandleTypeDef *hmsc = (USBD_MSC_BOT_HandleTypeDef *)pdev->pClassDataCmsit[pdev->classId];
  uint16_t len = MODE_SENSE6_LEN;

  if (hmsc == NULL)
  {
    return -1;
  }

  /* Check If media is write-protected */
  if (((USBD_StorageTypeDef *)pdev->pUserData[pdev->classId])->IsWriteProtected(lun) != 0)
  {
    MSC_Mode_Sense6_data[2] |= (0x1U << 7); /* Set the WP (write protection) bit */
  }
  else
  {
    MSC_Mode_Sense10_data[2] &= ~(0x1U << 7); /* Clear the WP (write protection) bit */
  }

  if (params[4] <= len)
  {
    len = params[4];
  }

  (void)SCSI_UpdateBotData(hmsc, MSC_Mode_Sense6_data, len);

  return 0;
}


/**
  * @brief  SCSI_ModeSense10
  *         Process Mode Sense10 command
  * @param  lun: Logical unit number
  * @param  params: Command parameters
  * @retval status
  */
static int8_t SCSI_ModeSense10(USBD_HandleTypeDef *pdev, uint8_t lun, uint8_t *params)
{
  UNUSED(lun);
  USBD_MSC_BOT_HandleTypeDef *hmsc = (USBD_MSC_BOT_HandleTypeDef *)pdev->pClassDataCmsit[pdev->classId];
  uint16_t len = MODE_SENSE10_LEN;

  if (hmsc == NULL)
  {
    return -1;
  }

  /* Check If media is write-protected */
  if (((USBD_StorageTypeDef *)pdev->pUserData[pdev->classId])->IsWriteProtected(lun) != 0)
  {
    MSC_Mode_Sense10_data[3] |= (0x1U << 7); /* Set the WP (write protection) bit */
  }
  else
  {
    MSC_Mode_Sense10_data[3] &= ~(0x1U << 7); /* Clear the WP (write protection) bit */
  }

  if (params[8] <= len)
  {
    len = params[8];
  }

  (void)SCSI_UpdateBotData(hmsc, MSC_Mode_Sense10_data, len);

  return 0;
}


/**
  * @brief  SCSI_RequestSense
  *         Process Request Sense command
  * @param  lun: Logical unit number
  * @param  params: Command parameters
  * @retval status
  */
static int8_t SCSI_RequestSense(USBD_HandleTypeDef *pdev, uint8_t lun, uint8_t *params)
{
  UNUSED(lun);
  USBD_MSC_BOT_HandleTypeDef *hmsc = (USBD_MSC_BOT_HandleTypeDef *)pdev->pClassDataCmsit[pdev->classId];

  if (hmsc == NULL)
  {
    return -1;
  }

  if (hmsc->cbw.dDataLength == 0U)
  {
    SCSI_SenseCode(pdev, hmsc->cbw.bLUN, ILLEGAL_REQUEST, INVALID_CDB);
    return -1;
  }

  uint8_t response[REQUEST_SENSE_DATA_LEN];
  (void)USBD_memset(response, 0, sizeof(response));

  response[0] = 0x70U;
  response[7] = REQUEST_SENSE_DATA_LEN - 6U;

  if ((hmsc->scsi_sense_head != hmsc->scsi_sense_tail))
  {
    response[2] = (uint8_t)hmsc->scsi_sense[hmsc->scsi_sense_head].Skey;
    response[12] = (uint8_t)hmsc->scsi_sense[hmsc->scsi_sense_head].w.b.ASC;
    response[13] = (uint8_t)hmsc->scsi_sense[hmsc->scsi_sense_head].w.b.ASCQ;
    hmsc->scsi_sense_head++;

    if (hmsc->scsi_sense_head == SENSE_LIST_DEEPTH)
    {
      hmsc->scsi_sense_head = 0U;
    }
  }

  uint32_t sendLen = REQUEST_SENSE_DATA_LEN;
  if (params[4] <= REQUEST_SENSE_DATA_LEN)
  {
    sendLen = params[4];
  }

  (void)SCSI_UpdateBotData(hmsc, response, sendLen);

  return 0;
}


/**
  * @brief  SCSI_SenseCode
  *         Load the last error code in the error list
  * @param  lun: Logical unit number
  * @param  sKey: Sense Key
  * @param  ASC: Additional Sense Code
  * @retval none

  */
void SCSI_SenseCode(USBD_HandleTypeDef *pdev, uint8_t lun, uint8_t sKey, uint8_t ASC)
{
  UNUSED(lun);
  USBD_MSC_BOT_HandleTypeDef *hmsc = (USBD_MSC_BOT_HandleTypeDef *)pdev->pClassDataCmsit[pdev->classId];

  if (hmsc == NULL)
  {
    return;
  }

  hmsc->scsi_sense[hmsc->scsi_sense_tail].Skey = sKey;
  hmsc->scsi_sense[hmsc->scsi_sense_tail].w.b.ASC = ASC;
  hmsc->scsi_sense[hmsc->scsi_sense_tail].w.b.ASCQ = 0U;
  hmsc->scsi_sense_tail++;

  if (hmsc->scsi_sense_tail == SENSE_LIST_DEEPTH)
  {
    hmsc->scsi_sense_tail = 0U;
  }
}


/**
  * @brief  SCSI_StartStopUnit
  *         Process Start Stop Unit command
  * @param  lun: Logical unit number
  * @param  params: Command parameters
  * @retval status
  */
static int8_t SCSI_StartStopUnit(USBD_HandleTypeDef *pdev, uint8_t lun, uint8_t *params)
{
  USBD_MSC_BOT_HandleTypeDef *hmsc;
  USBD_MSC_BOT_LUN_TypeDef *p_scsi_blk;

  if (SCSI_GetLunBlock(pdev, lun, &hmsc, &p_scsi_blk) < 0) return -1;

  if ((hmsc->scsi_medium_state == SCSI_MEDIUM_LOCKED) && ((params[4] & 0x3U) == 2U))
  {
    SCSI_SenseCode(pdev, lun, ILLEGAL_REQUEST, INVALID_FIELD_IN_COMMAND);

    return -1;
  }

  if ((params[4] & 0x3U) == 0x1U) /* START=1 */
  {
    hmsc->scsi_medium_state = SCSI_MEDIUM_UNLOCKED;
  }
  else if ((params[4] & 0x3U) == 0x2U) /* START=0 and LOEJ Load Eject=1 */
  {
    /* Sync before eject */
    if (((USBD_StorageTypeDef *)pdev->pUserData[pdev->classId])->Sync != NULL)
    {
      if (((USBD_StorageTypeDef *)pdev->pUserData[pdev->classId])->Sync(lun) != 0)
      {
        SCSI_SenseCode(pdev, lun, HARDWARE_ERROR, WRITE_FAULT);
        return -1;
      }
    }
    hmsc->scsi_medium_state = SCSI_MEDIUM_EJECTED;
  }
  else if ((params[4] & 0x3U) == 0x3U) /* START=1 and LOEJ Load Eject=1 */
  {
    hmsc->scsi_medium_state = SCSI_MEDIUM_UNLOCKED;
  }
  else
  {
    /* .. */
  }
  hmsc->bot_data_length = 0U;

  return 0;
}


/**
  * @brief  SCSI_AllowPreventRemovable
  *         Process Allow Prevent Removable medium command
  * @param  lun: Logical unit number
  * @param  params: Command parameters
  * @retval status
  */
static int8_t SCSI_AllowPreventRemovable(USBD_HandleTypeDef *pdev, uint8_t lun, uint8_t *params)
{
  UNUSED(lun);
  USBD_MSC_BOT_HandleTypeDef *hmsc = (USBD_MSC_BOT_HandleTypeDef *)pdev->pClassDataCmsit[pdev->classId];

  if (hmsc == NULL)
  {
    return -1;
  }

  if (params[4] == 0U)
  {
    hmsc->scsi_medium_state = SCSI_MEDIUM_UNLOCKED;
  }
  else
  {
    hmsc->scsi_medium_state = SCSI_MEDIUM_LOCKED;
  }

  hmsc->bot_data_length = 0U;

  return 0;
}


/**
  * @brief  SCSI_Read10
  *         Process Read10 command
  * @param  lun: Logical unit number
  * @param  params: Command parameters
  * @retval status
  */
static int8_t SCSI_Read10(USBD_HandleTypeDef *pdev, uint8_t lun, uint8_t *params)
{
  USBD_MSC_BOT_HandleTypeDef *hmsc;
  USBD_MSC_BOT_LUN_TypeDef *p_scsi_blk;

  if (SCSI_GetLunBlock(pdev, lun, &hmsc, &p_scsi_blk) < 0) return -1;

  if (hmsc->bot_state == USBD_BOT_IDLE) /* Idle */
  {
    /* case 10 : Ho <> Di */
    if ((hmsc->cbw.bmFlags & 0x80U) != 0x80U)
    {
      SCSI_SenseCode(pdev, hmsc->cbw.bLUN, ILLEGAL_REQUEST, INVALID_CDB);
      return -1;
    }

    if (hmsc->scsi_medium_state == SCSI_MEDIUM_EJECTED)
    {
      SCSI_SenseCode(pdev, lun, NOT_READY, MEDIUM_NOT_PRESENT);

      return -1;
    }

    if (((USBD_StorageTypeDef *)pdev->pUserData[pdev->classId])->IsReady(lun) != 0)
    {
      SCSI_SenseCode(pdev, lun, NOT_READY, MEDIUM_NOT_PRESENT);
      return -1;
    }

    p_scsi_blk->addr = ((uint32_t)params[2] << 24) |
                       ((uint32_t)params[3] << 16) |
                       ((uint32_t)params[4] <<  8) |
                       (uint32_t)params[5];

    p_scsi_blk->len = ((uint32_t)params[7] <<  8) | (uint32_t)params[8];

    if (SCSI_CheckAddressRange(pdev, lun, p_scsi_blk->addr, p_scsi_blk->len) < 0)
    {
      return -1; /* error */
    }

    /* cases 4,5 : Hi <> Dn */
    {
      uint32_t blen;
      if (SCSI_BlocksToBytes(p_scsi_blk->len, p_scsi_blk->size, &blen) < 0 ||
          hmsc->cbw.dDataLength != blen)
      {
        SCSI_SenseCode(pdev, hmsc->cbw.bLUN, ILLEGAL_REQUEST, INVALID_CDB);
        return -1;
      }
    }

    hmsc->bot_state = USBD_BOT_DATA_IN;
  }
  hmsc->bot_data_length = MSC_MEDIA_PACKET;

  return SCSI_ProcessRead(pdev, lun);
}

/**
  * @brief  SCSI_Read12
  *         Process Read12 command
  * @param  lun: Logical unit number
  * @param  params: Command parameters
  * @retval status
  */
static int8_t SCSI_Read12(USBD_HandleTypeDef *pdev, uint8_t lun, uint8_t *params)
{
  USBD_MSC_BOT_HandleTypeDef *hmsc;
  USBD_MSC_BOT_LUN_TypeDef *p_scsi_blk;

  if (SCSI_GetLunBlock(pdev, lun, &hmsc, &p_scsi_blk) < 0) return -1;

  if (hmsc->bot_state == USBD_BOT_IDLE) /* Idle */
  {
    /* case 10 : Ho <> Di */
    if ((hmsc->cbw.bmFlags & 0x80U) != 0x80U)
    {
      SCSI_SenseCode(pdev, hmsc->cbw.bLUN, ILLEGAL_REQUEST, INVALID_CDB);
      return -1;
    }

    if (hmsc->scsi_medium_state == SCSI_MEDIUM_EJECTED)
    {
      SCSI_SenseCode(pdev, lun, NOT_READY, MEDIUM_NOT_PRESENT);
      return -1;
    }

    if (((USBD_StorageTypeDef *)pdev->pUserData[pdev->classId])->IsReady(lun) != 0)
    {
      SCSI_SenseCode(pdev, lun, NOT_READY, MEDIUM_NOT_PRESENT);
      return -1;
    }

    p_scsi_blk->addr = ((uint32_t)params[2] << 24) |
                       ((uint32_t)params[3] << 16) |
                       ((uint32_t)params[4] <<  8) |
                       (uint32_t)params[5];

    p_scsi_blk->len = ((uint32_t)params[6] << 24) |
                      ((uint32_t)params[7] << 16) |
                      ((uint32_t)params[8] << 8) |
                      (uint32_t)params[9];

    if (SCSI_CheckAddressRange(pdev, lun, p_scsi_blk->addr, p_scsi_blk->len) < 0)
    {
      return -1; /* error */
    }

    /* cases 4,5 : Hi <> Dn */
    {
      uint32_t blen;
      if (SCSI_BlocksToBytes(p_scsi_blk->len, p_scsi_blk->size, &blen) < 0 ||
          hmsc->cbw.dDataLength != blen)
      {
        SCSI_SenseCode(pdev, hmsc->cbw.bLUN, ILLEGAL_REQUEST, INVALID_CDB);
        return -1;
      }
    }

    hmsc->bot_state = USBD_BOT_DATA_IN;
  }
  hmsc->bot_data_length = MSC_MEDIA_PACKET;

  return SCSI_ProcessRead(pdev, lun);
}

/**
  * @brief  SCSI_Write10
  *         Process Write10 command
  * @param  lun: Logical unit number
  * @param  params: Command parameters
  * @retval status
  */
static int8_t SCSI_Write10(USBD_HandleTypeDef *pdev, uint8_t lun, uint8_t *params)
{
  USBD_MSC_BOT_HandleTypeDef *hmsc;
  USBD_MSC_BOT_LUN_TypeDef *p_scsi_blk;

  if (SCSI_GetLunBlock(pdev, lun, &hmsc, &p_scsi_blk) < 0) return -1;

  uint32_t len;

#ifdef USE_USBD_COMPOSITE
  /* Get the Endpoints addresses allocated for this class instance */
  MSCOutEpAdd = USBD_CoreGetEPAdd(pdev, USBD_EP_OUT, USBD_EP_TYPE_BULK, (uint8_t)pdev->classId);
#endif /* USE_USBD_COMPOSITE */

  if (hmsc->bot_state == USBD_BOT_IDLE) /* Idle */
  {
    if (hmsc->cbw.dDataLength == 0U)
    {
      SCSI_SenseCode(pdev, hmsc->cbw.bLUN, ILLEGAL_REQUEST, INVALID_CDB);
      return -1;
    }

    /* case 8 : Hi <> Do */
    if ((hmsc->cbw.bmFlags & 0x80U) == 0x80U)
    {
      SCSI_SenseCode(pdev, hmsc->cbw.bLUN, ILLEGAL_REQUEST, INVALID_CDB);
      return -1;
    }

    /* Check whether Media is ready */
    if (((USBD_StorageTypeDef *)pdev->pUserData[pdev->classId])->IsReady(lun) != 0)
    {
      SCSI_SenseCode(pdev, lun, NOT_READY, MEDIUM_NOT_PRESENT);
      return -1;
    }

    /* Check If media is write-protected */
    if (((USBD_StorageTypeDef *)pdev->pUserData[pdev->classId])->IsWriteProtected(lun) != 0)
    {
      SCSI_SenseCode(pdev, lun, NOT_READY, WRITE_PROTECTED);
      return -1;
    }

    p_scsi_blk->addr = ((uint32_t)params[2] << 24) |
                       ((uint32_t)params[3] << 16) |
                       ((uint32_t)params[4] << 8) |
                       (uint32_t)params[5];

    p_scsi_blk->len = ((uint32_t)params[7] << 8) |
                      (uint32_t)params[8];

    /* check if LBA address is in the right range */
    if (SCSI_CheckAddressRange(pdev, lun, p_scsi_blk->addr, p_scsi_blk->len) < 0)
    {
      return -1; /* error */
    }

    /* cases 3,11,13 : Hn,Ho <> D0 */
    {
      uint32_t blen;
      if (SCSI_BlocksToBytes(p_scsi_blk->len, p_scsi_blk->size, &blen) < 0 ||
          hmsc->cbw.dDataLength != blen)
      {
        SCSI_SenseCode(pdev, hmsc->cbw.bLUN, ILLEGAL_REQUEST, INVALID_CDB);
        return -1;
      }
      len = blen;
    }

    len = MIN(len, MSC_MEDIA_PACKET);

    /* Prepare EP to receive first data packet */
    hmsc->bot_state = USBD_BOT_DATA_OUT;
    (void)USBD_LL_PrepareReceive(pdev, MSCOutEpAdd, hmsc->bot_data, len);
  }
  else /* Write Process ongoing */
  {
    return SCSI_ProcessWrite(pdev, lun);
  }

  return 0;
}

/**
  * @brief  SCSI_Write12
  *         Process Write12 command
  * @param  lun: Logical unit number
  * @param  params: Command parameters
  * @retval status
  */
static int8_t SCSI_Write12(USBD_HandleTypeDef *pdev, uint8_t lun, uint8_t *params)
{
  USBD_MSC_BOT_HandleTypeDef *hmsc;
  USBD_MSC_BOT_LUN_TypeDef *p_scsi_blk;

  if (SCSI_GetLunBlock(pdev, lun, &hmsc, &p_scsi_blk) < 0) return -1;

  uint32_t len;
#ifdef USE_USBD_COMPOSITE
  /* Get the Endpoints addresses allocated for this class instance */
  MSCOutEpAdd = USBD_CoreGetEPAdd(pdev, USBD_EP_OUT, USBD_EP_TYPE_BULK, (uint8_t)pdev->classId);
#endif /* USE_USBD_COMPOSITE */

  if (hmsc->bot_state == USBD_BOT_IDLE) /* Idle */
  {
    if (hmsc->cbw.dDataLength == 0U)
    {
      SCSI_SenseCode(pdev, hmsc->cbw.bLUN, ILLEGAL_REQUEST, INVALID_CDB);
      return -1;
    }

    /* case 8 : Hi <> Do */
    if ((hmsc->cbw.bmFlags & 0x80U) == 0x80U)
    {
      SCSI_SenseCode(pdev, hmsc->cbw.bLUN, ILLEGAL_REQUEST, INVALID_CDB);
      return -1;
    }

    /* Check whether Media is ready */
    if (((USBD_StorageTypeDef *)pdev->pUserData[pdev->classId])->IsReady(lun) != 0)
    {
      SCSI_SenseCode(pdev, lun, NOT_READY, MEDIUM_NOT_PRESENT);
      hmsc->bot_state = USBD_BOT_NO_DATA;
      return -1;
    }

    /* Check If media is write-protected */
    if (((USBD_StorageTypeDef *)pdev->pUserData[pdev->classId])->IsWriteProtected(lun) != 0)
    {
      SCSI_SenseCode(pdev, lun, NOT_READY, WRITE_PROTECTED);
      hmsc->bot_state = USBD_BOT_NO_DATA;
      return -1;
    }

    p_scsi_blk->addr = ((uint32_t)params[2] << 24) |
                       ((uint32_t)params[3] << 16) |
                       ((uint32_t)params[4] << 8) |
                       (uint32_t)params[5];

    p_scsi_blk->len = ((uint32_t)params[6] << 24) |
                      ((uint32_t)params[7] << 16) |
                      ((uint32_t)params[8] << 8) |
                      (uint32_t)params[9];

    /* check if LBA address is in the right range */
    if (SCSI_CheckAddressRange(pdev, lun, p_scsi_blk->addr, p_scsi_blk->len) < 0)
    {
      return -1; /* error */
    }

    /* cases 3,11,13 : Hn,Ho <> D0 */
    {
      uint32_t blen;
      if (SCSI_BlocksToBytes(p_scsi_blk->len, p_scsi_blk->size, &blen) < 0 ||
          hmsc->cbw.dDataLength != blen)
      {
        SCSI_SenseCode(pdev, hmsc->cbw.bLUN, ILLEGAL_REQUEST, INVALID_CDB);
        return -1;
      }
      len = blen;
    }

    len = MIN(len, MSC_MEDIA_PACKET);

    /* Prepare EP to receive first data packet */
    hmsc->bot_state = USBD_BOT_DATA_OUT;
    (void)USBD_LL_PrepareReceive(pdev, MSCOutEpAdd, hmsc->bot_data, len);
  }
  else /* Write Process ongoing */
  {
    return SCSI_ProcessWrite(pdev, lun);
  }

  return 0;
}

/**
  * @brief  SCSI_Verify10
  *         Process Verify10 command
  * @param  lun: Logical unit number
  * @param  params: Command parameters
  * @retval status
  */
static int8_t SCSI_Verify10(USBD_HandleTypeDef *pdev, uint8_t lun, uint8_t *params)
{
  USBD_MSC_BOT_HandleTypeDef *hmsc;
  USBD_MSC_BOT_LUN_TypeDef *p_scsi_blk;

  if (SCSI_GetLunBlock(pdev, lun, &hmsc, &p_scsi_blk) < 0) return -1;

  if ((params[1] & 0x02U) == 0x02U)
  {
    SCSI_SenseCode(pdev, lun, ILLEGAL_REQUEST, INVALID_FIELD_IN_COMMAND);
    return -1; /* Error, Verify Mode Not supported*/
  }

  if (SCSI_CheckAddressRange(pdev, lun, p_scsi_blk->addr, p_scsi_blk->len) < 0)
  {
    return -1; /* error */
  }

  hmsc->bot_data_length = 0U;

  return 0;
}

/**
  * @brief  SCSI_ReportLuns12
  *         Process ReportLuns command
  * @retval status
  */
static int8_t SCSI_ReportLuns(USBD_HandleTypeDef *pdev, uint8_t lun, uint8_t *params)
{
  uint32_t lun_list_length;
  uint32_t total_length;
  uint8_t lun_idx;

  UNUSED(lun);
  UNUSED(params);

  /* Define the report LUNs buffer Each LUN entry is 8 bytes */
  static uint8_t lun_report[8U * (MSC_BOT_MAX_LUN + 1U)];

  USBD_MSC_BOT_HandleTypeDef *hmsc = SCSI_GetBot(pdev);

  if (hmsc == NULL)
  {
    return -1;
  }

  /* Initialize the report LUNs buffer */
  (void)USBD_memset(lun_report, 0, sizeof(lun_report));

  /* Set the LUN list length in the first 4 bytes */
  lun_list_length = 8U * (hmsc->max_lun + 1U);
  lun_report[0] = (uint8_t)(lun_list_length >> 24);
  lun_report[1] = (uint8_t)(lun_list_length >> 16);
  lun_report[2] = (uint8_t)(lun_list_length >> 8);
  lun_report[3] = (uint8_t)(lun_list_length & 0xFFU);

  /* Update the LUN list */
  for (lun_idx = 0U; lun_idx <= hmsc->max_lun; lun_idx++)
  {
    /* LUN identifier is placed at the second byte of each 8-byte entry */
    lun_report[(8U * (lun_idx + 1U)) + 1U] = lun_idx;
  }

  /* Calculate the total length of the report LUNs buffer */
  total_length = lun_list_length + 8U;

  /* Update the BOT data with the report LUNs buffer */
  (void)SCSI_UpdateBotData(hmsc, lun_report, (uint16_t)total_length);

  return 0;
}

/**
  * @brief  SCSI_ReceiveDiagnosticResults
  *         Process SCSI_Receive Diagnostic Results command
  * @param  lun: Logical unit number
  * @param  params: Command parameters
  * @retval status
  */
static int8_t SCSI_ReceiveDiagnosticResults(USBD_HandleTypeDef *pdev, uint8_t lun, uint8_t *params)
{
  UNUSED(lun);
  USBD_MSC_BOT_HandleTypeDef *hmsc = (USBD_MSC_BOT_HandleTypeDef *)pdev->pClassDataCmsit[pdev->classId];
  uint16_t allocation_length;

  /* Extract the allocation length from the CDB */
  allocation_length = (((uint16_t)params[3] << 8) | (uint16_t)params[4]);

  if (allocation_length == 0U)
  {
    return 0;
  }

  /* Ensure the allocation length does not exceed the diagnostic data length */
  if (allocation_length > DIAGNOSTIC_DATA_LEN)
  {
    allocation_length = DIAGNOSTIC_DATA_LEN;
  }

  /* Send the diagnostic data to the host */
  (void)SCSI_UpdateBotData(hmsc, MSC_Diagnostic_Data, allocation_length);

  return 0;
}

/**
  * @brief  SCSI_SynchronizeCache10
  *         Process Synchronize Cache (10) command
  * @param  lun: Logical unit number
  * @param  params: Command parameters
  * @retval status
  */
static int8_t SCSI_SynchronizeCache10(USBD_HandleTypeDef *pdev, uint8_t lun, uint8_t *params)
{
  UNUSED(params);
  USBD_MSC_BOT_HandleTypeDef *hmsc;
  USBD_MSC_BOT_LUN_TypeDef *p_scsi_blk;

  if (SCSI_GetLunBlock(pdev, lun, &hmsc, &p_scsi_blk) < 0) return -1;

  /* Validate no data transfer expected */
  if (hmsc->cbw.dDataLength != 0U)
  {
    SCSI_SenseCode(pdev, lun, ILLEGAL_REQUEST, INVALID_CDB);
    return -1;
  }

  if (hmsc->scsi_medium_state == SCSI_MEDIUM_EJECTED)
  {
    SCSI_SenseCode(pdev, lun, NOT_READY, MEDIUM_NOT_PRESENT);
    hmsc->bot_state = USBD_BOT_NO_DATA;
    return -1;
  }

  if (((USBD_StorageTypeDef *)pdev->pUserData[pdev->classId])->IsReady(lun) != 0)
  {
    SCSI_SenseCode(pdev, lun, NOT_READY, MEDIUM_NOT_PRESENT);
    hmsc->bot_state = USBD_BOT_NO_DATA;
    return -1;
  }

  /* Call Sync if available */
  if (((USBD_StorageTypeDef *)pdev->pUserData[pdev->classId])->Sync != NULL)
  {
    if (((USBD_StorageTypeDef *)pdev->pUserData[pdev->classId])->Sync(lun) != 0)
    {
      SCSI_SenseCode(pdev, lun, HARDWARE_ERROR, WRITE_FAULT);
      return -1;
    }
  }

  hmsc->bot_data_length = 0U;
  return 0;
}

/**
  * @brief  SCSI_CheckAddressRange
  *         Check address range
  * @param  lun: Logical unit number
  * @param  blk_offset: first block address
  * @param  blk_nbr: number of block to be processed
  * @retval status
  */
static int8_t SCSI_CheckAddressRange(USBD_HandleTypeDef *pdev, uint8_t lun,
                                     uint32_t blk_offset, uint32_t blk_nbr)
{
  USBD_MSC_BOT_HandleTypeDef *hmsc;
  USBD_MSC_BOT_LUN_TypeDef *p_scsi_blk;

  if (SCSI_GetLunBlock(pdev, lun, &hmsc, &p_scsi_blk) < 0) return -1;

  if (blk_offset > p_scsi_blk->nbr)
  {
    SCSI_SenseCode(pdev, lun, ILLEGAL_REQUEST, ADDRESS_OUT_OF_RANGE);
    return -1;
  }

  if (blk_nbr > (p_scsi_blk->nbr - blk_offset))
  {
    SCSI_SenseCode(pdev, lun, ILLEGAL_REQUEST, ADDRESS_OUT_OF_RANGE);
    return -1;
  }

  return 0;
}

/**
  * @brief  SCSI_ProcessRead
  *         Handle Read Process
  * @param  lun: Logical unit number
  * @retval status
  */
static int8_t SCSI_ProcessRead(USBD_HandleTypeDef *pdev, uint8_t lun)
{
  USBD_MSC_BOT_HandleTypeDef *hmsc;
  USBD_MSC_BOT_LUN_TypeDef *p_scsi_blk;

  if (SCSI_GetLunBlock(pdev, lun, &hmsc, &p_scsi_blk) < 0) return -1;

  uint32_t len;

  if (SCSI_BlocksToBytes(p_scsi_blk->len, p_scsi_blk->size, &len) < 0) {
    SCSI_SenseCode(pdev, lun, HARDWARE_ERROR, UNRECOVERED_READ_ERROR);
    return -1;
  }

#ifdef USE_USBD_COMPOSITE
  /* Get the Endpoints addresses allocated for this class instance */
  MSCInEpAdd = USBD_CoreGetEPAdd(pdev, USBD_EP_IN, USBD_EP_TYPE_BULK, (uint8_t)pdev->classId);
#endif /* USE_USBD_COMPOSITE */

  len = MIN(len, MSC_MEDIA_PACKET);

  if (((USBD_StorageTypeDef *)pdev->pUserData[pdev->classId])->Read(lun, hmsc->bot_data,
                                                                    p_scsi_blk->addr,
                                                                    (len / p_scsi_blk->size)) < 0)
  {
    SCSI_SenseCode(pdev, lun, HARDWARE_ERROR, UNRECOVERED_READ_ERROR);
    return -1;
  }

  (void)USBD_LL_Transmit(pdev, MSCInEpAdd, hmsc->bot_data, len);

  p_scsi_blk->addr += (len / p_scsi_blk->size);
  p_scsi_blk->len -= (len / p_scsi_blk->size);

  /* case 6 : Hi = Di */
  hmsc->csw.dDataResidue -= len;

  if (p_scsi_blk->len == 0U)
  {
    hmsc->bot_state = USBD_BOT_LAST_DATA_IN;
  }

  return 0;
}

/**
  * @brief  SCSI_ProcessWrite
  *         Handle Write Process
  * @param  lun: Logical unit number
  * @retval status
  */
static int8_t SCSI_ProcessWrite(USBD_HandleTypeDef *pdev, uint8_t lun)
{
  USBD_MSC_BOT_HandleTypeDef *hmsc;
  USBD_MSC_BOT_LUN_TypeDef *p_scsi_blk;

  if (SCSI_GetLunBlock(pdev, lun, &hmsc, &p_scsi_blk) < 0) return -1;

  uint32_t len;

  if (SCSI_BlocksToBytes(p_scsi_blk->len, p_scsi_blk->size, &len) < 0) {
    SCSI_SenseCode(pdev, lun, HARDWARE_ERROR, WRITE_FAULT);
    return -1;
  }

#ifdef USE_USBD_COMPOSITE
  /* Get the Endpoints addresses allocated for this class instance */
  MSCOutEpAdd = USBD_CoreGetEPAdd(pdev, USBD_EP_OUT, USBD_EP_TYPE_BULK, (uint8_t)pdev->classId);
#endif /* USE_USBD_COMPOSITE */

  len = MIN(len, MSC_MEDIA_PACKET);

  if (((USBD_StorageTypeDef *)pdev->pUserData[pdev->classId])->Write(lun, hmsc->bot_data, p_scsi_blk->addr,
                                                                     (len / p_scsi_blk->size)) < 0)
  {
    SCSI_SenseCode(pdev, lun, HARDWARE_ERROR, WRITE_FAULT);
    return -1;
  }

  p_scsi_blk->addr += (len / p_scsi_blk->size);
  p_scsi_blk->len -= (len / p_scsi_blk->size);

  /* case 12 : Ho = Do */
  hmsc->csw.dDataResidue -= len;

  if (p_scsi_blk->len == 0U)
  {
    MSC_BOT_SendCSW(pdev, USBD_CSW_CMD_PASSED);
  }
  else
  {
    uint32_t nextLen;
    if (SCSI_BlocksToBytes(p_scsi_blk->len, p_scsi_blk->size, &nextLen) < 0) {
      SCSI_SenseCode(pdev, lun, HARDWARE_ERROR, WRITE_FAULT);
      return -1;
    }
    len = MIN(nextLen, MSC_MEDIA_PACKET);

    /* Prepare EP to Receive next packet */
    (void)USBD_LL_PrepareReceive(pdev, MSCOutEpAdd, hmsc->bot_data, len);
  }

  return 0;
}


/**
  * @brief  SCSI_UpdateBotData
  *         fill the requested Data to transmit buffer
  * @param  hmsc handler
  * @param  pBuff: Data buffer
  * @param  length: Data length
  * @retval status
  */
static int8_t SCSI_UpdateBotData(USBD_MSC_BOT_HandleTypeDef *hmsc,
                                 const uint8_t *pBuff, uint32_t length)
{
  if (hmsc == NULL)
  {
    return -1;
  }

  uint32_t len = length;
  len = MIN(len, MSC_MEDIA_PACKET);
  len = MIN(len, hmsc->cbw.dDataLength);

  hmsc->bot_data_length = (uint16_t)len;

  for (uint32_t i = 0U; i < len; i++)
  {
    hmsc->bot_data[i] = pBuff[i];
  }

  return 0;
}
/**
  * @}
  */


/**
  * @}
  */


/**
  * @}
  */

