/*
 * ESP8266_HAL.c
 *
 *  Created on: Apr 14, 2020
 *      Author: Controllerstech
 */


#include "ESP8266_HAL.h"

#include "main.h"
#include "stdio.h"
#include "string.h"

extern UART_HandleTypeDef huart6;
extern UART_HandleTypeDef huart2;

#define wifi_uart &huart6
#define pc_uart &huart2


char buffer[20];

char *LED_ON = "!DOCTYPE html> <html><p>LED Status: ON</p><a class=\"button button-off\" href=\"/ledoff\">OFF</a></body></html>";
char *LED_OFF = "<!DOCTYPE html> <html></head><p>LED1 Status: OFF</p><a class=\"button button-on\" href=\"/ledon\">ON</a></body></html>";


uint8_t rx_buffer[50];
volatile uint8_t rx_index = 0;
volatile uint8_t rx_complete = 0;

/*****************************************************************************************************************************************/

static void Uart_sendstring(const char *s, UART_HandleTypeDef *uart)
{
    HAL_UART_Transmit(uart, (uint8_t *)s, strlen(s), 1000);
}

static int Wait_for(char *string, UART_HandleTypeDef *uart)
{
    uint32_t start_time = HAL_GetTick();
    uint32_t timeout = 15000;
    rx_index = 0;
    rx_complete = 0;
    memset(rx_buffer, 0, sizeof(rx_buffer));

    // Start received data via uart
    HAL_UART_Receive_IT(uart, &rx_buffer[rx_index], 1);

    while (!rx_complete && (HAL_GetTick() - start_time) < timeout)
    {
        HAL_Delay(10);

    if (rx_complete)
    {
        if (strncmp((char *)rx_buffer, string, strlen(string)) == 0)
        {
            return HAL_OK; // received correct string
        }
        else
        {
            return HAL_ERROR; // received another string
        }
    }

    return HAL_TIMEOUT; // Timeout
}

int Copy_upto(char *string, char *buffertocopyinto, UART_HandleTypeDef *uart)
{
    uint32_t start_time = HAL_GetTick();
    uint32_t timeout = 5000; // Timeout 5 seconds
    rx_index = 0;
    rx_complete = 0;
    memset(rx_buffer, 0, sizeof(rx_buffer));
    memset(buffertocopyinto, 0, strlen (buffertocopyinto)); // clear destination buffer
    HAL_UART_Receive_IT(uart, &rx_buffer[rx_index], 1);

    while (!rx_complete && (HAL_GetTick() - start_time) < timeout)
    {
        // Check if inside buffer founded string
        if (rx_index >= strlen(string))
        {
            if (strstr((char *)rx_buffer, string) != NULL)
            {
                rx_complete = 1; // string founded
            }
        }
        HAL_Delay(10); // short delay
    }

    if (rx_complete)
    {
        // Copy data into buffertocopyinto
        char *found = strstr((char *)rx_buffer, string);
        if (found)
        {
            size_t len_to_copy = (found - (char *)rx_buffer) + strlen(string) + 1; // +1 for null-terminatora
            if (len_to_copy <= sizeof(rx_buffer))
            {
                strncpy(buffertocopyinto, (char *)rx_buffer, len_to_copy);
                buffertocopyinto[len_to_copy - 1] = '\0'; // null-terminator
                return 1; // Success
            }
        }
    }

    return -1; // error timeout or string not founded
}

int Get_after(char *string, uint8_t numberofchars, char *buffertosave, UART_HandleTypeDef *uart)
{
    uint32_t start_time = HAL_GetTick();
    uint32_t timeout = 5000; // Timeout 5 seconds
    rx_index = 0;
    rx_complete = 0;
    memset(rx_buffer, 0, sizeof(rx_buffer));
    memset(buffertosave, 0, strlen(buffertosave));

    HAL_UART_Receive_IT(uart, &rx_buffer[rx_index], 1);

    while (!rx_complete && (HAL_GetTick() - start_time) < timeout)
    {
        if (rx_index >= strlen(string))
        {
            char *found = strstr((char *)rx_buffer, string);
            if (found && (rx_index >= (found - (char *)rx_buffer) + strlen(string) + numberofchars))
            {
                rx_complete = 1; // string founded
            }
        }
        HAL_Delay(10);
    }

    if (rx_complete)
    {
        char *found = strstr((char *)rx_buffer, string);
        if (found)
        {
            size_t start_pos = (found - (char *)rx_buffer) + strlen(string); // begin of string
            if (start_pos + numberofchars <= rx_index) // Check correct number of characters
            {
                strncpy(buffertosave, (char *)(rx_buffer + start_pos), numberofchars);
                buffertosave[numberofchars] = '\0'; // Null-terminator
                return 1; // Success
            }
        }
    }

    return -1; // timeout error
}

int Look_for(char *str, char *buffertolookinto)
{
    if (strstr(buffertolookinto, str) != NULL)
    {
        return 1; // String found
    }
    return -1; // String not found
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART6)
    {
        // received 1 byte
        rx_index++;

        // If received \n or full buffer end receiving
        if (rx_buffer[rx_index-1] == '\n' || rx_index >= sizeof(rx_buffer)-1)
        {
            rx_complete = 1; // set flag
        }
        else
        {
            // continue receiving
            HAL_UART_Receive_IT(&huart6, &rx_buffer[rx_index], 1);
        }
    }
}

