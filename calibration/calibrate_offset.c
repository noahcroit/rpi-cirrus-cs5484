#include "../smartmeter_lib/cs5484_wiringpi.h"
#include "../smartmeter_lib/relay_led.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <math.h>



/*
 * Configuration struct for measurement HAT board
 *
 */ 
cs5484_basic_config_t   s_cs5484_config;
cs5484_input_config_t   s_cs5484_channel_1;
cs5484_input_config_t   s_cs5484_channel_2;
relay_led_config_t      s_relay_config;


/*
 * Function prototype in calibration program
 *
 */
int meter_wiringpi_init(cs5484_basic_config_t *s_config, relay_led_config_t *s_relay_config);
int meter_chipmeasurement_init(cs5484_basic_config_t *s_cs5484_config,
		               cs5484_input_config_t *s_cs5484_channel_1,
			       cs5484_input_config_t *s_cs5484_channel_2);
void meter_chiptest();
void meter_offset_calibration_1();
void meter_load_calibration(FILE **calibration_file, const char *filename);
void meter_save_calibration(FILE **calibration_file, const char *filename);

const char* CALIBRATION_FILENAME = "calibration_output.txt";

/*
 * Main calibration parameters
 *
 */
uint32_t v_gain;
uint32_t i_gain;
uint32_t i_acoffset;
uint32_t p_offset;
uint32_t q_offset;

int main()
{  
    /*
     * CS5484 Basic Configuration
     *
     */
    s_cs5484_config.pi_spi_channel = SPI_CHANNEL_0;
    s_cs5484_config.spi_mode = SPI_MODE_CS5484;
    s_cs5484_config.spi_bus_speed = 100000;
    s_cs5484_config.spi_cs_pin = CS_PIN;
    s_cs5484_config.spi_mosi_pin = MOSI_PIN;
    s_cs5484_config.spi_miso_pin = MISO_PIN;
    s_cs5484_config.spi_sck_pin = SCK_PIN;
    s_cs5484_config.chip_reset_pin = RST_PIN;
    s_cs5484_config.csum_en = 0;
    s_cs5484_config.conversion_type = CONVERSION_TYPE_CONTINUOUS;
    
    /*
     * CS5484 Input Configuration of Channel_1
     *
     */
    s_cs5484_channel_1.channel = 1;
    s_cs5484_channel_1.filter_mode_v = FILTER_MODE_HPF;
    s_cs5484_channel_1.filter_mode_i = FILTER_MODE_HPF;
    
    /*
     * CS5484 Input Configuration of Channel_2
     *
     */
    s_cs5484_channel_2.channel = 2;
    s_cs5484_channel_2.filter_mode_v = FILTER_MODE_DISABLE;
    s_cs5484_channel_2.filter_mode_i = FILTER_MODE_DISABLE;

    /*
     *  Relay & LED Configuration
     *
     */
    s_relay_config.relay_open_pin = PIN_RELAY_OPEN; 
    s_relay_config.relay_close_pin = PIN_RELAY_CLOSE;
    s_relay_config.relay_state = 1; 
    s_relay_config.led_kwh_pin = PIN_LED_KWH;
    


    /*
     *  initialize meter
     *
     */
    FILE *calibration_file;
    meter_wiringpi_init(&s_cs5484_config, &s_relay_config);
    meter_chiptest();
    meter_load_calibration(&calibration_file, CALIBRATION_FILENAME); // must call this before chipmeasurement_init().
    meter_chipmeasurement_init(&s_cs5484_config, &s_cs5484_channel_1, &s_cs5484_channel_2);
    


    /*
     *  offset calibration & update offset for channel1's config
     *
     */
    meter_offset_calibration_1();
    meter_save_calibration(&calibration_file, CALIBRATION_FILENAME);


         
    /*
     *  set filter settling time back to the default, 
     *
     */
    s_cs5484_config.t_settling = 30;
    cs5484_set_settlingtime(&s_cs5484_config);
    cs5484_page_select(&s_cs5484_config, 16);
    cs5484_reg_write(&s_cs5484_config, 4000, 51);
    


    uint32_t voltage_raw, current_raw, power_raw, pf_raw;
    double v, i, p, q, pf;
    double temp;
    uint32_t timeout;
    int ret;
    uint32_t data_buf;
    
    while(1){    
        cs5484_start_conversion(&s_cs5484_config);
        ret = cs5484_wait_for_conversion(&s_cs5484_config, 200);
	if(ret == STATUS_OK){
            i  = cs5484_get_current_rms(&s_cs5484_config, &s_cs5484_channel_1);
            v  = cs5484_get_voltage_rms(&s_cs5484_config, &s_cs5484_channel_1);
            p  = cs5484_get_act_power_avg(&s_cs5484_config, &s_cs5484_channel_1);
            q  = cs5484_get_react_power_avg(&s_cs5484_config, &s_cs5484_channel_1);
	    pf = cs5484_get_pf(&s_cs5484_config, &s_cs5484_channel_1);
	    
	    timeout=0;
	    while(!cs5484_is_temperature_ready(&s_cs5484_config) && !(timeout > 2000)){
	        // wait for TUP, do nothing
		// It should be better if interrupt is used in this aplication. 
		timeout++;
	    }
	    if(timeout > 2000){
                printf("chip config lost! re-init chip...\n");
		meter_chipmeasurement_init(&s_cs5484_config, &s_cs5484_channel_1, &s_cs5484_channel_2);
	    }
	    else{
		temp = cs5484_get_temperature(&s_cs5484_config);
	    }
	    
	    uint32_t i_acoff;
            i_acoff = cs5484_get_offset_current(&s_cs5484_config, &s_cs5484_channel_1);
            printf("current offset (after calibration) = %x\n", i_acoff);
            printf("AC Line Voltage, Current, Power, PF = %.3f, %.3f, %.3f, %.3f temp(C)=%.2f\n", v, i, p, pf, temp);
	}
    }
    
    return 0;
}



