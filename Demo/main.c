// main.c
// Authored by Jared Hull
// Modified by Roope Lindström & Emil Pirinen
//
// Main initialises the devices
// Tasks simulate car lights

#include <FreeRTOS.h>
#include <task.h>
#include <string.h>

#include "interrupts.h"
#include "gpio.h"
#include "video.h"
#include "FreeRTOS_IP.h"
#include "FreeRTOS_Sockets.h"

//Only for debug, normally should not
//   include private header
#include "FreeRTOS_IP_Private.h"

#define ACCELERATE_LED_GPIO 23
#define BRAKE_LED_GPIO 24
#define CLUTCH_LED_GPIO 25

#define ACCELERATE_TASK_DELAY 1000
#define BRAKE_TASK_DELAY 2000
#define CLUTCH_TASK_DELAY 5000

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef long int32_t;

xTaskHandle acc;
int accState = 1;

void task(int pin, int delay)
{
    int i = 0;
    while (1)
    {
        i = i ? 0 : 1;
        SetGpio(pin, i);
        vTaskDelay(delay);
        FreeRTOS_printf("Blink");
    }
}

void taskAccelerate()
{
    task(ACCELERATE_LED_GPIO, ACCELERATE_TASK_DELAY);
}

void taskBrake()
{
    task(BRAKE_LED_GPIO, BRAKE_TASK_DELAY);
}

void taskClutch()
{
    task(CLUTCH_LED_GPIO, CLUTCH_TASK_DELAY);
}

void runCommand(uint8_t* cmd) {
    if (strcmp(cmd, "acc")) {
        if (accState) vTaskSuspend(acc);
        else vTaskResume(acc);
        accState = accState ? 0 : 1; 
    }
}

#undef CREATE_SOCK_TASK
// #define CREATE_SOCK_TASK
#define tcpechoSHUTDOWN_DELAY (pdMS_TO_TICKS(5000))

//server task DOES work in this build, it DOES accept a connection
void serverListenTask()
{
    int status = 0;
    static const portTickType xReceiveTimeOut = portMAX_DELAY;
    const portTickType xDelay8s = pdMS_TO_TICKS(8000UL);
    const portTickType xDelay500ms = pdMS_TO_TICKS(500UL);
    const portBASE_TYPE xBacklog = 4;

    portTickType xTimeOnShutdown;
    uint8_t *pucRxBuffer;
    // for debug
    FreeRTOS_Socket_t *sockt;

    //printAddressConfiguration();
    volatile int times = 10;
    println("Server task starting", BLUE_TEXT);
    if (FreeRTOS_IsNetworkUp())
    {
        println("Network is UP", BLUE_TEXT);
    }
    else
    {
        println("Network is Down", BLUE_TEXT);
        while (!FreeRTOS_IsNetworkUp())
        {
            vTaskDelay(xDelay500ms);
        }
    }
    println("Serv tsk done wait net", BLUE_TEXT);

    Socket_t listen_sock;
    listen_sock = FreeRTOS_socket(FREERTOS_AF_INET, FREERTOS_SOCK_STREAM, FREERTOS_IPPROTO_TCP);
    printHex("listenfd sock: ", (int)listen_sock, BLUE_TEXT);
    printHex("IPPROTO_TCP val: ", FREERTOS_IPPROTO_TCP, BLUE_TEXT);
    if (listen_sock == FREERTOS_INVALID_SOCKET)
    {
        println("Socket is NOT valid", GREEN_TEXT);
    }
    else
    {
        println("Socket is valid", BLUE_TEXT);
        sockt = (FreeRTOS_Socket_t *)listen_sock;
        if (sockt->ucProtocol == FREERTOS_IPPROTO_TCP)
        {
            println("Proto is TCP", BLUE_TEXT);
        }
        else
        {
            printHex("Proto NOT TCP: ", sockt->ucProtocol, BLUE_TEXT);
        }
    }

    /* Set a time out so accept() will just wait for a connection. */
    FreeRTOS_setsockopt(listen_sock,
                        0,
                        FREERTOS_SO_RCVTIMEO,
                        &xReceiveTimeOut,
                        sizeof(xReceiveTimeOut));

    /**
         ** If I dont set REUSE option, accept will never return
        **/
    portBASE_TYPE xReuseSocket = pdTRUE;
    FreeRTOS_setsockopt(listen_sock,
                        0,
                        FREERTOS_SO_REUSE_LISTEN_SOCKET,
                        (void *)&xReuseSocket,
                        sizeof(xReuseSocket));

    struct freertos_sockaddr server, client;
    server.sin_port = FreeRTOS_htons((uint16_t)2056);
    //server.sin_addr = FreeRTOS_inet_addr("192.168.1.9");

    socklen_t cli_size = sizeof(client);
    println("Server task about to bind", BLUE_TEXT);
    status = FreeRTOS_bind(listen_sock, &server, sizeof(server));
    printHex("bind status: ", (int)status, BLUE_TEXT);
    sockt = (FreeRTOS_Socket_t *)listen_sock;
    printHex("Bind port: ", (unsigned int)sockt->usLocPort, BLUE_TEXT);

    println("Server task about to listen", BLUE_TEXT);
    status = FreeRTOS_listen(listen_sock, xBacklog);
    printHex("listen status: ", (int)status, BLUE_TEXT);

    int clients = 0;
    int32_t lBytes, lSent, lTotalSent;

    println("Server task accepting", BLUE_TEXT);
    Socket_t connect_sock = FreeRTOS_accept(listen_sock, (struct freertos_sockaddr *)&client, &cli_size);
    println("Connection accepted", ORANGE_TEXT);

    pucRxBuffer = (uint8_t *)pvPortMalloc(ipconfigTCP_MSS);
    for (;;)
    {
        memset(pucRxBuffer, 0x00, ipconfigTCP_MSS);
        if ((lBytes = FreeRTOS_recv(connect_sock, pucRxBuffer, ipconfigTCP_MSS, 0)) > 0)
        {
            runCommand(pucRxBuffer);
            printHex("Chars Received: ", (unsigned int)lBytes, ORANGE_TEXT);
            println(pucRxBuffer, RED_TEXT);

            lSent = 0;
            lTotalSent = 0;

            uint8_t *messageBuffer = "From server: ";
            int32_t messageBytes = sizeof(uint8_t) * strlen((char *)messageBuffer);
            int32_t totalBytes = lBytes + messageBytes;

            uint8_t *totalBuffer = (uint8_t *)malloc(totalBytes + 1);
            strcpy(totalBuffer, messageBuffer);
            strcat(totalBuffer, pucRxBuffer);

            while ((lSent >= 0) && (lTotalSent < totalBytes))
            {
                lSent = FreeRTOS_send(connect_sock, totalBuffer, totalBytes - lTotalSent, 0);
                lTotalSent += lSent;
            }
            // if (lSent < 0) break;
        }

        FreeRTOS_shutdown(connect_sock, FREERTOS_SHUT_RDWR);

        // Wait for the shutdown to take effect, indicated by FreeRTOS_recv()
        // returning an error.
        xTimeOnShutdown = xTaskGetTickCount();

        println("Waiting for shutdown", GREEN_TEXT);

        do
        {
            if (FreeRTOS_recv(connect_sock, pucRxBuffer, ipconfigTCP_MSS, 0) < 0)
                break;
        } while ((xTaskGetTickCount() - xTimeOnShutdown) < tcpechoSHUTDOWN_DELAY);

        println("Shutdown successful", GREEN_TEXT);

        vPortFree(pucRxBuffer);
        FreeRTOS_closesocket(connect_sock);

        println("Socket closed", GREEN_TEXT);
        break;
    }
}