void ESP_Init (char *SSID, char *PASSWD)
{
	char data[80];

	Uart_sendstring("AT+RST\r\n", wifi_uart);
	Uart_sendstring("RESETTING.", pc_uart);
	for (int i=0; i<5; i++)
	{
		Uart_sendstring(".", pc_uart);
		HAL_Delay(1000);
	}

	/********* AT **********/
	Uart_sendstring("AT\r\n", wifi_uart);
	while(!(Wait_for("AT\r\r\n\r\nOK\r\n", wifi_uart))){
		uint8_t buffer[10];
		HAL_UART_Receive(&huart6, buffer, 10, 1000);
	}
	Uart_sendstring("AT---->OK\n\n", pc_uart);


	/********* AT+CWMODE=1 **********/
	Uart_sendstring("AT+CWMODE=1\r\n", wifi_uart);
	while (!(Wait_for("AT+CWMODE=1\r\r\n\r\nOK\r\n", wifi_uart)));
	Uart_sendstring("CW MODE---->1\n\n", pc_uart);


	/********* AT+CWJAP="SSID","PASSWD" **********/
	Uart_sendstring("connecting... to the provided AP\n", pc_uart);
	sprintf (data, "AT+CWJAP=\"%s\",\"%s\"\r\n", SSID, PASSWD);
	Uart_sendstring(data, wifi_uart);
	while (!(Wait_for("WIFI GOT IP\r\n\r\nOK\r\n", wifi_uart)));
	sprintf (data, "Connected to,\"%s\"\n\n", SSID);
	Uart_sendstring(data,pc_uart);


	/********* AT+CIFSR **********/
	Uart_sendstring("AT+CIFSR\r\n", wifi_uart);
	while (!(Wait_for("CIFSR:STAIP,\"", wifi_uart)));
	while (!(Copy_upto("\"",buffer, wifi_uart)));
	while (!(Wait_for("OK\r\n", wifi_uart)));
	int len = strlen (buffer);
	buffer[len-1] = '\0';
	sprintf (data, "IP ADDR: %s\n\n", buffer);
	Uart_sendstring(data, pc_uart);


	Uart_sendstring("AT+CIPMUX=1\r\n", wifi_uart);
	while (!(Wait_for("AT+CIPMUX=1\r\r\n\r\nOK\r\n", wifi_uart)));
	Uart_sendstring("CIPMUX---->OK\n\n", pc_uart);

	Uart_sendstring("AT+CIPSERVER=1,80\r\n", wifi_uart);
	while (!(Wait_for("OK\r\n", wifi_uart)));
	Uart_sendstring("CIPSERVER---->OK\n\n", pc_uart);

	Uart_sendstring("Now Connect to the IP ADRESS\n\n", pc_uart);

}




int Server_Send (char *str, int Link_ID)
{
	int len = strlen (str);
	char data[80];
	sprintf (data, "AT+CIPSEND=%d,%d\r\n", Link_ID, len);
	Uart_sendstring(data, wifi_uart);
	while (!(Wait_for(">", wifi_uart)));
	Uart_sendstring (str, wifi_uart);
	while (!(Wait_for("SEND OK", wifi_uart)));
	sprintf (data, "AT+CIPCLOSE=5\r\n");
	Uart_sendstring(data, wifi_uart);
	while (!(Wait_for("OK\r\n", wifi_uart)));
	return 1;
}

void Server_Handle (char *str, int Link_ID)
{
	char datatosend[1024] = {0};
	if (!(strcmp (str, "/ledon")))
	{
		sprintf (datatosend, LED_ON);
		Server_Send(datatosend, Link_ID);
	}

	else if (!(strcmp (str, "/ledoff")))
	{
		sprintf(datatosend, LED_OFF);
		Server_Send(datatosend, Link_ID);
	}

	else
	{
		sprintf (datatosend, LED_OFF);
		Server_Send(datatosend, Link_ID);
	}

}

void Server_Start (void)
{
	char buftocopyinto[64] = {0};
	char Link_ID;
	while (!(Get_after("+IPD,", 1, &Link_ID, wifi_uart)));
	Link_ID -= 48;
	while (!(Copy_upto(" HTTP/1.1", buftocopyinto, wifi_uart)));
	if (Look_for("/ledon", buftocopyinto) == 1)
	{
		HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, 1);
		Server_Handle("/ledon",Link_ID);
	}

	else if (Look_for("/ledoff", buftocopyinto) == 1)
	{
		HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, 0);
		Server_Handle("/ledoff",Link_ID);
	}

	else if (Look_for("/favicon.ico", buftocopyinto) == 1);

	else
	{
		HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, 0);
		Server_Handle("/ ", Link_ID);
	}
}
