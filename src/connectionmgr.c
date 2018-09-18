#include "SWIWWANCMAPI.h"
#include "displaymgmt.h"
#include <unistd.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include "qmerrno.h"
#include <sys/timeb.h>
#include "GpsAdapter.h"

#define SUCCESS                  0
#define FAIL                     1
#define MAX_FIELD_SIZE           128
#define DEV_NODE_SZ              256
#define DEV_KEY_SZ               16

#define rcprint(s, u) syslog( LOG_USER, "%s: rc = 0x%X", s, u )

/* Device information structure */
typedef struct device_info_param{
  CHAR deviceNode[DEV_NODE_SZ];
  CHAR deviceKey[DEV_KEY_SZ];
}device_info_t;

/* path to sdk binary */
static char *sdkbinpath = NULL;
static device_info_t             devices[1] = { { {'\0'}, {'\0'} } };
static device_info_t             *pdev = &devices[0];

typedef void( *sighandler_t )( int );



void QuitApplication()
{
    free(sdkbinpath);
    fprintf( stderr, "Exiting Application!!!\n" );
    QCWWANDisconnect();
    exit( EXIT_SUCCESS );
}

ULONG StartSDK(BYTE modem_index)
{
    ULONG resultCode  = 0;
    BYTE  devicesSize = 1;

    /* Set SDK image path */
    if( SUCCESS != (resultCode = SetSDKImagePath(sdkbinpath)) )
    {
        rcprint( __func__, resultCode );
        return resultCode;
    }

    /* Establish APP<->SDK IPC */
    if( SUCCESS != (resultCode = SLQSStart(modem_index, NULL)) )
    {
        /* first attempt failed, kill SDK process */
        if( SUCCESS != SLQSKillSDKProcess() )
        {
            return resultCode;
        }
        else
        {
            /* start new SDK process */
            if( SUCCESS != (resultCode = SLQSStart(modem_index, NULL)) )
            {
                return resultCode;
            }
        }
    }

    /* Enumerate the device */
    while (QCWWAN2kEnumerateDevices(&devicesSize, (BYTE *)pdev) != 0)
    {
        printf ("\nUnable to find device..\n");
        sleep(1);
    }

    /* Connect to the SDK */
    resultCode = QCWWANConnect( pdev->deviceNode,
                                pdev->deviceKey );
    return resultCode;
}

/*************************************************************************
  Signal handler: To take care of <defunct> process
*************************************************************************/
/*
 * Name:     appSignalInstall
 *
 * Purpose:  install signal handler
 */
void appSignalInstall( unsigned int signo,  void (*functionp)(int, siginfo_t *, void *) )
{
    struct sigaction sa;

    if (functionp == NULL)
        return;
    sa.sa_sigaction = functionp;
    sigemptyset(&sa.sa_mask);

    sa.sa_flags = SA_NODEFER | SA_SIGINFO;

    if (sigaction(signo, &sa, NULL) < 0)
    {
        fprintf(stderr,"Signal handling falied");
    }
}

void appSIGCHLDhandler (int signo, siginfo_t *siginfop, void *contextp)
{
    int status;

    UNUSEDPARAM ( signo );
    UNUSEDPARAM ( siginfop );
    UNUSEDPARAM ( contextp );

    /* Allow the SDK process to terminate */
    wait(&status);
}

void SignalInit()
{
   appSignalInstall (SIGCHLD, appSIGCHLDhandler);
}

/*************************************************************************
 *             Display Window Fields Initialization functions.
 ************************************************************************/

