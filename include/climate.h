#ifndef __FFF_TH_H__
#define __FFF_TH_H__

#include <Arduino.h>
#include <SimpleDHT.h>

namespace climate {
    struct dht_data {
        byte temp;
        byte humid;
    };

    dht_data getInside();
    dht_data getOutside();

    class Season;
    class Spring;
    class Summer;
    class Autumn;
    class Winter;
}

#endif // __FFF_TH_H__