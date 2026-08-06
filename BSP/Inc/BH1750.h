#ifndef BH1750_H_
#define BH1750_H_

void BH1750_INIT(void);
void BH7150_cmd_register(void);
uint16_t bh7150_get_data(void);
extern uint16_t lx_Data;

#endif /* BH1750_H_ */
