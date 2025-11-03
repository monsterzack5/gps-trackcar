#pragma once

struct TempAndHumidity {
    double temperature;
    double humidity;
};

TempAndHumidity get_temp_and_humidity();
float get_temperature();

int temp_sensor_init();