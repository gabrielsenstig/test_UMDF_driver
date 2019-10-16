/*++

Copyright (c) Microsoft Corporation.  All rights reserved.

    THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY
    KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
    IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A PARTICULAR
    PURPOSE.

Module Name:

    TESTAPP.C

Abstract:

    Console test app for osrusbfx2 driver.

Environment:

    user mode only

--*/


#include <DriverSpecs.h>
_Analysis_mode_(_Analysis_code_type_user_code_)

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "devioctl.h"
#include "strsafe.h"

#pragma warning(disable:4200)  //
#pragma warning(disable:4201)  // nameless struct/union
#pragma warning(disable:4214)  // bit field types other than int

#include <cfgmgr32.h>
#include <basetyps.h>
#include "usbdi.h"
#include "public.h"
#include "inttypes.h"
#pragma warning(default:4200)
#pragma warning(default:4201)
#pragma warning(default:4214)

#define WHILE(a) \
while(__pragma(warning(disable:4127)) a __pragma(warning(disable:4127)))

#define MAX_DEVPATH_LENGTH 256
#define NUM_ASYNCH_IO   100
#define BUFFER_SIZE     1024
#define READER_TYPE   1
#define WRITER_TYPE   2

BOOL G_fDumpUsbConfig = FALSE;    // flags set in response to console command line switches
BOOL G_fDumpReadData = FALSE;
BOOL G_fRead = FALSE;
BOOL G_fWrite = FALSE;
BOOL G_fPlayWithDevice = FALSE;
BOOL G_fPerformAsyncIo = FALSE;
ULONG G_IterationCount = 1; //count of iterations of the test we are to perform
ULONG G_WriteLen = 512;         // #bytes to write
ULONG G_ReadLen = 512;          // #bytes to read
char* poutBuf = NULL;
BOOL
DumpUsbConfig( // defined in dump.c
    );



_Success_(return)
BOOL
GetDevicePath(
    _In_  LPGUID InterfaceGuid,
    _Out_writes_z_(BufLen) PWCHAR DevicePath,
    _In_ size_t BufLen
    )
{
    CONFIGRET cr = CR_SUCCESS;
    PWSTR deviceInterfaceList = NULL;
    ULONG deviceInterfaceListLength = 0;
    PWSTR nextInterface;
    HRESULT hr = E_FAIL;
    BOOL bRet = TRUE;

    cr = CM_Get_Device_Interface_List_Size(
        &deviceInterfaceListLength,
        InterfaceGuid,
        NULL,
        CM_GET_DEVICE_INTERFACE_LIST_PRESENT);
    if (cr != CR_SUCCESS) {
        printf("Error 0x%x retrieving device interface list size.\n", cr);
        goto clean0;
    }

    if (deviceInterfaceListLength <= 1) {
        bRet = FALSE;
        printf("Error: No active device interfaces found.\n"
            " Is the sample driver loaded?");
        goto clean0;
    }

    deviceInterfaceList = (PWSTR)malloc(deviceInterfaceListLength * sizeof(WCHAR));
    if (deviceInterfaceList == NULL) {
        bRet = FALSE;
        printf("Error allocating memory for device interface list.\n");
        goto clean0;
    }
    ZeroMemory(deviceInterfaceList, deviceInterfaceListLength * sizeof(WCHAR));

    cr = CM_Get_Device_Interface_List(
        InterfaceGuid,
        NULL,
        deviceInterfaceList,
        deviceInterfaceListLength,
        CM_GET_DEVICE_INTERFACE_LIST_PRESENT);
    if (cr != CR_SUCCESS) {
        printf("Error 0x%x retrieving device interface list.\n", cr);
        goto clean0;
    }

    nextInterface = deviceInterfaceList + wcslen(deviceInterfaceList) + 1;
    if (*nextInterface != UNICODE_NULL) {
        printf("Warning: More than one device interface instance found. \n"
            "Selecting first matching device.\n\n");
    }

    hr = StringCchCopy(DevicePath, BufLen, deviceInterfaceList);
    if (FAILED(hr)) {
        bRet = FALSE;
        printf("Error: StringCchCopy failed with HRESULT 0x%x", hr);
        goto clean0;
    }

clean0:
    if (deviceInterfaceList != NULL) {
        free(deviceInterfaceList);
    }
    if (CR_SUCCESS != cr) {
        bRet = FALSE;
    }

    return bRet;
}

