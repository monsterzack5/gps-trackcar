#pragma once

struct TempAndHumidity {
    double temperature;
    double humidity;
};

TempAndHumidity get_temp_and_humidity();

int temp_sensor_init();