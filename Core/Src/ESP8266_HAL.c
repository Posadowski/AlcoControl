#include "ESP8266_HAL.h"

#include "main.h"
#include "stdio.h"
#include "string.h"

extern UART_HandleTypeDef huart6;
extern UART_HandleTypeDef huart2;

#define wifi_uart &huart6
#define pc_uart &huart2
#define RX_BUFFER_SIZE 1024
#define UART_TIMEOUT_MS 5000

char buffer[RX_BUFFER_SIZE];

char *LED_ON = "<!DOCTYPE html> <html><p>LED Status: ON</p><a class=\"button button-off\" href=\"/ledoff\">OFF</a></body></html>";
char *LED_OFF = "<!DOCTYPE html> <html></head><p>LED1 Status: OFF</p><a class=\"button button-on\" href=\"/ledon\">ON</a></body></html>";


uint8_t rx_buffer[RX_BUFFER_SIZE];
volatile uint8_t rx_index = 0;
volatile uint8_t rx_complete = 0;

/*****************************************************************************************************************************************/

static void Uart_sendstring(const char *s, UART_HandleTypeDef *uart)
{
    HAL_UART_Transmit(uart, (uint8_t *)s, strlen(s), 1000);
}

int Wait_for(const char *string, UART_HandleTypeDef *uart)
{
    rx_index = 0;
    rx_complete = 0;
    memset(rx_buffer, 0, sizeof(rx_buffer));

    // Start receiving first byte
    HAL_UART_Receive_IT(uart, &rx_buffer[rx_index], 1);

    uint32_t timeout = HAL_GetTick() + UART_TIMEOUT_MS;

    while (HAL_GetTick() < timeout)
    {
         if (strstr((char *)rx_buffer, string) != NULL)
        {
            memset(rx_buffer, 0, sizeof(rx_buffer));
            rx_index = 0;
            rx_complete = 1;
            return HAL_OK;
        }

        HAL_Delay(10);
    }
    return HAL_TIMEOUT;
}



int Copy_upto(const char *string, char *result, UART_HandleTypeDef *uart)
{

	// Start receiving first byte
    HAL_UART_Receive_IT(uart, &rx_buffer[rx_index], 1);

    uint32_t timeout = HAL_GetTick() + UART_TIMEOUT_MS;

    while (HAL_GetTick() < timeout)
    {
        char *end = strstr((char *)rx_buffer, string);
        if (end != NULL)
        {
            *end = '\0'; // End string before "string"
            strncpy(result, (char *)rx_buffer, end - (char *)rx_buffer); // Copy data to buffer
            memset(rx_buffer, 0, sizeof(rx_buffer));
            rx_index = 0;
            rx_complete = 1;
            return 1; // SUCCESS
        }
        HAL_Delay(10);
    }
    return -1; // TIMEOUT
}
int Get_after(const char *string, uint8_t length, char *result, UART_HandleTypeDef *uart)
{
    rx_index = 0;
    rx_complete = 0;
    memset(rx_buffer, 0, sizeof(rx_buffer));

    // Start receiving first byte
    HAL_UART_Receive_IT(uart, &rx_buffer[rx_index], 1);

    uint32_t timeout = HAL_GetTick() + UART_TIMEOUT_MS; // 5 seconds timeout

    while (HAL_GetTick() < timeout)
    {
        if (rx_complete || strstr((char *)rx_buffer, string) != NULL)
        {
            char *start = strstr((char *)rx_buffer, string);
            if (start != NULL)
            {
                start += strlen(string); // Move the pointer behind the string
                if (strlen(start) >= length) // Check if there is enough data
                {
                    strncpy(result, start, length); // Copy a specific number of characters
                    result[length] = '\0'; // Terminate the string with null
                    return 0; // Success
                }
            }
        }
        HAL_Delay(10);
    }
    return -1; // TIMEOUT
}

int Look_for(const char *str, const char *buffertolookinto)
{
    return (strstr(buffertolookinto, str) != NULL) ? 1 : -1;
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART6)
    {

        if(!rx_complete){
        	HAL_UART_Receive_IT(&huart6, &rx_buffer[rx_index], 1);
        	rx_index++;
			if(rx_index >= RX_BUFFER_SIZE){
				rx_index = 0;
			}
        }

    }
}

