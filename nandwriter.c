/*
 * nandwriter.c
*/

/*
 * Copyright (C) 2012 Texas Instruments Incorporated - http://www.ti.com/ 
*/
/* 
 *  Redistribution and use in source and binary forms, with or without 
 *  modification, are permitted provided that the following conditions 
 *  are met:
 *
 *    Redistributions of source code must retain the above copyright 
 *    notice, this list of conditions and the following disclaimer.
 *
 *    Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the 
 *    documentation and/or other materials provided with the   
 *    distribution.
 *
 *    Neither the name of Texas Instruments Incorporated nor the names of
 *    its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS 
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT 
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT 
 *  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, 
 *  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT 
 *  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT 
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE 
 *  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
*/
/* --------------------------------------------------------------------------
    FILE        : nandwriter.c                                                   
    PURPOSE     : NAND writer main program
    PROJECT     : DA8xx CCS NAND Flashing Utility (for use on DM355 EVM)
    AUTHOR      : Daniel Allred
    DESC        : CCS-based utility to flash the DM644x in preparation for 
                  NAND booting
 ----------------------------------------------------------------------------- */

// C standard I/O library
#include "stdio.h"
#include "stdlib.h"
#include "string.h"

// General type include
#include "tistdtypes.h"

// Device specific CSL
#include "device.h"

// Debug print header file
#include "debug.h"

// This module's header file 
#include "nandwriter.h"

// NAND driver include
#include "nand.h"
#include "device_nand.h"

// Misc. utility function include
#include "util.h"


/************************************************************
* Explicit External Declarations                            *
************************************************************/


/************************************************************
* Local Macro Declarations                                  *
************************************************************/

#define NANDWIDTH_16
//#define NANDWIDTH_8

/************************************************************
* Local Typedef Declarations                                *
************************************************************/


/************************************************************
* Local Function Declarations                               *
************************************************************/

static Uint32 nandwriter(void);
static Uint32 LOCAL_writeData(NAND_InfoHandle hNandInfo, Uint8 *srcBuf, Uint32 totalPageCnt);


/************************************************************
* Global Variable Definitions
************************************************************/

extern VUint32 __FAR__ DDRStart;
extern VUint32 __FAR__ NANDStart;

// Global variables for page buffers 
static Uint8* gNandTx;
static Uint8* gNandRx;

/************************************************************
* Global Function Definitions                               *
************************************************************/

void main( void )
{
  int status;

  // Init memory alloc pointer
  UTIL_setCurrMemPtr(0);

  // System init
  if (DEVICE_init() != E_PASS)
  {
    exit(1);
  }

  // Execute the NAND flashing
  status = nandwriter();

  if (status != E_PASS)
  {
    DEBUG_printString("\n\nNAND flashing failed!\r\n");
  }
  else
  {
    DEBUG_printString( "\n\nNAND boot preparation was successful!\r\n" );
  }
}


/************************************************************
* Local Function Definitions                                *
************************************************************/

static Uint32 nandwriter()
{
  Uint32 numPagesAIS;

  NAND_InfoHandle  hNandInfo;

  FILE  *fPtr;
  Uint8  *aisPtr;
  Int32  aisFileSize = 0,aisAllocSize = 0;
  Int8  fileName[256];
  Int32 i=0;

  DEBUG_printString("Starting ");
  DEBUG_printString((String)devString);
  DEBUG_printString(" NANDWriter.\r\n");

  // Initialize NAND Flash
#if defined(NANDWIDTH_8)
  hNandInfo = NAND_open((Uint32)&NANDStart, DEVICE_BUSWIDTH_8BIT );
#elif defined(NANDWIDTH_16)
  hNandInfo = NAND_open((Uint32)&NANDStart, DEVICE_BUSWIDTH_16BIT );
#else
  #error "Must define one of NANDWIDTH_8 or NANDWIDTH_16"
#endif
  
  if (hNandInfo == NULL)
  {
    DEBUG_printString( "\tERROR: NAND Initialization failed.\r\n" );
    return E_FAIL;
  }

  // Erase the nand flash
  DEBUG_printString("Do you want to global erase NAND flash?");
  DEBUG_readString(fileName);
  fflush(stdin);
  if (strcmp(fileName,"y") == 0)
  {
    if (NAND_globalErase(hNandInfo) != E_PASS)
    {
      DEBUG_printString("\tERROR: NAND gloabal erase failed!");
    }
  }

  // Read the file from host
  DEBUG_printString("Enter the binary AIS file name to flash (enter 'none' to skip) :\r\n");
  DEBUG_readString(fileName);
  fflush(stdin);

  if (strcmp(fileName,"none") != 0)
  {
    // Open an File from the hard drive
    fPtr = fopen(fileName, "rb");
    if(fPtr == NULL)
    {
      DEBUG_printString("\tERROR: File ");
      DEBUG_printString(fileName);
      DEBUG_printString(" open failed.\r\n");
      return E_FAIL;
    }

    // Read file size
    fseek(fPtr,0,SEEK_END);
    aisFileSize = ftell(fPtr);

    if(aisFileSize == 0)
    {
      DEBUG_printString("\tERROR: File read failed.. Closing program.\r\n");
      fclose (fPtr);
      return E_FAIL;
    }

    numPagesAIS = 0;
    while ( (numPagesAIS * hNandInfo->dataBytesPerPage)  < aisFileSize )
    {
      numPagesAIS++;
    }

    //We want to allocate an even number of pages.
    aisAllocSize = numPagesAIS * hNandInfo->dataBytesPerPage;

    // Setup pointer in RAM
    aisPtr = (Uint8 *) UTIL_allocMem(aisAllocSize);

    // Clear memory
    for (i=0; i<aisAllocSize; i++)
      aisPtr[i]=0xFF;

    // Go to start of file
    fseek(fPtr,0,SEEK_SET);

    // Read file data
    if (aisFileSize != fread(aisPtr, 1, aisFileSize, fPtr))
    {
      DEBUG_printString("\tWARNING: File Size mismatch.\r\n");
    }

    // Close file
    fclose (fPtr);

    // Write the file data to the NAND flash
    if (LOCAL_writeData(hNandInfo, aisPtr, numPagesAIS) != E_PASS)
    {
      printf("\tERROR: Write failed.\r\n");
      return E_FAIL;
    }
  }

  return E_PASS;
}

