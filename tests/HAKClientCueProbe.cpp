#include "../src/HAKSoundNotifications.h"
int main() {
    hak::ObserveClient();hak::ObserveClient();
    hak::Notify(4);
    snooze(500000);
    return 0;
}