_Check_return_
_Ret_notnull_
_Success_(return != INVALID_HANDLE_VALUE)
HANDLE
OpenDevice(
    _In_ BOOL Synchronous
    )

/*++
Routine Description:

    Called by main() to open an instance of our device after obtaining its name

Arguments:

    Synchronous - TRUE, if Device is to be opened for synchronous access.
                  FALSE, otherwise.

Return Value:

    Device handle on success else INVALID_HANDLE_VALUE

--*/

{
    HANDLE hDev;
    WCHAR completeDeviceName[MAX_DEVPATH_LENGTH];

	//DEFINE_GUID(GUID_DEVINTERFACE_USBUMDF2Driver2,
	//0xe4cadf6d, 0xbbc1, 0x4633, 0xbe, 0x8f, 0x7f, 0x57, 0x42, 0x78, 0x9d, 0xcd);
    //  & GUID_DEVINTERFACE_OSRUSBFX2
	//GUID_DEVINTERFACE_HuddlyI4Driver
	//GUID_DEVINTERFACE_USBUMDF2Driver3
	if ( ! (GetDevicePath((LPGUID)& GUID_DEVINTERFACE_HuddlyI4Driver,completeDeviceName,sizeof(completeDeviceName)/sizeof(completeDeviceName[0])) || 
		GetDevicePath((LPGUID)& GUID_DEVINTERFACE_USBUMDF2Driver2,completeDeviceName,sizeof(completeDeviceName) / sizeof(completeDeviceName[0]))))
    {
        return  INVALID_HANDLE_VALUE;
    }


    printf("\nDeviceName = (%S)\n", completeDeviceName);

    if(Synchronous) {
        hDev = CreateFile(completeDeviceName,
                GENERIC_WRITE | GENERIC_READ,
                FILE_SHARE_WRITE | FILE_SHARE_READ,
                NULL, // default security
                OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL,
                NULL);
    } else {

        hDev = CreateFile(completeDeviceName,
                GENERIC_WRITE | GENERIC_READ,
                FILE_SHARE_WRITE | FILE_SHARE_READ,
                NULL, // default security
                OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
                NULL);
    }

    if (hDev == INVALID_HANDLE_VALUE) {
        printf("Failed to open the device, error - %d", GetLastError());
    } else {
        printf("Opened the device successfully.\n");
    }

    return hDev;
}



VOID
Usage()

{
    printf("Usage for testapp:\n");
    printf("-r [n] where n can be any number\n");
    printf("-w [n] where n is number of bytes to write\n");
    printf("-v verbose -- dumps read data\n");

    return;
}


void
Parse(
    _In_ int argc,
    _In_reads_(argc) LPSTR  *argv
    )
{
    int i;
	
    if ( argc < 2 ) // give usage if invoked with no parms
        Usage();

    for (i=0; i<argc; i++) 
	{
        if (argv[i][0] == '-' ||
            argv[i][0] == '/') {
            switch(argv[i][1]) {
            case 'r':
            case 'R':
                if (i+1 >= argc) {
                    Usage();
                    exit(1);
                }
                else {
#pragma warning(suppress: 6385)
                    G_ReadLen = atoi(&argv[i+1][0]);
                                    G_fRead = TRUE;
                }
                i++;
                break;
            case 'w':
            case 'W':
                if (i+1 >= argc) {
                    Usage();
                    exit(1);
                }
                else {
                    G_WriteLen = (int)strlen(&argv[i+1][0]);
                                    G_fWrite = TRUE;
									printf("Write length to write               %d\n", (int)strlen(&argv[i + 1][0]));					
									poutBuf = &argv[i + 1][0];
									printf("Ut data								%s\n",poutBuf);


                }
                i++;
                break;
            case 'v':
            case 'V':
                G_fDumpReadData = TRUE;
                break;
            default:
                Usage();
            }
        }
    }
}

