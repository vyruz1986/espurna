//--------------------------------------------------------------------------------
// Custom RF module
// Check http://tinkerman.cat/adding-rf-to-a-non-rf-itead-sonoff/
// Enable support by passing RF_SUPPORT=1 build flag
//--------------------------------------------------------------------------------

#ifndef RF_SUPPORT
#define RF_SUPPORT                  0
#endif

#ifndef RF_PIN
#define RF_PIN                      14
#endif

#define RF_CHANNEL                  31
#define RF_DEVICE                   1

//--------------------------------------------------------------------------------
// General
//--------------------------------------------------------------------------------

#ifndef TEMPERATURE_MIN_CHANGE
#define TEMPERATURE_MIN_CHANGE      0.0             // Minimum temperature change to report
#endif

#ifndef HUMIDITY_MIN_CHANGE
#define HUMIDITY_MIN_CHANGE         0               // Minimum humidity change to report
#endif

#define TEMPERATURE_CORRECTION      0.0             // This is both for DHT and DS18B20
#define TEMPERATURE_DECIMALS        1               // Decimals for temperature values

//--------------------------------------------------------------------------------
// DHTXX temperature/humidity sensor
// Enable support by passing DHT_SUPPORT=1 build flag
//--------------------------------------------------------------------------------

#ifndef DHT_SUPPORT
#define DHT_SUPPORT                 0
#endif

#ifndef DHT_PIN
#define DHT_PIN                     14
#endif

#ifndef DHT_TYPE
#define DHT_TYPE                    DHT22
#endif

#ifndef DHT_PULLUP
#define DHT_PULLUP                  0
#endif

#ifndef DHT_UPDATE_INTERVAL
#define DHT_UPDATE_INTERVAL         60000
#endif

#define DHT_TEMPERATURE_TOPIC       "temperature"
#define DHT_HUMIDITY_TOPIC          "humidity"

#define HUMIDITY_NORMAL             0
#define HUMIDITY_COMFORTABLE        1
#define HUMIDITY_DRY                2
#define HUMIDITY_WET                3

//--------------------------------------------------------------------------------
// Analog sensor
// Enable support by passing ANALOG_SUPPORT=1 build flag
//--------------------------------------------------------------------------------

#ifndef ANALOG_SUPPORT
#define ANALOG_SUPPORT              0
#endif

#ifndef ANALOG_PIN
#define ANALOG_PIN                  0
#endif

#ifndef ANALOG_UPDATE_INTERVAL
#define ANALOG_UPDATE_INTERVAL      60000
#endif

#define ANALOG_TOPIC                "analog"

#if ANALOG_SUPPORT
    #undef ADC_VCC_ENABLED
    #define ADC_VCC_ENABLED         0
#endif

//--------------------------------------------------------------------------------
// Counter sensor
// Enable support by passing COUNTER_SUPPORT=1 build flag
//--------------------------------------------------------------------------------

#ifndef COUNTER_SUPPORT
#define COUNTER_SUPPORT             0           // Do not build with counter support by default
#endif

#ifndef COUNTER_PIN
#define COUNTER_PIN                 2           // GPIO to monitor
#endif

#ifndef COUNTER_PIN_MODE
#define COUNTER_PIN_MODE            INPUT       // INPUT, INPUT_PULLUP
#endif

#ifndef COUNTER_INTERRUPT_MODE
#define COUNTER_INTERRUPT_MODE      RISING      // RISING, FALLING, BOTH
#endif

#ifndef COUNTER_UPDATE_INTERVAL
#define COUNTER_UPDATE_INTERVAL     5000        // Update counter every this millis
#endif

#define COUNTER_REPORT_EVERY        12          // Report counter every this updates (1 minute)
#define COUNTER_DEBOUNCE            10          // Do not register events within less than 10 millis
#define COUNTER_TOPIC               "counter"   // Default topic for MQTT, API and InfluxDB

//--------------------------------------------------------------------------------
// DS18B20 temperature sensor
// Enable support by passing DS18B20_SUPPORT=1 build flag
//--------------------------------------------------------------------------------

#ifndef DS18B20_SUPPORT
#define DS18B20_SUPPORT             0
#endif

#ifndef DS18B20_PIN
#define DS18B20_PIN                 14
#endif

#ifndef DS18B20_PULLUP
#define DS18B20_PULLUP              1
#endif

#ifndef DS18B20_UPDATE_INTERVAL
#define DS18B20_UPDATE_INTERVAL     60000
#endif

#ifndef DS18B20_TEMPERATURE_TOPIC
#define DS18B20_TEMPERATURE_TOPIC   "temperature"
#endif

//--------------------------------------------------------------------------------
// PIR Motion sensor
// Enable support by passing PIR_SUPPORT=1 build flag
//--------------------------------------------------------------------------------

#ifndef PIR_SUPPORT
#define PIR_SUPPORT                 0
#endif

#ifndef PIR_PIN
#define PIR_PIN                     12
#endif

#ifndef PIR_PULLUP
#define PIR_PULLUP                  0
#endif

#ifndef PIR_UPDATE_INTERVAL
#define PIR_UPDATE_INTERVAL         60000
#endif

#define PIR_TOPIC                "motion"

//--------------------------------------------------------------------------------
// Internal power montior
// Enable support by passing ADC_VCC_ENABLED=1 build flag
// Do not enable this if using the analog GPIO for any other thing
//--------------------------------------------------------------------------------

#ifndef ADC_VCC_ENABLED
#define ADC_VCC_ENABLED             1
#endif

#if ADC_VCC_ENABLED
    ADC_MODE(ADC_VCC);
#endif
