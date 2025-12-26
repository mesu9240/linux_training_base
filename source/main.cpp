#include <iostream>
#include <string>
#include <cstring>
#include <chrono>
#include <thread>
#include <unistd.h>      // for close()
#include <sys/socket.h>  // for socket functions
#include <sys/ioctl.h>   // for ioctl()
#include <linux/can.h>   // for CAN frame structure
#include <linux/can/raw.h> // for raw CAN sockets
#include <net/if.h>      // for ifreq structure

using namespace std;
using namespace std::chrono;

const string CAN_INTERFACE = "can0";
const int SEND_ID = 0x100;
const int RECV_ID = 0x200;
const milliseconds CYCLE_TIME_MS(10);

int main() {
    int s; // Socket file descriptor
    struct sockaddr_can addr;
    struct ifreq ifr;
    struct can_frame frame_send;
    struct can_frame frame_recv;
    
    char receivedPayload[8] = {0}; // 8-element char array for received payload

    // 1. Create a socket
    // PF_CAN domain, SOCK_RAW type, CAN_RAW protocol
    s = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (s < 0) {
        perror("socket");
        return 1;
    }

    // 2. Specify the CAN interface name
    strcpy(ifr.ifr_name, CAN_INTERFACE.c_str());
    ioctl(s, SIOCGIFINDEX, &ifr); // Retrieve the interface index

    // 3. Bind the socket to the interface
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(s);
        return 1;
    }

    // Optional: Set a receive filter to only receive ID 0x200 (exact match)
    struct can_filter rfilter[1];
    rfilter[0].can_id = RECV_ID;
    rfilter[0].can_mask = CAN_SFF_MASK; // Standard frame format mask
    setsockopt(s, SOL_CAN_RAW, CAN_RAW_FILTER, &rfilter, sizeof(rfilter));

    // Optional: Set socket to non-blocking mode for receive function logic
    // This allows recvfrom to return immediately if no message is pending.
    // fcntl(s, F_SETFL, O_NONBLOCK);


    // Prepare the message to send (ID 0x100, 8 bytes)
    frame_send.can_id = SEND_ID;
    frame_send.can_dlc = 8;
    for (int i = 0; i < 8; ++i) {
        frame_send.data[i] = i; 
    }

    // Main loop running every 10ms
    auto startTime = high_resolution_clock::now();

    while (true) {
        auto currentTime = high_resolution_clock::now();
        auto elapsedTime = duration_cast<milliseconds>(currentTime - startTime);

        if (elapsedTime >= CYCLE_TIME_MS) {
            startTime = currentTime;

            // Send CAN message ID 0x100
            if (write(s, &frame_send, sizeof(struct can_frame)) != sizeof(struct can_frame)) {
                perror("write");
            }

            // Receive CAN messages (non-blocking logic required by user request)
            // Using a timeout with select() is a robust way to achieve this
            fd_set read_fds;
            struct timeval tv;

            tv.tv_sec = 0;
            tv.tv_usec = 0; // Check instantly during this loop iteration

            FD_ZERO(&read_fds);
            FD_SET(s, &read_fds);

            if (select(s + 1, &read_fds, NULL, NULL, &tv) > 0) {
                if (read(s, &frame_recv, sizeof(struct can_frame)) == sizeof(struct can_frame)) {
                    if (frame_recv.can_id == RECV_ID && frame_recv.can_dlc == 8) {
                        memcpy(receivedPayload, frame_recv.data, 8);
                        // Data successfully received and copied to receivedPayload
                        // cout << "R: " << (int)receivedPayload[0] << endl;
                    }
                }
            }
            // If select returns 0 or -1, no message arrived within the instant check, and the array is unchanged, meeting the requirement.
        }

        // Sleep briefly to manage loop frequency
        this_thread::sleep_for(milliseconds(1));
    }

    // Close the socket (unreachable in infinite loop)
    close(s);
    return 0;
}
