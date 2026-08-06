#ifndef __DHT11_H__
#define __DHT11_H__

void dht11_cmd_register(void);
void DHT11_Data(float *temp, float *hum);
extern float tem, hum;

#endif /* __DHT11_H__ */