BOOL
Compare_Buffs(
    _In_reads_bytes_(buff1length) char *buff1,
    _In_ ULONG buff1length,
    _In_reads_bytes_(buff2length) char *buff2,
    _In_ ULONG buff2length
    )

{
    int ok = 1;

    if (buff1length != buff2length || memcmp(buff1, buff2, buff1length )) {
  
        ok = 0;
    }

    return ok;
}

#define NPERLN 8

VOID
Dump(UCHAR *b,int len)
{
    ULONG i;
    ULONG longLen = (ULONG)len / sizeof( ULONG );
    PULONG pBuf = (PULONG) b;

    // dump an ordinal ULONG for each sizeof(ULONG)'th byte
    printf("\n****** BEGIN DUMP decimal %d, 0x%x\n", len,len);
    for (i=0; i<longLen; i++) {
        printf("%04X ", *pBuf++);
        if (i % NPERLN == (NPERLN - 1)) {
            printf("\n");
        }
    }
    if (i % NPERLN != 0) {
        printf("\n");
    }
    printf("\n****** END DUMP decimal %d, 0x%x\n", len,len);
}


ULONG
AsyncIo(
    PVOID  ThreadParameter
    )
{
    HANDLE hDevice = INVALID_HANDLE_VALUE;
    HANDLE hCompletionPort = NULL;
    OVERLAPPED *pOvList = NULL;
    PUCHAR      buf = NULL;
    ULONG_PTR    i;
    ULONG   ioType = (ULONG)(ULONG_PTR)ThreadParameter;
    ULONG   error;

    hDevice = OpenDevice(FALSE);

    if (hDevice == INVALID_HANDLE_VALUE) {
        printf("Cannot open device %d\n", GetLastError());
        goto Error;
    }

    hCompletionPort = CreateIoCompletionPort(hDevice, NULL, 1, 0);

    if (hCompletionPort == NULL) {
        printf("Cannot open completion port %d \n",GetLastError());
        goto Error;
    }

    pOvList = (OVERLAPPED *)malloc(NUM_ASYNCH_IO * sizeof(OVERLAPPED));

    if (pOvList == NULL) {
        printf("Cannot allocate overlapped array \n");
        goto Error;
    }

    buf = (PUCHAR)malloc(NUM_ASYNCH_IO * BUFFER_SIZE);

    if (buf == NULL) {
        printf("Cannot allocate buffer \n");
        goto Error;
    }

    ZeroMemory(pOvList, NUM_ASYNCH_IO * sizeof(OVERLAPPED));
    ZeroMemory(buf, NUM_ASYNCH_IO * BUFFER_SIZE);

    //
    // Issue asynch I/O
    //

    for (i = 0; i < NUM_ASYNCH_IO; i++) {
        if (ioType == READER_TYPE) {
            if ( ReadFile( hDevice,
                      buf + (i* BUFFER_SIZE),
                      BUFFER_SIZE,
                      NULL,
                      &pOvList[i]) == 0) {

                error = GetLastError();
                if (error != ERROR_IO_PENDING) {
                    printf(" %Iu th read failed %d \n",i, GetLastError());
                    goto Error;
                }
            }

        } else {
            if ( WriteFile( hDevice,
                      buf + (i* BUFFER_SIZE),
                      BUFFER_SIZE,
                      NULL,
                      &pOvList[i]) == 0) {
                error = GetLastError();
                if (error != ERROR_IO_PENDING) {
                    printf(" %Iu th write failed %d \n",i, GetLastError());
                    goto Error;
                }
            }
        }
    }

    //
    // Wait for the I/Os to complete. If one completes then reissue the I/O
    //

    WHILE (1) {
        OVERLAPPED *completedOv;
        ULONG_PTR   key;
        ULONG     numberOfBytesTransferred;

        if ( GetQueuedCompletionStatus(hCompletionPort, &numberOfBytesTransferred,
                            &key, &completedOv, INFINITE) == 0) {
            printf("GetQueuedCompletionStatus failed %d\n", GetLastError());
            goto Error;
        }

        //
        // Read successfully completed. Issue another one.
        //

        if (ioType == READER_TYPE) {

            i = completedOv - pOvList;

            printf("Number of bytes read by request number %Iu is %d\n",
                                i, numberOfBytesTransferred);

            if ( ReadFile( hDevice,
                      buf + (i * BUFFER_SIZE),
                      BUFFER_SIZE,
                      NULL,
                      completedOv) == 0) {
                error = GetLastError();
                if (error != ERROR_IO_PENDING) {
                    printf("%Iu th Read failed %d \n", i, GetLastError());
                    goto Error;
                }
            }
        } else {

            i = completedOv - pOvList;

            printf("Number of bytes written by request number %Iu is %d\n",
                            i, numberOfBytesTransferred);

            if ( WriteFile( hDevice,
                      buf + (i * BUFFER_SIZE),
                      BUFFER_SIZE,
                      NULL,
                      completedOv) == 0) {
                error = GetLastError();
                if (error != ERROR_IO_PENDING) {
                    printf("%Iu th write failed %d \n", i, GetLastError());
                    goto Error;
                }
            }
        }
    }

Error:
    if (hDevice != INVALID_HANDLE_VALUE) {
        CloseHandle(hDevice);
    }

    if (hCompletionPort) {
        CloseHandle(hCompletionPort);
    }

    if (pOvList) {
        free(pOvList);
    }
    if (buf) {
        free(buf);
    }

    return 1;

}
void string2ByteArray(char* input, BYTE* output)
{
	int loop;
	int i;
	loop = 0;
	i = 0;
	while (input[loop] != '\0')
	{
		output[i++] = input[loop++];
	}
}


