#include <stddef.h>
#include <zephyr/kernel.h>

#pragma once

template<typename T>
inline void print_bin(T number)
{
    auto number_of_bits = ((sizeof(T) * 8));
    for (int16_t i = number_of_bits - 1; i != -1; i -= 1) {
        printk("%u", ((number >> i) & 1));
    }
}

inline void print_u8_array(const uint8_t array[], const size_t array_size)
{
    for (size_t i = 0; i < array_size; i += 1) {

        printk("data[%2u]: ", i);
        print_bin(array[i]);
        printk(" | %3u | 0x%02X | ", array[i], array[i]);

        if (array[i] > 31) {
            printk("%c", (char)array[i]);
        }

        printk("\n");
        k_sleep(K_MSEC(30));
    }
    printk("\n");
}
