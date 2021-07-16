#include "cs5484_wiringpi.h"






uint8_t reg_write(uint32_t data, uint8_t addr, uint8_t csum_en)
{
    uint8_t *pdata = (uint8_t *)&data;
    uint8_t buf[5];
    uint8_t csum;

    buf[0] = 0x40 | addr;       // CMD byte
    buf[1] = pdata[2];          // Data byte (MSB)
    buf[2] = pdata[1];          // Data byte
    buf[3] = pdata[0];          // Data byte (LSB)
     
    digitalWrite(CS_PIN, 0);
    wiringPiSPIDataRW(SPI_CHANNEL, buf, 4);             // send CMD + write data
    if(csum_en != 0){
	csum = 0xFF - (pdata[0] + pdata[1] + pdata[2]);
        wiringPiSPIDataRW(SPI_CHANNEL, &csum, 1);       // send checksum after write
    }
    digitalWrite(CS_PIN, 1);

    return STATUS_OK;
}

uint8_t reg_read(uint32_t *data, uint8_t addr, uint8_t csum_en)
{
    uint8_t buf[5];
    uint8_t csum;
    
    buf[0] = 0x00 | addr;
    buf[1] = 0xFF;
    buf[2] = 0xFF;
    buf[3] = 0xFF;

    digitalWrite(CS_PIN, 0);
    wiringPiSPIDataRW(SPI_CHANNEL, buf, 1);        // send CMD
    if(csum_en != 0){
	csum = 0xFF - addr;
        wiringPiSPIDataRW(SPI_CHANNEL, &csum, 1);  // send checksum after CMD
        wiringPiSPIDataRW(SPI_CHANNEL, &buf[1], 3);  // read data
        *data = (uint32_t)(buf[3] + (8 << buf[2]) + (16 << buf[1]));
	csum = 0xFF;
        wiringPiSPIDataRW(SPI_CHANNEL, &csum, 1);  // read checksum after read
        digitalWrite(CS_PIN, 1);
        // checksum 	
        if(csum == (uint8_t)(0xFF - (buf[3] + buf[2] + buf[1]))){
	    return STATUS_OK;
	}
	else{
	    return STATUS_FAIL;
	}
    }
    else{
        wiringPiSPIDataRW(SPI_CHANNEL, &buf[1], 3);  // read data
        digitalWrite(CS_PIN, 1);
        *data = (uint32_t)(buf[3] + (buf[2] << 8) + (buf[1] << 16));
	return STATUS_OK;
    }
}

uint8_t page_select(uint8_t page, uint8_t csum_en)
{
    uint8_t buf[5];
    uint8_t csum;

    buf[0] = 0x80 | page;
    digitalWrite(CS_PIN, 0);
    wiringPiSPIDataRW(SPI_CHANNEL, buf, 1);  // send CMD
    if(csum_en != 0){
	csum = 0xFF - page;
        wiringPiSPIDataRW(SPI_CHANNEL, &csum, 1);  // send checksum
    }
    digitalWrite(CS_PIN, 1);

    return STATUS_OK;
}

uint8_t instruction(uint8_t instruct_code, uint8_t csum_en)
{
    uint8_t buf[5];
    uint8_t csum;

    buf[0] = 0xC0 | instruct_code;
    digitalWrite(CS_PIN, 0);
    wiringPiSPIDataRW(SPI_CHANNEL, buf, 1);  // send CMD
    if(csum_en != 0){
	csum = 0xFF - instruct_code;
        wiringPiSPIDataRW(SPI_CHANNEL, &csum, 1);  // send checksum
    }
    digitalWrite(CS_PIN, 1);

    return STATUS_OK;
}

uint8_t reset(uint8_t csum_en)
{
    return instruction(0x01, csum_en);
}

uint8_t start_conversion(uint8_t conversion_type, uint8_t csum_en, int timeout)
{   
    page_select(0, csum_en); // set to page 0
    reg_write(0x800000, 23, csum_en); // set Interrupt Status (Status0) to default
    
    if(conversion_type == CONVERSION_TYPE_SINGLE)          instruction(0x14, csum_en); // set single conversion 
    else if(conversion_type == CONVERSION_TYPE_CONTINUOUS) instruction(0x15, csum_en); // set continuous conversion
    else return STATUS_FAIL;

    uint32_t status0;
    uint8_t ret;
    int timeout_cnt=timeout;
    int ready=0;
    while(!ready)
    {
	ret = reg_read(&status0, 23, csum_en);
	//printf("read status0 : %x\r\n", status0);
	if(ret == STATUS_OK)
	{
            // data ready and conversion ready flag in status0
	    if((status0 & 0xC00000) == 0xC00000)
	    {   
	        printf("read status0 : %x\r\n", status0);
		printf("conversion data ready!\n");
                page_select(0, 0);
		reg_write(0x800000, 23, 0);
		ready = 1;		
	    }
	}
	else{
	    timeout_cnt--;
	}

	// check timeout
	if(timeout_cnt <= 0){
	    return STATUS_TIMEOUT;
	}

	delay(1);
    }

    return STATUS_OK;
}