// Generic function to write a UBL or Application header and the associated data
static Uint32 LOCAL_writeData(NAND_InfoHandle hNandInfo, Uint8 *srcBuf, Uint32 totalPageCnt)
{
  Uint32    blockNum,pageNum,pageCnt;
  Uint32    numBlks;
  Uint32    i;
  Uint8     *dataPtr;

  gNandTx = (Uint8 *) UTIL_allocMem(NAND_MAX_PAGE_SIZE);
  gNandRx = (Uint8 *) UTIL_allocMem(NAND_MAX_PAGE_SIZE);

  for (i=0; i<NAND_MAX_PAGE_SIZE; i++)  
  {
    gNandTx[i]=0xff;
    gNandRx[i]=0xff;
  }  
  
  // Get total number of blocks needed
  numBlks = 0;
  while ( (numBlks*hNandInfo->pagesPerBlock)  < totalPageCnt )
  {
    numBlks++;
  }
  DEBUG_printString("Number of blocks needed for data: ");
  DEBUG_printHexInt(numBlks);
  DEBUG_printString("\r\n");

  // Start in block 1 (leave block 0 alone)
  blockNum = 1;

  // Unprotect all blocks of the device
  if (NAND_unProtectBlocks(hNandInfo, blockNum, (hNandInfo->numBlocks-1)) != E_PASS)
  {
    blockNum++;
    DEBUG_printString("Unprotect failed.\r\n");
    return E_FAIL;
  }

  while (blockNum < hNandInfo->numBlocks)
  {
    // Find first good block
    while (NAND_badBlockCheck(hNandInfo,blockNum) != E_PASS)
    {
      blockNum++;
    }

    // Erase the current block
    NAND_eraseBlocks(hNandInfo,blockNum,1);

    // Start writing in page 0 of current block
    pageNum = 0;
    pageCnt = 0;

    // Setup data pointer
    dataPtr = srcBuf;

    // Start page writing loop
    do
    {
      DEBUG_printString((String)"Writing image data to block ");
      DEBUG_printHexInt(blockNum);
      DEBUG_printString((String)", page ");
      DEBUG_printHexInt(pageNum);
      DEBUG_printString((String)"\r\n");

      // Write the AIS image data to the NAND device
      if (NAND_writePage(hNandInfo, blockNum,  pageNum, dataPtr) != E_PASS)
      {
        DEBUG_printString("Write failed. Marking block as bad...\n");
        NAND_reset(hNandInfo);
        NAND_badBlockMark(hNandInfo,blockNum);
        dataPtr -=  pageNum * hNandInfo->dataBytesPerPage;
        blockNum++;
        continue;
      }
    
      UTIL_waitLoop(200);
    
      // Verify the page just written
      if (NAND_verifyPage(hNandInfo, blockNum, pageNum, dataPtr, gNandRx) != E_PASS)
      {
        DEBUG_printString("Verify failed. Marking block as bad...\n");
        NAND_reset(hNandInfo);
        NAND_badBlockMark(hNandInfo,blockNum);
        dataPtr -=  pageNum * hNandInfo->dataBytesPerPage;
        blockNum++;
        continue;
      }
    
      pageNum++;
      pageCnt++;
      dataPtr +=  hNandInfo->dataBytesPerPage;
  
      if (pageNum == hNandInfo->pagesPerBlock)
      {
        // A block transition needs to take place; go to next good block
        do
        {
          blockNum++;
        }
        while (NAND_badBlockCheck(hNandInfo,blockNum) != E_PASS);

        // Erase the current block
        NAND_eraseBlocks(hNandInfo,blockNum,1);

        pageNum = 0;
      }
    } while (pageCnt < totalPageCnt);

    NAND_protectBlocks(hNandInfo);
    break;
  }
  return E_PASS;
}


/***********************************************************
* End file                                                 
************************************************************/


