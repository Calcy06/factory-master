#ifndef SCD04_H_
#define SCD04_H_

extern float tem, hum;

void SCD04_INIT(void);
void SCD04_cmd_register(void);
void DHT11_Data(float *temp, float *hum);
int scd04_get_Data(void);
int scd04_collect(void);
int scd04_read_data(void);
extern uint16_t data_co2;

#endif /* SCD04_H_ */