int
_cdecl
main(
    _In_ int argc,
    _In_reads_(argc) LPSTR  *argv
    )
{
    char * pinBuf = NULL;
    
  
	ULONG  nBytesRead1;
	ULONG  nBytesRead2;
    ULONG  nBytesWrite = 0;
    int    retValue = 0;
    UINT   success;
    HANDLE hRead = INVALID_HANDLE_VALUE;
    HANDLE hWrite = INVALID_HANDLE_VALUE;
    ULONG  i;
	typedef unsigned char BYTE;

	uint8_t msg_name[] = { 'h','l','i','n','k','-','m','b','-','s','u','b','s','c','r','i','b','e' };
	uint16_t sizeMsg_name = (uint16_t) sizeof(msg_name);
	uint8_t payload[] = { 'p','r','o','d','i','n','f','o','/','g','e','t','_','m','s','g','p','a','c','k','_','r','e','p','l','y' };
	uint32_t sizePayload = (uint32_t)sizeof(payload);
	uint8_t outbuffer[] = { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,'m','m','n','n','n','n','h','l','i','n','k','-','m','b','-','s','u','b','s','c','r','i','b','e', 'p','r','o','d','i','n','f','o','/','g','e','t','_','m','s','g','p','a','c','k','_','r','e','p','l','y' };
	outbuffer[15] = (sizePayload >> 24) & 0xFF;
	outbuffer[14] = (sizePayload >> 16) & 0xFF;
	outbuffer[13] = (sizePayload >> 8) & 0xFF;
	outbuffer[12] = sizePayload & 0xFF;
	outbuffer[11] = (sizeMsg_name >> 8) & 0xFF;
	outbuffer[10] = sizeMsg_name & 0xFF;

	uint8_t msg_name2[] = { 'p','r','o','d','i','n','f','o','/','g','e','t','_','m','s','g','p','a','c', 'k' };
	uint16_t sizeMsg_name2 = (uint16_t) sizeof(msg_name2);
	uint8_t payload2[] = { "" };
	uint32_t sizePayload2 = (uint32_t)sizeof(payload2);
	uint8_t outbuffer2[] = { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,'m','m','n','n','n','n','p','r','o','d','i','n','f','o','/','g','e','t','_','m','s','g','p','a','c', 'k' };
	outbuffer2[15] = (sizePayload2 >> 24) & 0xFF;
	outbuffer2[14] = (sizePayload2 >> 16) & 0xFF;
	outbuffer2[13] = (sizePayload2 >> 8) & 0xFF;
	//outbuffer2[12] = sizePayload2 & 0xFF;
	outbuffer2[12] = 0x00;
	outbuffer2[11] = (sizeMsg_name2 >> 8) & 0xFF;
	outbuffer2[10] = sizeMsg_name2 & 0xFF;

	uint8_t msg_name3[] = { 'h','l','i','n','k','-','m','b','-','u','n','s','u','b','s','c','r','i','b','e' };
	uint16_t sizeMsg_name3 = (uint16_t) sizeof(msg_name3);
	uint8_t payload3[] = { 'p','r','o','d','i','n','f','o','/','g','e','t','_','m','s','g','p','a','c','k','_','r','e','p','l','y' };
	uint32_t sizePayload3 = (uint32_t)sizeof(payload3);
	uint8_t outbuffer3[] = { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,'m','m','n','n','n','n','h','l','i','n','k','-','m','b','-','u','n','s','u','b','s','c','r','i','b','e', 'p','r','o','d','i','n','f','o','/','g','e','t','_','m','s','g','p','a','c','k','_','r','e','p','l','y' };
	outbuffer3[15] = (sizePayload3 >> 24) & 0xFF;
	outbuffer3[14] = (sizePayload3 >> 16) & 0xFF;
	outbuffer3[13] = (sizePayload3 >> 8) & 0xFF;
	outbuffer3[12] = sizePayload3 & 0xFF;
	outbuffer3[11] = (sizeMsg_name3 >> 8) & 0xFF;
	outbuffer3[10] = sizeMsg_name3 & 0xFF;


    Parse(argc, argv );

 
    // doing a read, write, or both test
    //
    if ((G_fRead) || (G_fWrite)) {

        if (G_fRead) {
            
			printf("Reading data\n");
            //
            // open the output file
            //
            hRead = OpenDevice(TRUE);
            if(hRead == INVALID_HANDLE_VALUE) {
                retValue = 1;
                goto exit;
            }

			if (G_fDumpReadData)
			{ // round size to sizeof ULONG for readable dumping
				while (G_ReadLen % sizeof(ULONG))
				{
					G_ReadLen++;
				}
			}
            pinBuf = (char*)malloc(G_ReadLen);
			printf(pinBuf);
        }

        if (G_fWrite) {
            if ( G_fDumpReadData ) { // round size to sizeof ULONG for readable dumping
                while( G_WriteLen % sizeof( ULONG ) ) {
                    G_WriteLen++;
                }
            }

            //
            // open the output file
            //
            hWrite = OpenDevice(TRUE);
            if(hWrite == INVALID_HANDLE_VALUE) {
               retValue = 1;
               goto exit;
            }

            //poutBuf = malloc(G_WriteLen); //Allocation the lenght of the out buffer
			printf(poutBuf);
        }

        for (i = 0; i < G_IterationCount; i++) {
            ULONG  j;

            if (G_fWrite && poutBuf && hWrite != INVALID_HANDLE_VALUE) {

                PULONG pOut = (PULONG) poutBuf;
                ULONG  numLongs = G_WriteLen / sizeof( ULONG );

                //
                // put some data in the output buffer
                //
                for (j=0; j<numLongs; j++) {
                    *(pOut+j) = j;
                }

                //
                // send the write
                //
				printf("\nStart send sequnce \n");
				printf("Outbuffer							%s with this size of the buffer %d\n", outbuffer, (int)sizeof(outbuffer));
				//nBytesWrite = 0;
				success = WriteFile(hWrite, outbuffer, sizeof(outbuffer), &nBytesWrite, NULL);
                
				if(success == 0) {
                    printf("WriteFile failed - error %d\n", GetLastError());
                    retValue = 1;
                    goto exit;
                }
				printf("WriteFile first argument %d\n", nBytesWrite);
				
				nBytesWrite = 0;
				success = WriteFile(hWrite, outbuffer2, sizeof(outbuffer2), &nBytesWrite, NULL);
                
				if(success == 0) {
                    printf("WriteFile failed - error %d\n", GetLastError());
                    retValue = 1;
                    goto exit;
                }
				printf("WriteFile second argument %d\n", nBytesWrite);
				
				
				
				
                

                
            }

            if (G_fRead && pinBuf) {
				printf("Readfile waiting for data\n");
				uint8_t pinBuf2[1024];
				uint8_t pinBuf1[1024];

                success = ReadFile(hRead, pinBuf1, G_ReadLen, &nBytesRead1, NULL);
                
				if(success == 0) {
                    printf("ReadFile failed - error %d\n", GetLastError());
                    retValue = 1;
                    goto exit;
                }
				for (i = 0; i < (int)nBytesRead1; i++)
				{
					wprintf(L"%c", (char) * (pinBuf1 + i));
				}
                printf("Read (%04.4u) : request %06.6u bytes -- %06.6u bytes read\n",i, G_ReadLen, nBytesRead1);
				
				
				
				
				success = ReadFile(hRead, pinBuf2, G_ReadLen, &nBytesRead2, NULL);
				if (success == 0) {
					printf("ReadFile failed - error %d\n", GetLastError());
					retValue = 1;
					goto exit;
				}
				
				for (i = 0; i < (int)nBytesRead2; i++)
				{
					wprintf(L"%c", (char) * (pinBuf2 + i));
				}

				printf("WriteFile third argument %d\n", nBytesRead2);
				printf("Read (%04.4u) : request %06.6u bytes -- %06.6u bytes read\n",i, G_ReadLen, nBytesRead2);


				nBytesWrite = 0;
				success = WriteFile(hWrite, outbuffer3, sizeof(outbuffer3), &nBytesWrite, NULL);

				if (success == 0) {
					printf("WriteFile failed - error %d\n", GetLastError());
					retValue = 1;
					goto exit;
				}

				printf("WriteFile %s third argument %d\n", outbuffer3, nBytesWrite);


                if (G_fWrite && poutBuf) {
                    if( G_fDumpReadData ) {
                        printf("Dumping read buffer 1\n");
                        Dump( (PUCHAR) pinBuf1,  nBytesRead1 );
						printf("Dumping read buffer 2\n");
						Dump((PUCHAR)pinBuf2, nBytesRead2);
                        printf("Dumping write buffer\n");
                        Dump( (PUCHAR) poutBuf, nBytesWrite);
                    }
                
                }
            }
        }

    }

exit:

    if (pinBuf) {
        free(pinBuf);
    }

    if (poutBuf) {
        free(poutBuf);
    }
	

    // close devices if needed
    if (hRead != INVALID_HANDLE_VALUE) {
        CloseHandle(hRead);
    }

    if (hWrite != INVALID_HANDLE_VALUE) {
        _Analysis_assume_(hWrite != NULL);
        CloseHandle(hWrite);
    }

    return retValue;
}


