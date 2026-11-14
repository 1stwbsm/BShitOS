#include "drivers/console.h"
#include "net/net.h"
#include "net/nic.h"

static const struct nic_driver *const drivers[] = {
    &rtl8168_driver,
    &e1000_driver,
};

static const struct nic_driver *active;

int nic_init(void)
{
    for (unsigned int i = 0; i < sizeof(drivers) / sizeof(drivers[0]); i++) {
        if (drivers[i]->init() == 0) {
            active = drivers[i];
            console_print("[\033[92m ok \033[0m] network card: ");
            console_print(active->name);
            console_print("\n");
            return 0;
        }
    }
    return -1;
}

void nic_mac(uint8_t out[6])
{
    if (active)
        active->mac(out);
}

void nic_send(const void *frame, uint32_t length)
{
    if (active)
        active->send(frame, length);
}

uint32_t nic_receive(void *frame)
{
    return active ? active->receive(frame) : 0;
}