void UpdateSigInfoDisplay(
    nasGetSigInfoResp       *pSigInfo,

    nasCellLocationInfoResp *pCellLocationInfo,
    FILE                    *pFile)
{
    CHAR respBuf[MAX_FIELD_SIZE];
  
    if ( NULL != pSigInfo->pLTESSInfo )
    {
        if( -1 != pSigInfo->pLTESSInfo->rsrq &&
            -1 != pSigInfo->pLTESSInfo->rsrp &&
            -1 != pSigInfo->pLTESSInfo->rssi &&
            -1 != pSigInfo->pLTESSInfo->snr  
     )
        {
            /* Update the value of RSRQ */
            memset( respBuf, 0, MAX_FIELD_SIZE );
            fprintf(pFile,"%d     %d     %d     %d     %d       %d      \n",

                    pSigInfo->pLTESSInfo->rsrq, 
                    pSigInfo->pLTESSInfo->rsrp,
                    pSigInfo->pLTESSInfo->rssi,
                    pSigInfo->pLTESSInfo->snr,

                    pCellLocationInfo->pLTEInfoIntrafreq->globalCellId,
                    pCellLocationInfo->pLTEInfoIntrafreq->tac              );
        }
    }

}

/*
 * Name:     DisplayRSSInfo
 */
void DisplayRSSInfo(FILE *pFile)
{
    LTESSInfo               lteSSInfo;

    LTEInfoIntrafreq        lteInfoIntrafreq;
    LTEInfoInterfreq        lteInfoInterfreq;

    nasGetSigInfoResp       sigResp;

    nasCellLocationInfoResp locResp;



    ULONG                   sigResultCode = 0;

    ulong                   locResultCode = 0;

    memset(&sigResp,0,sizeof(nasGetSigInfoResp));

    memset(&locResp,0,sizeof(nasCellLocationInfoResp));

    sigResp.pLTESSInfo      =   &lteSSInfo;

    locResp.pLTEInfoIntrafreq  =   &lteInfoIntrafreq;
    locResp.pLTEInfoInterfreq  =   &lteInfoInterfreq;


    /* Get the information about the received Signal Strength */
    sigResultCode = SLQSNasGetSigInfo( &sigResp );

    locResultCode = SLQSNasGetCellLocationInfo( &locResp );

    if( SUCCESS != sigResultCode && SUCCESS != locResultCode)
    {   

        UpdateSigInfoDisplay( NULL, NULL, pFile );
        return;
    }

    /* Display the RSS Information */
    UpdateSigInfoDisplay( &sigResp, &locResp, pFile );
}

int main( int argc, const char *argv[])
{
    int value;
    BYTE modem_index;
    ULONG resultCode = 0;
    int n;
    

    if( argc < 3 )
    {
        fprintf( stderr, "usage: %s <path to sdk binary> <modem_index>\n", argv[0] );
        exit( EXIT_SUCCESS );
    }

    if( NULL == (sdkbinpath = (char*)malloc(strlen(argv[1]) + 1)) )
    {
        perror(__func__);
        exit( EXIT_FAILURE );
    }

    strncpy( sdkbinpath, argv[1], strlen(argv[1]) + 1);

    value = atoi(argv[2]);
    if ( 
            (value < 0)
            || (value > 8)
       )
    {
        fprintf( stderr, "cannot convert second param into modem_index\n");
        exit( EXIT_FAILURE );
    }
    modem_index = value;

    /* Start the SDK */
    resultCode = StartSDK(modem_index);

    if( SUCCESS != resultCode )
    {
        free(sdkbinpath);
        /* Display the failure reason */
        fprintf( stderr,  "Failed to start SDK : Exiting App\n"\
                          "Failure Code : %u\n", resultCode );
        /* Failed to start SDK, exit the application */
        exit( EXIT_FAILURE );
    }

    SignalInit();

    FILE    *pFile;
    pFile = fopen( "Meas_log_sierra.txt", "wb" );
    struct timeb tp1,tp2;
    ftime(&tp1);
    for(n=100; n>0; n--)
    {
        DisplayRSSInfo(pFile);
 //       fprintf(stderr, "%d\n",n);
    }
    ftime(&tp2);
    float result = ((tp2.time*1e3 + tp2.millitm) - (tp1.time*1e3 + tp1.millitm))/1e3;
    fprintf(stderr, "%f[ms]\n", result);
fclose( pFile );  
return SUCCESS;
}