void serverLoop()
{
    while (1)
    {
        serverListenTask();
    }
}

int main(void)
{
    SetGpioFunction(ACCELERATE_LED_GPIO, 1);
    SetGpioFunction(BRAKE_LED_GPIO, 1);
    SetGpioFunction(CLUTCH_LED_GPIO, 1);

    initFB();

    // SetGpio(ACCELERATE_LED_GPIO, 1);
    // SetGpio(BRAKE_LED_GPIO, 1);
    // SetGpio(CLUTCH_LED_GPIO, 1);

    DisableInterrupts();
    InitInterruptController();

    //ensure the IP and gateway match the router settings!
    //const unsigned char ucIPAddress[ 4 ] = {192, 168, 1, 42};
    const unsigned char ucIPAddress[4] = {10, 10, 206, 100};
    const unsigned char ucNetMask[4] = {255, 255, 255, 0};
    const unsigned char ucGatewayAddress[4] = {10, 10, 206, 1};
    const unsigned char ucDNSServerAddress[4] = {10, 10, 206, 1};
    //const unsigned char ucMACAddress[ 6 ] = {0xB8, 0x27, 0xEB, 0x19, 0xAD, 0xA7};
    const unsigned char ucMACAddress[6] = {0xB8, 0x27, 0xEB, 0xA0, 0xE8, 0x54};
    FreeRTOS_IPInit(ucIPAddress, ucNetMask, ucGatewayAddress, ucDNSServerAddress, ucMACAddress);

    xTaskCreate(serverLoop, "server", 128, NULL, 0, NULL);
    xTaskCreate(taskAccelerate, "LED_A", 128, NULL, 0, &acc);
    // xTaskCreate(taskBrake, "LED_B", 128, NULL, 0, NULL);
    // xTaskCreate(taskClutch, "LED_C", 128, NULL, 0, NULL);

    //set to 0 for no debug, 1 for debug, or 2 for GCC instrumentation (if enabled in config)
    loaded = 1;

    println("Starting task scheduler", GREEN_TEXT);

    vTaskStartScheduler();

    /*
	 *	We should never get here, but just in case something goes wrong,
	 *	we'll place the CPU into a safe loop.
	 */
    while (1)
    {
        ;
    }
}