uint32_t get_voltage_peak(uint8_t input_channel, uint8_t csum_en)
{
    uint8_t addr;
    uint32_t v;

    if(input_channel == ANALOG_INPUT_CH1)   	addr = 36;
    else if(input_channel == ANALOG_INPUT_CH2)	addr = 38;
    else return STATUS_FAIL;

    page_select(0, csum_en);
    reg_read(&v, addr, csum_en);

    return v;
}

uint32_t get_current_peak(uint8_t input_channel, uint8_t csum_en)
{
    uint8_t addr;
    uint32_t i;

    if(input_channel == ANALOG_INPUT_CH1)   	addr = 37;
    else if(input_channel == ANALOG_INPUT_CH2)	addr = 39;
    else return STATUS_FAIL;

    page_select(0, csum_en);
    reg_read(&i, addr, csum_en);

    return i;
}

int get_voltage_rms(uint8_t input_channel, uint8_t csum_en)
{
    uint8_t addr;
    uint32_t raw;
    int v;

    if(input_channel == ANALOG_INPUT_CH1)   	addr = 7;
    else if(input_channel == ANALOG_INPUT_CH2)	addr = 13;
    else return STATUS_FAIL;

    page_select(16, csum_en);
    reg_read(&raw, addr, csum_en);
    v  = CalFullScale(438000,0x999999,(uint32_t)raw);

    return v;
}

int get_current_rms(uint8_t input_channel, uint8_t csum_en)
{
    uint8_t addr;
    uint32_t raw;
    int i;

    if(input_channel == ANALOG_INPUT_CH1)   	addr = 6;
    else if(input_channel == ANALOG_INPUT_CH2)	addr = 12;
    else return STATUS_FAIL;

    page_select(16, csum_en);
    reg_read(&raw, addr, csum_en);
    i  = CalFullScale(150000,0x999999,(uint32_t)raw);
    
    return i;
}

int get_act_power_avg(uint8_t input_channel, uint8_t csum_en)
{
    uint8_t addr;
    uint32_t raw;
    int p;

    if(input_channel == ANALOG_INPUT_CH1)   	addr = 5;
    else if(input_channel == ANALOG_INPUT_CH2)	addr = 11;
    else return STATUS_FAIL;

    page_select(16, csum_en);
    reg_read(&raw, addr, csum_en);
    p  = CalPow(convert3byteto4byte(raw));

    return p;
}

int get_react_power_avg(uint8_t input_channel, uint8_t csum_en)
{
    uint8_t addr;
    uint32_t raw;
    int q;

    if(input_channel == ANALOG_INPUT_CH1)   	addr = 14;
    else if(input_channel == ANALOG_INPUT_CH2)	addr = 16;
    else return STATUS_FAIL;

    page_select(16, csum_en);
    reg_read(&raw, addr, csum_en);
    q = CalPow(convert3byteto4byte(raw));

    return q;
}

int get_apparent_power_avg(uint8_t input_channel, uint8_t csum_en)
{
    uint8_t addr;
    uint32_t raw;
    int s;

    if(input_channel == ANALOG_INPUT_CH1)   	addr = 20;
    else if(input_channel == ANALOG_INPUT_CH2)	addr = 24;
    else return STATUS_FAIL;

    page_select(16, csum_en);
    reg_read(&raw, addr, csum_en);
    s = CalPow(convert3byteto4byte(raw));

    return s;
}

int get_pf(uint8_t input_channel, uint8_t csum_en)
{ 
    uint8_t addr;
    uint32_t raw;
    int pf;

    if(input_channel == ANALOG_INPUT_CH1)   	addr = 21;
    else if(input_channel == ANALOG_INPUT_CH2)	addr = 25;
    else return STATUS_FAIL;

    page_select(16, csum_en);
    reg_read(&raw, addr, csum_en);
    pf = CalPF(convert3byteto4byte(raw));

    return pf;
}

int CalFullScale(uint32_t fullscale,uint32_t full_reg, uint32_t raw)
{
    return (((uint64_t)fullscale*raw)/full_reg);
}

long convert3byteto4byte(unsigned long raw)
{
    if(raw&0x800000)
	return (long)(raw-0x1000000);
    else
	return (long)(raw);
}

int CalPow(int raw)
{
    return (((int)(raw))*(long long)(POWER_LINE_FULLSCALE))/(0x7FFFFF*0.36);
}

int CalPF (int raw )
{
    return ((uint64_t)abs(raw)*10000)/0x7FFFFF;
}