void ESP_Init (char *SSID, char *PASSWD)
{
	char data[1024];

	Uart_sendstring("AT+RST\r\n", wifi_uart);
	Uart_sendstring("RESETTING.", pc_uart);
	for (int i=0; i<5; i++)
	{
		Uart_sendstring(".", pc_uart);
		HAL_Delay(1000);
	}

	/********* AT **********/
	Uart_sendstring("AT\r\n", wifi_uart);
	while(Wait_for("OK", wifi_uart) == HAL_ERROR);
	Uart_sendstring("AT---->OK\n\n", pc_uart);


	/********* AT+CWMODE=1 **********/
	Uart_sendstring("AT+CWMODE=1\r\n", wifi_uart);
	while (Wait_for("OK", wifi_uart) == HAL_ERROR);
	Uart_sendstring("CW MODE---->1\n\n", pc_uart);


	/********* AT+CWJAP="SSID","PASSWD" **********/
	Uart_sendstring("connecting... to the provided AP\n", pc_uart);
	sprintf (data, "AT+CWJAP=\"%s\",\"%s\"\r\n", SSID, PASSWD);
	Uart_sendstring(data, wifi_uart);
	while (Wait_for("WIFI GOT IP\r\n\r\nOK\r\n", wifi_uart) == HAL_ERROR);
	sprintf (data, "Connected to,\"%s\"\n\n", SSID);
	Uart_sendstring(data,pc_uart);


	/********* AT+CIFSR **********/
	Uart_sendstring("AT+CIFSR\r\n", wifi_uart);
	while (Wait_for("OK", wifi_uart)== HAL_ERROR);

	Uart_sendstring("AT+CIPMUX=1\r\n", wifi_uart);
	while (Wait_for("OK", wifi_uart) == HAL_ERROR);
	Uart_sendstring("CIPMUX---->OK\n\n", pc_uart);

	Uart_sendstring("AT+CIPSERVER=1,80\r\n", wifi_uart);
	while (Wait_for("OK", wifi_uart) == HAL_ERROR);
	Uart_sendstring("CIPSERVER---->OK\n\n", pc_uart);

	Uart_sendstring("Now Connect to the IP ADRESS\n\n", pc_uart);

}




int Server_Send (char *str, int Link_ID)
{
	int len = strlen (str);
	char data[80];
	sprintf (data, "AT+CIPSEND=%d,%d\r\n", Link_ID, len);
	Uart_sendstring(data, wifi_uart);
	while (Wait_for(">", wifi_uart) == HAL_ERROR);
	Uart_sendstring (str, wifi_uart);
	while (Wait_for("SEND OK", wifi_uart) == HAL_ERROR);
	sprintf (data, "AT+CIPCLOSE=5\r\n");
	Uart_sendstring(data, wifi_uart);
	while (Wait_for("OK\r\n", wifi_uart) == HAL_ERROR);
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

void Server_Start(void)
{
    char buftocopyinto[64] = {0};
    char Link_ID_str[2] = {0}; // 1 char + null

    // Read Link_ID from response +IPD,x,...
    if (Get_after("+IPD,", 1, Link_ID_str, wifi_uart) == -1){
    	return;
    }

    // Convert digit sign to int Link_ID
    char Link_ID = Link_ID_str[0] - '0';

    Uart_sendstring("client get\n", pc_uart);

    // Get HTTP request to buffer
    if(Copy_upto(" HTTP/1.1", buftocopyinto, wifi_uart) != 1){
    	return;
    }

    if (Look_for("/ledon", buftocopyinto) == 1)
    {
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_SET);
        Server_Handle("/ledon", Link_ID);
    }
    else if (Look_for("/ledoff", buftocopyinto) == 1)
    {
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_RESET);
        Server_Handle("/ledoff", Link_ID);
    }
    else if (Look_for("/favicon.ico", buftocopyinto) == 1)
    {
        // Ignore favicon
    }
    else
    {
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_RESET);
        Server_Handle("/ ", Link_ID);
    }
}
