#include <iostream>
#include <string>
#include <cstring>
#include <chrono>
#include <thread>
#include <unistd.h>      // for close()
#include <sys/mman.h> // for memory locking
#include <sched.h>        // for real time scheduling
#include "can0.h"

using namespace std;
using namespace std::chrono;

const string CAN_INTERFACE = "can0";
const int SEND_ID = 0x100;
const int RECV_ID = 0x200;
constexpr int SENSOR_CAN_ID = 0x300;
const milliseconds CYCLE_TIME_MS(10);

int main() {
    // Lock memory to prevent paging
    if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
        perror("mlockall failed");
        return 1;
    }
    // Set real-time scheduling
    struct sched_param sch_params;
    sch_params.sched_priority = 80; // High priority
    if (sched_setscheduler(0, SCHED_FIFO, &sch_params) != 0) {
        perror("sched_setscheduler failed");
        // Not fatal; continue
    }

    CANSocket can(CAN_INTERFACE);
    if (!can.init()) {
        std::cerr << "Failed to initialize CAN interface\n";
        return 1;
    }

    char receivedPayload[8] = {0}; // 8-element char array for received payload

    // Prepare the message payload (8 bytes)
    char sendPayload[8];
    for (int i = 0; i < 8; ++i) sendPayload[i] = static_cast<char>(i);

    // Main loop running every 10ms
    auto startTime = high_resolution_clock::now();
    char counter = 0;

    while (true) {
        auto currentTime = high_resolution_clock::now();
        auto elapsedTime = duration_cast<milliseconds>(currentTime - startTime);

        if (elapsedTime >= CYCLE_TIME_MS) {
            startTime = currentTime;

            // Send CAN message ID 0x100
            if (can.writeFrame(SEND_ID, sendPayload) < 0) {
                // writeFrame prints error via perror already
            }

            // Non-blocking read for RECV_ID
            int r = can.readFrame(RECV_ID, receivedPayload);
            if (r == 1) {
                // receivedPayload now contains 8 bytes
                cout << "R: " << static_cast<int>(receivedPayload[0]) << endl;
                counter++;

            } else if (r < 0) {
                std::cerr << "CAN read error\n";
            }
            int r2 = can.readFrame(SENSOR_CAN_ID, receivedPayload);
            if (r2 == 1) {
                // Process sensor data in receivedPayload as needed
                cout << "Sensor Data R: " << static_cast<int>(receivedPayload[0]) << endl;
            } else if (r2 < 0) {
                std::cerr << "CAN read error for sensor data\n";
            }
            if(counter >= 100) break; // Exit after 100 messages received;
            // If select returns 0 or -1, no message arrived within the instant check, and the array is unchanged, meeting the requirement.
        }

        // Sleep briefly to manage loop frequency
        this_thread::sleep_for(milliseconds(1));
    }

    return 0;
}