/////////////////////////////////////////////// Initialized functions in smartmeter /////////////////////////////////////////////////////

int meter_wiringpi_init(cs5484_basic_config_t *s_config, relay_led_config_t *s_relay_config)
{
    // WiringPi setup for GPIO and SPI bus
    wiringPiSetupGpio();
    wiringPiSPISetupMode(s_config->pi_spi_channel, s_config->spi_bus_speed, s_config->spi_mode);
    
    // Config GPIO for SPI bus
    pinMode(s_config->spi_cs_pin, OUTPUT);
    pinMode(s_config->chip_reset_pin, OUTPUT);
    pullUpDnControl(s_config->spi_cs_pin, PUD_OFF);
    pullUpDnControl(s_config->chip_reset_pin, PUD_UP);
    digitalWrite(s_config->chip_reset_pin, 1);
    digitalWrite(s_config->spi_cs_pin, 1);

    // Config GPIO for Relay & Pulse LED
    relay_led_gpio_init(s_relay_config);
    led_kwh_off(s_relay_config);
    led_kvarh_off(s_relay_config);

    // Hard Reset CS5484
    digitalWrite(s_config->chip_reset_pin, 0);
    delay(1000);
    digitalWrite(s_config->chip_reset_pin, 1);
    delay(1000);

    // Software reset
    cs5484_reset(s_config);
    delay(10000);

    return STATUS_OK;
}



int meter_chipmeasurement_init(cs5484_basic_config_t *s_cs5484_config,
		               cs5484_input_config_t *s_cs5484_channel_1,
			       cs5484_input_config_t *s_cs5484_channel_2)
{    
    // set AC offset filter
    cs5484_input_filter_init(s_cs5484_config, s_cs5484_channel_1);
    
    // enable temperature sensor
    cs5484_temperature_enable(s_cs5484_config, 1);
    
    // set I1gain, V1gain
    cs5484_set_gain_voltage(s_cs5484_config, s_cs5484_channel_1);
    cs5484_set_gain_current(s_cs5484_config, s_cs5484_channel_1);

    // set Iacoff
    cs5484_set_offset_current(s_cs5484_config, s_cs5484_channel_1);

    // set phase compensation 
    //cs5484_set_phase_compensation(s_cs5484_config, s_cs5484_channel_1);
    //
    return STATUS_OK;
}


void meter_chiptest()
{
    /*
     * CS5484 : Test Write and Read back (Echo)
     *
     */
    uint32_t data_buf;
     
    cs5484_page_select(&s_cs5484_config, 0);

    // read Config0
    cs5484_reg_read(&s_cs5484_config, &data_buf, 0);
    printf("read Config0, default val = 0x400000, read value = %x\n", data_buf);
    delay(1000);
         
    // read Config1
    cs5484_reg_read(&s_cs5484_config, &data_buf, 1);
    cs5484_reg_read(&s_cs5484_config, &data_buf, 1);
    printf("read Config1, default val = 0x00EEEE, read value = %x\n", data_buf);
    delay(1000);

    // read UART control
    cs5484_reg_read(&s_cs5484_config, &data_buf, 7);
    printf("read UART control, default val = 0x02004D, read value = %x\n", data_buf);
    delay(1000);
}



