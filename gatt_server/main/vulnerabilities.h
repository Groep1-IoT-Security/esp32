#ifndef VULNERABILITIES_H
#define VULNERABILITIES_H

void start_api_server(void);
void init_sensors(void);
void sensor_and_api_task(void *pvParameters);
void send_temp_notification(float temperature);
// Add other functions you need to call
#endif