//////////////////////////////////////   Calibration Procudures ////////////////////////////////////
void meter_load_calibration(FILE **calibration_file, const char *filename)
{
    char line[1024];
    uint32_t field[5];

    *calibration_file = fopen(filename, "r");
    if(*calibration_file == NULL){
        printf("calibration file does not exist, exit read function\n");
    }
    else{
        fgets(line, 1024, *calibration_file);
        printf("calibration param : %s\n", line);
    	for(int i=0; i<5; i++){
            char *token;
            char tmp[1024];
            int len;
            int len_token;
       
	    // copy line to tmp buffer. because strtok() change input string
	    memcpy(tmp, line, sizeof(line));

	    // extract token 
            token = strtok(tmp, ",");

	    // convert string to floating-point value
	    field[i] = atof(token);

	    // update line by remove the previous token from the front of line string
	    len = strlen(line);
	    len_token = strlen(token);
            memcpy(tmp, line + len_token + 1, sizeof(line) - sizeof(len_token) - sizeof(char));
            memcpy(line, tmp, sizeof(tmp));
        }
        fclose(*calibration_file);
    }

    s_cs5484_channel_1.gain_v = field[0];
    s_cs5484_channel_1.gain_i = field[1];
    s_cs5484_channel_1.ac_offset_i = field[2];
    s_cs5484_channel_1.offset_p = field[3];
    s_cs5484_channel_1.offset_q = field[4];
}


void meter_save_calibration(FILE **calibration_file, const char *filename)
{
    /*
     *  Write calibration param to output file
     *
     */
    uint32_t field[5];
    field[0] = s_cs5484_channel_1.gain_v;
    field[1] = s_cs5484_channel_1.gain_i;
    field[2] = s_cs5484_channel_1.ac_offset_i;
    field[3] = s_cs5484_channel_1.offset_p;
    field[4] = s_cs5484_channel_1.offset_q;

    *calibration_file = fopen(filename, "r+");
    if(*calibration_file != NULL){
        char str_buf[200];
	sprintf(str_buf, "%d,%d,%d,%d,%d\n", \
	        field[0], field[1], field[2], field[3], field[4]);

	fputs(str_buf, *calibration_file);
	fclose(*calibration_file);
    }
}
void meter_offset_calibration_1()
{
    /* 
     * Offset Calibration Procedure
     *
     * 1) Remove load current
     * 2) Set settling time to 2000ms, SampleCount=16000
     * 3) Clear DRDY status bit
     * 4) Send AC Offset Calibration 0xF6 (for Current)
     * 5) Wait for DRDY is set
     * 6) Set settling time, Samplecount back to the default.
     * 7) Check if IACOFF != 0 then Save IACOFF
     */

    printf("Please disconnect load from meter...\n");
    delay(3000);

    /*
     *  Read Iacoff before calibration and reset to zero
     */
    uint32_t i_acoff;
    i_acoff = cs5484_get_offset_current(&s_cs5484_config, &s_cs5484_channel_1);
    printf("Iacoff (before calibration) = %x\n", i_acoff);
    s_cs5484_channel_1.ac_offset_i = 0;
    printf("Reset to 0...\n");
    cs5484_set_offset_current(&s_cs5484_config, &s_cs5484_channel_1);



    /*
     * Set settling time to 2000ms & SampleCount to 16000
     *
     */
    uint32_t settling_time;
    uint32_t sample_count;
    // SampleCount
    cs5484_page_select(&s_cs5484_config, 16);
    cs5484_reg_write(&s_cs5484_config, 16000, 51);
    cs5484_reg_read(&s_cs5484_config, &sample_count, 51);
    printf("sample count = %d, should be %d\n", sample_count, 16000);
    // Settling time
    s_cs5484_config.t_settling = T_SETTLING_2000MS;
    cs5484_set_settlingtime(&s_cs5484_config);
    cs5484_page_select(&s_cs5484_config, 16);
    cs5484_reg_read(&s_cs5484_config, &settling_time, 57);
    printf("settling time = %d, should be %d\n", settling_time, T_SETTLING_2000MS);
    


    /*
     * Send calibration CMD
     *
     */
    // clear DRDY, send calibration CMD
    cs5484_page_select(&s_cs5484_config, 0);          // set to page 0
    cs5484_reg_write(&s_cs5484_config, 0x800000, 23); // clear DRDY in Status0 by write 1 to it
    printf("Send offset calibration CMD...\n");
    cs5484_send_calibration_cmd_offset(&s_cs5484_config);
    while(cs5484_wait_for_conversion(&s_cs5484_config, 400) != STATUS_OK); // wait for DRDY



    /*
     *  Read offset after calibration & update value for saving param in output file
     *
     */
    i_acoff = cs5484_get_offset_current(&s_cs5484_config, &s_cs5484_channel_1);
    printf("current offset (after calibration) = %x\n", i_acoff);
    s_cs5484_channel_1.ac_offset_i = i_acoff;
    delay(1000);
}

